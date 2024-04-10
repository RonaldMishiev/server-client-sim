#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
/*
 * Initialize the ring
 * @param r A pointer to the ring
 * @return 0 on success, negative otherwise - this negative value will be
 * printed to output by the client program
 */
int init_ring(struct ring *r)
{
    struct buffer_descriptor temp;
    // ring_submit(r, &temp);
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
    uint32_t tail = atomic_load(&r->c_tail);
    uint32_t next_tail = tail + 1; // next is where tail will point to after this input.
    while (!atomic_compare_exchange_strong(&r->c_tail, &tail, next_tail))
    {
        tail = atomic_load(&r->c_tail);
        next_tail = tail + 1;
    }
    r->buffer[tail] = *bd;
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
    uint32_t head = atomic_load(&r->c_head);
    uint32_t next_head = head + 1; // next is where tail will point to after this input.
    while (!atomic_compare_exchange_strong(&r->c_head, &head, next_head))
    {
        head = atomic_load(&r->c_head);
        next_head = head + 1;
    }
    *bd = r->buffer[head];
}
