/**
 * build with g++ -g -O2 -o mq-perf-xmit mq-perf-xmit.cpp -pthread -lrt
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

#define SHMEM_NAME              "gugus"
#define SHMEM_MAX_MESSAGES      100
#define UDS_FILE                "/tmp/sock.uds"
#define QUEUE_NAME              "/mq-perf"
#define QUEUE_PERMISSIONS       0660
#define MAX_MESSAGES            10
#define MAX_MSG_SIZE            4096
#define MSG_BUFFER_SIZE         MAX_MSG_SIZE + 10
#define MSG_SEND_SIZE           256                 /* we send 256 bytes */
#define MSG_HDR_SIZE            (sizeof(int64_t) + sizeof(uint32_t))

#define MSG_SHMEM_SIZE          512 /* shared mem impl needs a fixed size so we choose 512 bytes */

#define IPC_METHOD_MQ           "mq"
#define IPC_METHOD_UDS          "uds"
#define IPC_METHOD_SHMEM        "shmem"
#define IPC_ENC_RAW             "raw"
#define PROGRAM 				"mq-perf-xmit"
#define PROGRAMVERSION 			"0.0.4"

static volatile int running = 1;
static int optTimeInterval = 6000;  /* 6ms                          */
static int optBurstCount = 0;       /* no burst                     */
static int optThreadPrio = 40;      /* fifo with prio 40            */
static char* optIPCMethod = nullptr;
static unsigned int optAffinityMask = 0;
static char* optEncapsulation = nullptr;
static uint32_t elementCounter = 0;
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
           "low level ipc latency test application (xmit part)\n"
           "\n"
           "example: " PROGRAM " --encapsulation=raw --prio=40 --time=6000 --burst=15\n"
           "\n"
           "  --help                              Show this menu\n"
           "  --version                           Show version of this application\n"
           "  -i, --ipc=[mq|uds|shmem]            Use MQ, Unix domain socket or shared memory as IPC\n"
           "  -m, --mask                          CPU affinity mask\n"
           "  -b, --burst                         Number of messages as burst (0 = no burst)\n"
           "  -t, --time                          Time interval between messages in micro seconds (0 = no wait)\n"
           "  -p, --prio                          Thread priority (FIFO scheduling)\n");
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
        static const char *short_options = "m:i:p:b:t:";

        static const struct option long_options[] = {
                { "help",          no_argument,       0,  0  },
                { "version",       no_argument,       0,  0  },
                { "ipc",           required_argument, 0, 'i' },
                { "mask",          required_argument, 0, 'm' },
                { "burst",         required_argument, 0, 'b' },
                { "time",          required_argument, 0, 't' },
                { "prio",          required_argument, 0, 'p' },
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
            case 't':
                optTimeInterval = atoi(optarg);
                break;
            case 'p':
                optThreadPrio = atoi(optarg);
                break;
            case 'i':
                optIPCMethod = strdup(optarg);
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
        *size = MSG_SEND_SIZE + MSG_HDR_SIZE;
        *buffer = (char*)std::malloc(*size);
    }

    std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
    *(int64_t*)&(*buffer)[0] = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    *(uint32_t*)&(*buffer)[sizeof(int64_t)] = elementCounter;
}

static void release_message_0(char** buffer, ssize_t* size)
{
    buffer = buffer;
    size = size;
}
/* end of plain, no protobuf */

