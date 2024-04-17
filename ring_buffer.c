#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
// #include <fcntl.h>  // for open
// #include <unistd.h> // for close
// #include <sys/stat.h>
// #include <sys/mman.h>
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
    for(int i = 0; i < RING_SIZE; i++) {
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

    // buffer is full
    if (((r->c_head + 1) % RING_SIZE) == r->c_tail) {
        return;
    }
   
    // uint32_t next_tail, tail;
    // do
    // {
    //     //printf("ring_submit is locked!\n");
    //     tail = atomic_load(&r->c_tail);
    //     next_tail = (tail + 1) % RING_SIZE; // next is where tail will point to after this input.
    //     r->buffer[r->c_tail] = *bd;
    // } while (!atomic_compare_exchange_strong(&r->c_tail, &tail, next_tail));
    //printf("     r->buffer[r->c_tail:%u]\n", r->c_tail);
   
    pthread_mutex_lock(&mutex_tail);
    int next_tail = (atomic_load(&r->c_tail) + 1) % RING_SIZE;
    if (next_tail != atomic_load(&r->c_head)) {
        r->buffer[atomic_load(&r->c_tail)] = *bd;
        atomic_store(&r->c_tail, next_tail);
    } else {
       printf("RING is full, cannot submit.\n");
    }
    pthread_mutex_unlock(&mutex_tail);


    //
    //printf("ring_submit r->buffer[r->c_tail:%u] = k:%u\n", tail,r->buffer[r->c_tail].k );
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
    // buffer is empty
    //if(r->c_head == 0 && r->c_head == r->c_tail) return;
   
    // uint32_t head, next_head;
    // do
    // {
    //     // printf("ring_get is locked!\n");
    //     // sleep(0.0001);
    //     head = atomic_load(&r->c_head);
    //     //  if (head == atomic_load(&r->c_tail)) {
    //     //     printf("Buffer is empty, cannot get.\n");
    //     //     return; 
    //     // }
    //      *bd = r->buffer[head];
    //     next_head = (head + 1) % RING_SIZE; // next is where tail will point to after this input.
    // } while (!atomic_compare_exchange_strong(&r->c_head, &head, next_head));
    
   // printf("     r->buffer[r->c_head:%u]\n", r->c_head);
    

    pthread_mutex_lock(&mutex_head);
      if (atomic_load(&r->c_head) == atomic_load(&r->c_tail)) {
        //printf("RING is empty, cannot get.\n");
    } else {
        *bd = r->buffer[atomic_load(&r->c_head)];
        atomic_store(&r->c_head, (atomic_load(&r->c_head) + 1) % RING_SIZE);
    }
    pthread_mutex_unlock(&mutex_head);
    //printf("ring_get r->buffer[r->c_head:%u] = k:%u\n", head,r->buffer[r->c_head].k );
}
