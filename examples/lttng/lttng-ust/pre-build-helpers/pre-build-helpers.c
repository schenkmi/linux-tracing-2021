/**
 * Example for doing call trace with liblttng-ust-cyg-profile.so
 * build with:
 * gcc -g -O2 -finstrument-functions -pthread pre-build-helpers.c -o pre-build-helpers
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

pthread_t tid[2];
int counter;
pthread_mutex_t lock;

int fibonacci(int i)
{
    if (i == 0) {
        return 0;
    }

    if (i == 1) {
        return 1;
    }

    return fibonacci(i-1) + fibonacci(i-2);
}

void* do_some_thing(void *arg)
{
    pthread_mutex_lock(&lock);

    unsigned long i = 0;
    counter += 1;
    printf("\n Job %d started\n", counter);

    for (i=0; i<(0xFFFFFFFF);i++);

    printf("\n Job %d finished\n", counter);

    pthread_mutex_unlock(&lock);

    /* hang around for seconds */
    struct timespec req = {
            .tv_sec = 5,
            .tv_nsec = 0
    };
    while (nanosleep(&req, &req) && (errno == EINTR));

    fibonacci(10);

    pthread_mutex_lock(&lock);

    void* memptrs[10];

    for (i = 0; i < 10; i++) {
        memptrs[i] = malloc(64);
    }

    pthread_mutex_unlock(&lock);

    for (i = 0; i < 10; i++) {
        free(memptrs[i]);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int i = 0;
    int err;

    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("mutex init failed\n");
        return 1;
    }

    while (i < 2) {
        if ((err = pthread_create(&(tid[i]), NULL, &do_some_thing, NULL)) != 0) {
            fprintf(stderr, "can't create thread :[%s]", strerror(err));
        }
        i++;
    }

    pthread_join(tid[0], NULL);
    pthread_join(tid[1], NULL);
    pthread_mutex_destroy(&lock);

    return 0;
}
