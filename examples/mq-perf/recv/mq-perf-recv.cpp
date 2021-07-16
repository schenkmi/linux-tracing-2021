/**
 * build with g++ -g -O2 -o mq-perf-recv mq-perf-recv.cpp -lrt
 */

/* local includes */
#include "shmemq.h"

/* global includes */
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <getopt.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <iostream>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <mqueue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sched.h>
#include <functional>
#include "TimeProfiling.h"

#define SHMEM_NAME              "gugus"
#define SHMEM_MAX_MESSAGES      100
#define UDS_FILE                "/tmp/sock.uds"
#define MEASURE_SAFETY_MARGIN   100 /* remove the first and last 100 measurements */
#define QUEUE_NAME              "/mq-perf"
#define QUEUE_PERMISSIONS       0660
#define MAX_MESSAGES            10
#define MAX_MSG_SIZE            4096
#define MSG_BUFFER_SIZE         MAX_MSG_SIZE + 10
#define MSG_SEND_SIZE           256                 /* we seend 256 bytes */
#define MSG_HDR_SIZE            (sizeof(int64_t) + sizeof(uint32_t))
#define IPC_METHOD_MQ           "mq"
#define IPC_METHOD_UDS          "uds"
#define IPC_METHOD_SHMEM        "shmem"
#define IPC_ENC_PROTOBUF        "protobuf"
#define IPC_ENC_RAW             "raw"
#define PROGRAM 		        "mq-perf-recv"
#define PROGRAMVERSION 		    "0.0.6"

static int running = 1;
static TimeProfiling timeProfiling;
static int optThreadPrio = 50;      /* fifo with prio 50            */
static char* optIPCMethod = nullptr;
static unsigned int optAffinityMask = 0;
static char* optEncapsulation = nullptr;
static int optStartDelay = 0;
static int optDuration = 0;
static int optBurstCount = 0;       /* no burst                     */
static std::function<void(char**, ssize_t*)> aquireFunc;
static std::function<void(char**, ssize_t*)> releaseFunc;

/**
 * display version
 */
void display_version (void)
{
    printf(PROGRAM " " PROGRAMVERSION "\n"
           "\n"
           "\n"
           PROGRAM " comes with NO WARRANTY\n"
           "to the extent permitted by law.\n"
           "\n");

    exit(0);
}

/**
 * display help
 */
void display_help (void)
{
    printf("Usage: " PROGRAM " [OPTIONS]\n"
           "low level ipc latency test application (recv part)\n"
           "\n"
           "example: " PROGRAM " --encapsulation=raw --ipc=mq --prio=50\n"
           "\n"
           "  --help                              Show this menu\n"
           "  --version                           Show version of this application\n"
           "  -i, --ipc=[mq|uds|shmem]            Use MQ, Unix domain socket or shared memory as IPC\n"
           "  -m, --mask                          CPU affinity mask\n"
           "  -b, --burst                         Expected number of messages coming as burst (0 = single messages, no burst)\n"
           "  -p, --prio                          Thread priority (FIFO scheduling)\n"
           "  -s, --start                         Time in seconds starting capture timestamps\n"
           "  -d, --duration                      Duration in seconds while capture timestamps\n");
    exit(-1);
}

/**
 *
 */
void process_options(int argc, char *argv[])
{
    int error = 0;

    for (;;) {
        int option_index = 0;
        static const char *short_options = "m:i:p:s:d:b:";

        static const struct option long_options[] = {
                { "help",          no_argument,       0,  0  },
                { "version",       no_argument,       0,  0  },
                { "mask",          required_argument, 0, 'm' },
                { "burst",         required_argument, 0, 'b' },
                { "ipc",           required_argument, 0, 'i' },
                { "prio",          required_argument, 0, 'p' },
                { "encapsulation", required_argument, 0, 'e' },
                { "start",         required_argument, 0, 's' },
                { "duration",      required_argument, 0, 'd' },
                { 0,               0,                 0,  0	 },
        };

        int c = getopt_long(argc, argv, short_options,
                            long_options, &option_index);
        /* detect the end of the options. */
        if (c == -1) {
            break;
        }

        switch (c) {
            case 0:
                switch (option_index) {
                    case 0:
                        display_help();
                        break;
                    case 1:
                        display_version();
                        break;
                }
                break;
            case 'm':
                optAffinityMask = strtoul(optarg, 0, 16);
                break;
            case 'b':
                optBurstCount = atoi(optarg);
                break;
            case 'p':
                optThreadPrio = atoi(optarg);
                break;
            case 'i':
                optIPCMethod = strdup(optarg);
                break;
            case 's':
                optStartDelay = atoi(optarg);
                break;
            case 'd':
                optDuration = atoi(optarg);
                break;
            case '?':
                error = 1;
                break;
        }
    }

    if ((argc - optind) != 0) {
        error = 1;
    }

    if (error) {
        display_help();
    }
}

