#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>

#include <semaphore.h>
#include <fcntl.h>

#define SNAME_EMPTY "/mysem_empty1"
#define SNAME_FULL "/mysem_full"
#define SNAME_SUBMIT "/mysem_submit"
#define SNAME_GET "/mysem_get"


pthread_mutex_t mutex[RING_SIZE];  //attempted to lock individual cell in the ring - in attempt (unsuccesful) to prevent from simutaneous reader and writing of same value
void sem_reset(sem_t *sem, int INITIAL_VAL)
{
    int value;
    sem_getvalue(sem, &value);
    while (value > INITIAL_VAL)
    {
        sem_wait(sem);
        sem_getvalue(sem, &value);
    }
    while (value < INITIAL_VAL)
    {
        sem_post(sem);
        sem_getvalue(sem, &value);
    }
}
/*
 * Initialize the ring
 * @param r A pointer to the ring
 * @return 0 on success, negative otherwise - this negative value will be
 * printed to output by the client program
 */
int init_ring(struct ring *r)
{
    for (int i = 0; i < RING_SIZE; i++)
    {
        r->buffer[i].k = 0;
        r->buffer[i].v = 0;
    }
    sem_init(&r->empty, 1, RING_SIZE); // Initialize semaphore with initial value 1
    sem_init(&r->full, 1, 0);
    sem_init(&r->mutex_submit, 1, 1);
    sem_init(&r->mutex_get, 1, 1);
    return 0;
}

void ring_submit(struct ring *r, struct buffer_descriptor *bd)
{

    //printf("submit___________sem_wait(&empty);\n");
    sem_wait(&r->empty);
    //printf("submit___________sem_wait(&mutex_submit);\n");
    sem_wait(&r->mutex_submit);
     
    int next_tail = (atomic_load(&r->c_tail) + 1) % RING_SIZE;

    //pthread_mutex_lock(&mutex[next_tail]);
    r->c_tail = next_tail;
    r->buffer[r->c_tail] = *bd;

    //pthread_mutex_unlock(&mutex[next_tail]);
   // printf("submit___________sem_post(&mutex_submit);\n");
    sem_post(&r->mutex_submit);
    //printf("submit___________sem_post(&full);\n");
    sem_post(&r->full);
}
int reloaded = 0;
void ring_get(struct ring *r, struct buffer_descriptor *bd)
{
    //printf("get___________sem_wait(&full); - head:%u <= tail:%u\n", r->c_head, r->c_tail);
    sem_wait(&r->full);
    //printf("get__________sem_wait(&mutex_get);\n");
    sem_wait(&r->mutex_get);
 
     int next_head =(atomic_load(&r->c_head) + 1) % RING_SIZE;
    //pthread_mutex_lock(&mutex[next_head]);
    r->c_head = next_head;
    *bd = r->buffer[r->c_head];
    //pthread_mutex_unlock(&mutex[next_head]);
    //printf("get__________sem_post(&mutex_get);;\n");
    sem_post(&r->mutex_get);
    //printf("get__________sem_post(&empty);;\n");
    sem_post(&r->empty);
}
