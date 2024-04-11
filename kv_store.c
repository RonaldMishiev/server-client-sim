/// todo
// fix ht_destroy
// utilize hashes
// implement ring_init in ring_buffer.h

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ring_buffer.h"
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>  // for open
#include <unistd.h> // for close
#include <stdatomic.h>

#define MAX_THREADS 128

char shm_file[] = "shmem_file";
struct stat file_info;
struct ring *ring = NULL;
pthread_t threads[MAX_THREADS];
char *shared_mem_start;
int verbose = 0;
int num_threads = 1;
struct thread_context
{
    int tid;                        /* thread ID */
    int num_reqs;                   /* # of requests that this thread is responsible for */
    struct buffer_descriptor *reqs; /* Corresponding result for each request in reqs */
};

// Hash table entry (slot may be filled or empty).
typedef struct
{
    const key_type *k; // key is NULL if this slot is empty
    value_type v;
} ht_entry;

// Hash table structure: create with ht_create, free with ht_destroy.
typedef struct
{
    ht_entry *entries; // hash slots
    uint32_t length;   // number of items in hash table
} ht;

uint32_t s_init_table_size = 0;
ht *table;

void print_ring()
{
    printf("---------print_ring ---------------------\n");
    for (int i = ring->c_head; i < ring->c_tail + 1; i++)
    {
        printf("  buffer(%u) = %u->%u\n", i, ring->buffer[i].k, ring->buffer[i].v);
    }
    printf("-----------------------------------------\n");
}

ht *ht_create(void)
{
    // Allocate space for hash table struct.
    ht *tb = malloc(sizeof(ht));
    if (tb == NULL)
    {
        return NULL;
    }
    tb->length = 0;

    // Allocate (zero'd) space for entry buckets.
    tb->entries = calloc(s_init_table_size, sizeof(ht_entry));
    if (tb->entries == NULL)
    {
        free(tb); // error, free table before we return!
        perror("error");
        return NULL;
    }
    return tb;
}

void ht_destroy(ht *table)
{
    // First free allocated keys.
    for (size_t i = 0; i < s_init_table_size; i++)
    {
        free((void *)table->entries[i].k);
    }
    // Then free entries array and table itself.
    free(table->entries);
    free(table);
}

// This function is used to insert a key-value pair into the store. If the key already exists,
// it updates the associated value.
void put(key_type k, value_type v)
{
    int length = atomic_load(&table->length);
    int i;
    for (i = 0; i < length; ++i)
    {
        if (k == *(table->entries[i].k))
        {
            // Key found, update value
            table->entries[i].v = v;
            return;
        }
    }
    // Key not found, insert new key-value pair
    table->entries[length].k = &k;
    table->entries[length].v = v;
    atomic_fetch_add(&table->length, 1);
}

// This function is used to retrieve the value associated with a given key from the store.
// If the key is not found, it returns 0.
// The server should be able to fetch requests from the Ring Buffer, and update the
// Request-status Board after completing the requests. We expect the server to be faster
// with an increase in the number of threads.
value_type get(key_type k)
{
    int length = atomic_load(&table->length);
    for (int i = 0; i < length; ++i)
    {
        if (k == *(table->entries[i].k))
        {
            return table->entries[i].v;
        }
    }
    return 0;
}

/*
 * Function that's run by each thread
 * @param arg context for this thread
 */
void *thread_function(void *arg)
{
    struct thread_context *ctx = arg;
    printf(" ctx->num_reqs = %d\n", ctx->num_reqs);

    /* Keep submitting the requests and processing the completions */
    // for (int i = 0; i < ctx->num_reqs; i++)

    // {
    struct buffer_descriptor bd; //= ctx->reqs[i];

    ring_get(ring, &bd);
    struct buffer_descriptor *result = (struct buffer_descriptor *)(shared_mem_start + bd.res_off);

    memcpy(result, &bd, sizeof(struct buffer_descriptor));

    if (result->req_type == PUT)
    {
        put(result->k, result->v);
    }
    else
    {
        result->v = get(result->k);
    }
    result->ready = 1;
    printf("--ring_get(k:%u,v:%u)\n", bd.k, bd.v);
    printf("-- result(k:%u,v:%u)\n\n\n", result->k, result->v);
    // }
}

// implements the server main() function with the following command line arguments:
// -n: number of server threads
// -s: the initial hashtable size
int main(int argc, char *argv[])
{
    int op;

    while ((op = getopt(argc, argv, "n:s:v")) != -1)
    {
        switch (op)
        {
        case 'n':
            num_threads = atoi(optarg);
            break;

        case 'v':
            verbose = 1;
            break;

        case 's':
            s_init_table_size = atoi(optarg);
            break;

        default:
            printf("failed getting arg in main %d;\n", op);
            return 1;
        }
    }

    table = ht_create();
    int fd = open(shm_file, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0)
        perror("open");
    else if (fstat(fd, &file_info) == -1)
    {
        perror("open");
    }
    // points to the beginning of the shared memory region
    shared_mem_start = mmap(NULL, file_info.st_size - 1, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (shared_mem_start == (void *)-1)
        perror("mmap");

    /* mmap dups the fd, no longer needed */
    close(fd);
    ring = (struct ring *)shared_mem_start;
   while(true) {

    int reqs_per_th = (ring->c_tail - ring->c_head + 1) / num_threads;
    struct buffer_descriptor *r = ring->buffer;
    // start threads
    for (int i = 0; i < num_threads; i++)
    {
        struct thread_context context;
        context.tid = i;
        context.num_reqs = reqs_per_th;
        context.reqs = r;
        if (pthread_create(&threads[i], NULL, &thread_function, &context))
            perror("pthread_create");
        r += reqs_per_th;
    }

    /// wait for threads
    for (int i = 0; i < num_threads; i++)
        if (pthread_join(threads[i], NULL))
            perror("pthread_join");
    while (ring->c_tail == ring->c_head)
        sleep(0);
    } 
    // need to figure out why below call is failing with seg fault
    // ht_destroy(table);
}