static int get_one_character(char* c)
{
    struct termios tmbuf,tmsave;

    if (tcgetattr(0,&tmbuf)) {
        return -1;
    }

    // save current state
    memcpy(&tmsave, &tmbuf, sizeof(tmbuf));

    tmbuf.c_lflag &= ~ICANON; // clear line oriented input
    tmbuf.c_cc[VMIN] = 1;     // number of bytes to read before read returns
    tmbuf.c_cc[VTIME] = 0;    // no timeout, wait forever

    // write new termios configuration
    if (tcsetattr(0, TCSANOW, &tmbuf)) {
        return -1;
    }

    // read a single character
    if (read(STDIN_FILENO, c, 1) != 1) {
        return -1;
    }

    // restore
    if (tcsetattr(0, TCSANOW, &tmsave)) {
        return -1;
    }

    return 0;
}

static void configure_cpu_affinity()
{
    /* get number of cores */
    int nproc = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    if (optAffinityMask > 0) {
        for (int cnt = 0; cnt < nproc; cnt++) {
            if ((optAffinityMask & (1 << cnt)) != 0) {
                /* enable core */
                CPU_SET(cnt, &cpuset);
            }
        }

        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
            perror("sched_setaffinity() failed");
        }
    }

    printf("We have %d CPUs\n", nproc);

    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpuset), &cpuset) != -1) {
        for (int i = 0; i < nproc; i++) {
            printf("  CPU %d is %s\n", i, CPU_ISSET(i, &cpuset) ? "enabled" : "disabled");
        }
    }
    else {
        perror("sched_getaffinity() failed");
    }
}

/* start of plain, no protobuf */
static void aquire_message_0(char** buffer, ssize_t* size)
{
    if (*buffer == nullptr) {
        *size = MSG_BUFFER_SIZE;
        *buffer = (char*)std::malloc(*size);
    }
}

static void release_message_0(char** buffer, ssize_t* size)
{
    if (*size > (ssize_t)MSG_HDR_SIZE) {
        timeProfiling.add(*((int64_t*)*buffer));
        // element counter access
        //printf("%d\n", *(uint32_t*)&(*buffer)[sizeof(int64_t)]);
    }
}
/* end of plain, no protobuf */

