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

sem_t *empty, *full, *mutex_submit, *mutex_get;
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

    empty = sem_open(SNAME_EMPTY, O_CREAT, 0644, RING_SIZE); //sem innit
    sem_reset(empty, RING_SIZE);
    if (empty == SEM_FAILED)
    {
        perror("Failed to open semphore for empty");
        exit(-1);
    }
    full = sem_open(SNAME_FULL, O_CREAT, 0644, 0);
    sem_reset(full, 0);
    if (full == SEM_FAILED)
    {
        perror("Failed to open semphore for full");
        exit(-1);
    }
    mutex_submit = sem_open(SNAME_SUBMIT, O_CREAT, 0644, 1);
    sem_reset(mutex_submit, 1);
    if (mutex_submit == SEM_FAILED)
    {
        perror("Failed to open semphore for mutex_submit");
        exit(-1);
    }
    mutex_get = sem_open(SNAME_GET, O_CREAT, 0644, 1);
    sem_reset(mutex_get, 1);
    if (mutex_get == SEM_FAILED)
    {
        perror("Failed to open semphore for mutex_get");
        exit(-1);
    }
    return 0;
}

int re_init()
{
    empty = sem_open(SNAME_EMPTY, RING_SIZE);
    if (empty == SEM_FAILED)
    {
        perror("Failed to open semphore for empty");
        exit(-1);
    }
    full = sem_open(SNAME_FULL, 0);
    if (full == SEM_FAILED)
    {
        perror("Failed to open semphore for full");
        exit(-1);
    }
    mutex_submit = sem_open(SNAME_SUBMIT, 1); // get in server only cares about its mutex lock
    if (mutex_submit == SEM_FAILED)
    {
        perror("Failed to open semphore for mutex_submit");
        exit(-1);
    }
    mutex_get = sem_open(SNAME_GET, 1);
    if (mutex_get == SEM_FAILED)
    {
        perror("Failed to open semphore for mutex_get");
        exit(-1);
        return 0;
    }
}

void ring_submit(struct ring *r, struct buffer_descriptor *bd)
{
     re_init();
    //printf("submit___________sem_wait(&empty);\n");
    sem_wait(empty);
    //printf("submit___________sem_wait(&mutex_submit);\n");
    sem_wait(mutex_submit);
    int next_tail = (r->c_tail + 1) % RING_SIZE;
    // if (next_tail == r->c_head)
    // {
    //     printf("RING is full, cannot submit. - head:%u == tail:%u\n", r->c_head, r->c_tail);
    //     printf("submit___________sem_post(&full);\n");
    //     sem_post(mutex_submit);
    //     sem_post(full);
    // }
    r->c_tail = next_tail;
    r->buffer[r->c_tail] = *bd;
   // printf("submit___________sem_post(&mutex_submit);\n");
    sem_post(mutex_submit);
    //printf("submit___________sem_post(&full);\n");
    sem_post(full);
}

void ring_get(struct ring *r, struct buffer_descriptor *bd)
{
    re_init();

    //printf("get___________sem_wait(&full); - head:%u <= tail:%u\n", r->c_head, r->c_tail);
    sem_wait(full);
    //printf("get__________sem_wait(&mutex_get);\n");
    sem_wait(mutex_get);
    // if (r->c_head == r->c_tail)
    // {
    //     printf("RING is empty, cannot get. (when tail==head==%u)\n", r->c_tail);
    //     printf("get__________sem_post(&empty);\n");
    //     sem_post(mutex_get);
    //     sem_post(empty);
    // }
    r->c_head = (r->c_head + 1) % RING_SIZE;
    *bd = r->buffer[r->c_head];
    //printf("get__________sem_post(&mutex_get);;\n");
    sem_post(mutex_get);
    //printf("get__________sem_post(&empty);;\n");
    sem_post(empty);
}
