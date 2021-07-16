#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#include "shmemq.h"

struct shmemq_info {
    pthread_mutex_t lock;
    /* start of semaphore */
    int sema_count;
    pthread_cond_t sema_cond;		/* conditional var							*/
    /* stop of semaphore */
    unsigned long read_index;
    unsigned long write_index;
    char data[1];
};

struct _shmemq {
    unsigned long max_count;
    int element_size;
    unsigned long max_size;
    char* name;
    int shmem_fd;
    unsigned long mmap_size;
    struct shmemq_info* mem;
};

shmemq_t* shmemq_new(char const* name, unsigned long max_count, unsigned int element_size)
{
    shmemq_t* self;
    bool created;

    self = (shmemq_t*)malloc(sizeof(shmemq_t));
    assert(self != nullptr);

    memset(self, 0, sizeof(shmemq_t));
    self->max_count = max_count;
    self->element_size = element_size;
    self->max_size = max_count * element_size;
    self->name = strdup(name);
    self->mmap_size = self->max_size + sizeof(struct shmemq_info) - 1;

    created = false;
    self->shmem_fd = shm_open(name, O_RDWR, S_IRUSR | S_IWUSR);
    if (self->shmem_fd == -1) {
        if (errno == ENOENT) {
            self->shmem_fd = shm_open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            if (self->shmem_fd == -1) {
                goto FAIL;
            }
            created = true;
        }
        else {
            goto FAIL;
        }
    }

    //printf("initialized queue %s, created = %d\n", name, created);

    if (created && (-1 == ftruncate(self->shmem_fd, self->mmap_size))) {
        goto FAIL;
    }

    self->mem = (struct shmemq_info*)mmap(NULL, self->mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, self->shmem_fd, 0);
    if (self->mem == MAP_FAILED) {
        goto FAIL;
    }

    if (created) {
        self->mem->read_index = self->mem->write_index = 0;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
#if ADAPTIVE_MUTEX
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
        pthread_mutex_init(&self->mem->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        /* the semaphore by using a mutex and a condition */
        self->mem->sema_count = 1; /* locked */
        pthread_condattr_t cond_attr;
        pthread_condattr_init(&cond_attr);
        pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
        pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&self->mem->sema_cond, &cond_attr);
        pthread_condattr_destroy(&cond_attr);
         // TODO Need to clean up the mutex? Also, maybe mark it as robust? (pthread_mutexattr_setrobust)
    }

    return self;

FAIL:
    if (self->shmem_fd != -1) {
        close(self->shmem_fd);
        shm_unlink(self->name);
    }
    free(self->name);
    free(self);
    return NULL;
}

bool shmemq_try_enqueue(shmemq_t* self, void* element, int len)
{
    if (len != self->element_size) {
        return false;
    }

    pthread_mutex_lock(&self->mem->lock);

    // TODO this test needs to take overflow into account
    if (self->mem->write_index - self->mem->read_index >= self->max_size) {
        pthread_mutex_unlock(&self->mem->lock);
        return false; // There is no more room in the queue
    }

    memcpy(&self->mem->data[self->mem->write_index % self->max_size], element, len);
    self->mem->write_index += self->element_size;

    pthread_mutex_unlock(&self->mem->lock);

    return true;
}


bool shmemq_try_enqueue_sema(shmemq_t* self, void* element, int len)
{
    if (len != self->element_size) {
        return false;
    }

    pthread_mutex_lock(&self->mem->lock);

    // TODO this test needs to take overflow into account
    if (self->mem->write_index - self->mem->read_index >= self->max_size) {
        pthread_mutex_unlock(&self->mem->lock);
        return false; // There is no more room in the queue
    }

    memcpy(&self->mem->data[self->mem->write_index % self->max_size], element, len);
    self->mem->write_index += self->element_size;

    /**
     * order is important here see:
     * http://stackoverflow.com/questions/4544234/calling-pthread-cond-signal-without-locking-mutex
     * to prevent loss signals, call pthread_cond_signal with mutex held.
     */
    self->mem->sema_count++;
    pthread_cond_signal(&self->mem->sema_cond);
    pthread_mutex_unlock(&self->mem->lock);

    return true;
}

bool shmemq_dequeue(shmemq_t* self, void* element, int len)
{
    if (len != self->element_size) {
        return false;
    }

    pthread_mutex_lock(&self->mem->lock);

    self->mem->sema_count--;

    while (self->mem->sema_count <= 0) {
        pthread_cond_wait(&self->mem->sema_cond, &self->mem->lock);
    }

    if (self->mem->read_index >= self->mem->write_index) {
        pthread_mutex_unlock(&self->mem->lock);
        printf("empty should not happen !!!!!\n");
        return false; // There are no elements that haven't been consumed yet
    }

    // copy data
    memcpy(element, &self->mem->data[self->mem->read_index % self->max_size], len);
    self->mem->read_index += self->element_size;

    pthread_mutex_unlock(&self->mem->lock);

    return true;
}

bool shmemq_try_dequeue(shmemq_t* self, void* element, int len)
{
    if (len != self->element_size) {
        return false;
    }

    pthread_mutex_lock(&self->mem->lock);

    // TODO this test needs to take overflow into account
    if (self->mem->read_index >= self->mem->write_index) {
        pthread_mutex_unlock(&self->mem->lock);
        return false; // There are no elements that haven't been consumed yet
    }

    memcpy(element, &self->mem->data[self->mem->read_index % self->max_size], len);
    self->mem->read_index += self->element_size;

    pthread_mutex_unlock(&self->mem->lock);

    return true;
} 

void shmemq_destroy(shmemq_t* self, int unlink)
{
    if (unlink) {
        pthread_mutex_destroy(&self->mem->lock);
        pthread_cond_destroy(&self->mem->sema_cond);
    }

    munmap(self->mem, self->max_size);
    close(self->shmem_fd);

    if (unlink) {
        shm_unlink(self->name);
    }

    free(self->name);
    free(self);
}