void xmit_func(mqd_t mq_descriptor)
{
    char* xmit_buffer = nullptr;
    ssize_t xmit_size = 0;
    int burstCnt = optBurstCount;
    struct timespec tm;

    std::cout << "start sending mq with interval [" << optTimeInterval << "] burst [" <<
              burstCnt << "] prio [" << optThreadPrio << "]" << std::endl;

    pthread_setname_np(pthread_self(), "mq_xmit");

    struct sched_param param = { .sched_priority = optThreadPrio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    configure_cpu_affinity();

    while (running) {
        if ((burstCnt == 0) && (optTimeInterval > 0)) {
            std::this_thread::sleep_for(std::chrono::microseconds(optTimeInterval));
            burstCnt = optBurstCount;
        }

        burstCnt > 0 ? --burstCnt : burstCnt;

        elementCounter++;

        clock_gettime(CLOCK_REALTIME, &tm);
        tm.tv_sec += 1;

        aquireFunc(&xmit_buffer, &xmit_size);

        mq_timedsend(mq_descriptor, xmit_buffer, xmit_size, 0, &tm);

        releaseFunc(&xmit_buffer, &xmit_size);
    }

    if (xmit_buffer) {
        std::free(xmit_buffer);
    }
}

void xmit_uds_func(int sockfd)
{
    char* xmit_buffer = nullptr;
    ssize_t xmit_size = 0;
    int burstCnt = optBurstCount;
    struct sockaddr_un uds_addr;

    bzero(&uds_addr, sizeof(uds_addr));
    uds_addr.sun_family = AF_LOCAL;
    strcpy(uds_addr.sun_path, UDS_FILE);

    std::cout << "start sending UDS with interval [" << optTimeInterval << "] burst [" <<
                 burstCnt << "] prio [" << optThreadPrio << "]" << std::endl;

    pthread_setname_np(pthread_self(), "uds_xmit");

    struct sched_param param = { .sched_priority = optThreadPrio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    configure_cpu_affinity();

    while (running) {
        if ((burstCnt == 0) && (optTimeInterval > 0)) {
            std::this_thread::sleep_for(std::chrono::microseconds(optTimeInterval));
            burstCnt = optBurstCount;
        }

        burstCnt > 0 ? --burstCnt : burstCnt;

        elementCounter++;

        aquireFunc(&xmit_buffer, &xmit_size);

        sendto(sockfd, xmit_buffer, xmit_size, 0, (struct sockaddr *) &uds_addr, sizeof(uds_addr));

        releaseFunc(&xmit_buffer, &xmit_size);
    }

    if (xmit_buffer) {
        std::free(xmit_buffer);
    }
}

void xmit_shmem_func(shmemq_t* shmemq)
{
    char* xmit_buffer = nullptr;
    ssize_t xmit_size = 0;
    int burstCnt = optBurstCount;

    std::cout << "start sending shmem with interval [" << optTimeInterval << "] burst [" <<
              burstCnt << "] prio [" << optThreadPrio << "]" << std::endl;

    pthread_setname_np(pthread_self(), "shmem_xmit");

    struct sched_param param = { .sched_priority = optThreadPrio };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    configure_cpu_affinity();

    while (running) {
        if ((burstCnt == 0) && (optTimeInterval > 0)) {
            std::this_thread::sleep_for(std::chrono::microseconds(optTimeInterval));
            burstCnt = optBurstCount;
        }

        burstCnt > 0 ? --burstCnt : burstCnt;

        elementCounter++;

        aquireFunc(&xmit_buffer, &xmit_size);

        while (!shmemq_try_enqueue_sema(shmemq, xmit_buffer, xmit_size)) {
            if (!running)
                break;
        }

        releaseFunc(&xmit_buffer, &xmit_size);
    }

    if (xmit_buffer) {
        std::free(xmit_buffer);
    }
}

int main(int argc, char **argv)
{
    char ch;
    std::thread xmit_thread;
    mqd_t mq_descriptor;   // queue descriptors
    struct mq_attr attr = { .mq_flags = 0,
                            .mq_maxmsg = MAX_MESSAGES,
                            .mq_msgsize = MAX_MSG_SIZE,
                            .mq_curmsgs = 0 };
    int sockfd;
    shmemq_t* shmemq = nullptr;

    /* parse given cmd line args */
    process_options(argc, argv);

    optEncapsulation = optEncapsulation ? optEncapsulation : strdup(IPC_ENC_RAW);

    aquireFunc = std::function<void(char**, ssize_t*)>(aquire_message_0);
    releaseFunc = std::function<void(char**, ssize_t*)>(release_message_0);

    optIPCMethod = optIPCMethod ? optIPCMethod : strdup(IPC_METHOD_MQ);

    if (strncmp(optIPCMethod, IPC_METHOD_UDS, strlen(IPC_METHOD_UDS)) == 0) {
        if ((sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
            perror("socket() failed");
        }

        xmit_thread = std::thread(xmit_uds_func, sockfd);
    }
    else if (strncmp(optIPCMethod, IPC_METHOD_SHMEM, strlen(IPC_METHOD_SHMEM)) == 0) {
        /* as we have a queue of fixed elements size must match */
        shmemq = shmemq_new(SHMEM_NAME, SHMEM_MAX_MESSAGES, (MSG_SEND_SIZE + MSG_HDR_SIZE));

        xmit_thread = std::thread(xmit_shmem_func, shmemq);
    }
    else {
        if ((mq_descriptor = mq_open(QUEUE_NAME, O_WRONLY /*| O_CREAT*/, QUEUE_PERMISSIONS, &attr)) == -1) {
            perror ("Server: mq_open (server)");
            exit (1);
        }

        xmit_thread = std::thread(xmit_func, mq_descriptor);
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

    if (xmit_thread.joinable()) {
        xmit_thread.join();
    }

    if (strncmp(optIPCMethod, IPC_METHOD_UDS, strlen(IPC_METHOD_UDS)) == 0) {
        close(sockfd);
    }
    else if (strncmp(optIPCMethod, IPC_METHOD_SHMEM, strlen(IPC_METHOD_SHMEM)) == 0) {
        if (shmemq) {
            shmemq_destroy(shmemq, 1 /* unlink */);
            shmemq = nullptr;
        }
    }
    else {
        mq_close(mq_descriptor);
        /* unlink done in receiver */
        //mq_unlink(QUEUE_NAME);
    }

    if (optEncapsulation) {
        free(optEncapsulation);
    }

    if (optIPCMethod) {
        free(optIPCMethod);
    }

    return 0;
}