void recv_func(mqd_t mq_descriptor)
{
    char* recv_buffer = nullptr;
    ssize_t recv_size = 0;
    struct timespec tm;
    ssize_t len;

    std::cout << "start receive mq with prio [" << optThreadPrio << "]" << std::endl;

    pthread_setname_np(pthread_self(), "mq_recv");

    struct sched_param param = { .sched_priority = optThreadPrio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    configure_cpu_affinity();

    while (running) {
        // get the oldest message with highest priority
        clock_gettime(CLOCK_REALTIME, &tm);
        tm.tv_sec += 1;

        aquireFunc(&recv_buffer, &recv_size);

        len = mq_timedreceive(mq_descriptor, recv_buffer, recv_size, NULL, &tm);

        releaseFunc(&recv_buffer, &len);
    }

    if (recv_buffer) {
        std::free(recv_buffer);
    }
}

void recv_uds_func(int sockfd)
{
    char* recv_buffer = nullptr;
    ssize_t recv_size = 0;
    ssize_t len;

    std::cout << "start receive UDS with prio [" << optThreadPrio << "]" << std::endl;

    pthread_setname_np(pthread_self(), "uds_recv");

    struct sched_param param = { .sched_priority = optThreadPrio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    configure_cpu_affinity();

    while (running) {
        aquireFunc(&recv_buffer, &recv_size);

        len = recv(sockfd, recv_buffer, recv_size, 0);

        releaseFunc(&recv_buffer, &len);
    }

    if (recv_buffer) {
        std::free(recv_buffer);
    }
}

void recv_shmem_func(shmemq_t* shmemq)
{
    char* recv_buffer = nullptr;
    ssize_t recv_size = 0;


    std::cout << "start receive shmem with prio [" << optThreadPrio << "]" << std::endl;

    pthread_setname_np(pthread_self(), "shmem_recv");

    struct sched_param param = { .sched_priority = optThreadPrio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    configure_cpu_affinity();

    while (running) {
        aquireFunc(&recv_buffer, &recv_size);

        /* shmem needs a fixed size */
        recv_size = (MSG_SEND_SIZE + MSG_HDR_SIZE);
        shmemq_dequeue(shmemq, recv_buffer, recv_size);

        releaseFunc(&recv_buffer, &recv_size);
    }

    if (recv_buffer) {
        std::free(recv_buffer);
    }
}

int main(int argc, char **argv)
{
    char ch;
    std::thread recv_thread;
    mqd_t mq_descriptor;   // queue descriptors
    struct mq_attr attr = { .mq_flags = 0,
            .mq_maxmsg = MAX_MESSAGES,
            .mq_msgsize = MAX_MSG_SIZE,
            .mq_curmsgs = 0 };
    int sockfd;
    struct sockaddr_un servaddr;
    shmemq_t* shmemq = nullptr;

    /* parse given cmd line args */
    process_options(argc, argv);

    timeProfiling.configure(optStartDelay, optDuration);
    timeProfiling.start();

    optEncapsulation = optEncapsulation ? optEncapsulation : strdup(IPC_ENC_RAW);

    aquireFunc = std::function<void(char**, ssize_t*)>(aquire_message_0);
    releaseFunc = std::function<void(char**, ssize_t*)>(release_message_0);

    optIPCMethod = optIPCMethod ? optIPCMethod : strdup(IPC_METHOD_MQ);

    if (strncmp(optIPCMethod, IPC_METHOD_UDS, strlen(IPC_METHOD_UDS)) == 0) {
        if ((sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0)) == -1) {
            perror("socket() failed");
        }

        unlink(UDS_FILE);

        bzero(&servaddr, sizeof(servaddr));
        servaddr.sun_family = AF_LOCAL;
        strcpy(servaddr.sun_path, UDS_FILE);

        if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
            perror("bind() failed");
            close(sockfd);
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        recv_thread = std::thread(recv_uds_func, sockfd);
    }
    else if (strncmp(optIPCMethod, IPC_METHOD_SHMEM, strlen(IPC_METHOD_SHMEM)) == 0) {
        /* as we have a queue of fixed elements size must match */
        shmemq = shmemq_new(SHMEM_NAME, SHMEM_MAX_MESSAGES, (MSG_SEND_SIZE + MSG_HDR_SIZE));

        recv_thread = std::thread(recv_shmem_func, shmemq);
    }
    else {
        if ((mq_descriptor = mq_open(QUEUE_NAME, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
            perror("Server: mq_open (server)");
            exit(1);
        }

        recv_thread = std::thread(recv_func, mq_descriptor);
    }

    while (running == 1) {
        printf("\nEnter command : ");
        fflush(stdout);
        get_one_character(&ch);
        printf("\n");
        switch (ch) {
            case 'h':
                printf("q.) exit application\n");
                printf("h.) this help\n");
                break;
            case 'q':
                printf("--> quit\n");
                running = 0;
                break;
        }
    }

    if (recv_thread.joinable()) {
        recv_thread.join();
    }

    if (strncmp(optIPCMethod, IPC_METHOD_UDS, strlen(IPC_METHOD_UDS)) == 0) {
        close(sockfd);
        unlink(UDS_FILE);
    }
    else if (strncmp(optIPCMethod, IPC_METHOD_SHMEM, strlen(IPC_METHOD_SHMEM)) == 0) {
        if (shmemq) {
            shmemq_destroy(shmemq, 0 /* do not unlink */);
            shmemq = nullptr;
        }
    }
    else {
        mq_close(mq_descriptor);
        mq_unlink(QUEUE_NAME);
    }

    if (optIPCMethod) {
        free(optIPCMethod);
    }

    if (optIPCMethod) {
        free(optIPCMethod);
    }

    timeProfiling.process(MEASURE_SAFETY_MARGIN /* remove first and last 100 elements */);
    timeProfiling.dump();

    return 0;
}
