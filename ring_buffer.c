#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
pthread_mutex_t mutex_head;
pthread_mutex_t mutex_tail;

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
    return 0;
}

/*
 * Submit a new item - should be thread-safe
 * This call will block the calling thread if there's not enough space
 * @param r The shared ring
 * @param bd A pointer to a valid buffer_descriptor - This pointer is only
 * guaranteed to be valid during the invocation of the function
 */
void ring_submit(struct ring *r, struct buffer_descriptor *bd)
{

    // uint32_t next_tail, tail;
    // do
    // {
    //     next_tail = (atomic_load(&r->c_tail) + 1) % RING_SIZE; // set next tail
    //     tail = atomic_load(&r->c_tail);                        // save current tail
    //     while (next_tail == atomic_load(&r->c_head))           // chec if buffer full
    //     {
    //         printf("Buffer is full, cannot submit.\n");
    //         sched_yield();
    //     }
    // } while (!atomic_compare_exchange_strong(&r->c_tail, &tail, next_tail)); // if current tail is still current - set to next tail
    // r->buffer[atomic_load(&r->c_tail)] = *bd; // return value from current tail

    //this lock still not heling with multithreaded client
        pthread_mutex_lock(&mutex_tail);
        int next_tail = (atomic_load(&r->c_tail) + 1) % RING_SIZE;
        if (next_tail != atomic_load(&r->c_head)) {
            r->c_tail=  atomic_load(&next_tail);
            r->buffer[atomic_load(&r->c_tail)] = *bd;     
        } else {
           printf("RING is full, cannot submit.\n");
           sched_yield();
        }
       pthread_mutex_unlock(&mutex_tail);
}

/*
 * Get an item from the ring - should be thread-safe
 * This call will block the calling thread if the ring is empty
 * @param r A pointer to the shared ring
 * @param bd pointer to a valid buffer_descriptor to copy the data to
 * Note: This function is not used in the clinet program, so you can change
 * the signature.
 */
void ring_get(struct ring *r, struct buffer_descriptor *bd)
{
    uint32_t head, next_head;
    do
    {
        if (atomic_load(&r->c_head) == atomic_load(&r->c_tail)) // check if buffer empty
        {
            //printf("Buffer is empty, cannot get. head=tail=%u\n", atomic_load(&r->c_head));
            sched_yield();
            return;
        }
        head = atomic_load(&r->c_head);     // save current head
        next_head = (head + 1) % RING_SIZE; // set next head
        // printf("ring_get. head=%u\n", atomic_load(&next_head));
    } while (!atomic_compare_exchange_strong(&r->c_head, &head, next_head)); // if current head is still current - set to next head
    *bd = r->buffer[atomic_load(&r->c_head)]; // return valud from new head

    //lock to handle multithread servers
    //  pthread_mutex_lock(&mutex_head);
    //  if (atomic_load(&r->c_head) == atomic_load(&r->c_tail))
    //  {
    //      //printf("RING is empty, cannot get. (when tail==head==%u)\n", atomic_load(&r->c_tail));
    //      sched_yield();    
    //  }
    //  else
    //  {
    //     r->c_head =  (atomic_load(&r->c_head) + 1) % RING_SIZE;
    //      *bd = r->buffer[atomic_load(&r->c_head)];
    //  }
    //   pthread_mutex_unlock(&mutex_head);
}
