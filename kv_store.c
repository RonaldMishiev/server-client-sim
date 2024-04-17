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
#define PRINTV(...)         \
    if (verbose)            \
        printf("Server: "); \
    if (verbose)            \
    printf(__VA_ARGS__)
char shm_file[] = "shmem_file";
struct stat file_info;
struct ring *ring = NULL;
pthread_t threads[MAX_THREADS];
char *shared_mem_start;
int verbose = 0;
int num_threads = 1;
uint32_t s_init_table_size = 1000000;

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
    ht_entry *entries;      // hash slots
    pthread_mutex_t *mutex; // locks for individual buckets
    uint32_t length;        // number of items in hash table
} ht;

ht *table;

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
    tb->mutex = calloc(s_init_table_size, sizeof(pthread_mutex_t));
    if (tb->mutex == NULL)
    {
        free(tb); // error, free table before we return!
        perror("error");
        return NULL;
    }

    return tb;
}
void print_kv_store()
{
    printf("------------------------\n");
    for (int i = 0; i < table->length; i++)
    {
        ht_entry *current = &table->entries[i];
        while (current != NULL)
        {
            printf("Key: %u, Value: %u\n", *current->k, current->v);
        }
    }
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
    if (k == 0) {
        return; // todo: may be find better way to filter out empty requsts
    }
    atomic_uint index = hash_function(k, s_init_table_size);
        pthread_mutex_lock(&table->mutex[index]);
    if (atomic_load(&table->entries[index].k) != NULL) 
    {
        // //Lock the mutex for the specific bucket
         PRINTV("PUT(UPDATE) *table->entries[%u].k:%u = %u len(%d)\n\n", index, k, v, table->length);
        if (atomic_load(table->entries[index].k) != k) {
            PRINTV("collision *table->entries[%u].k = %u, k=%u\n", index, *table->entries[index].k, k);
        }
        // Key found, update value
        table->entries[index].v = v;
    }
    else
    {
        if (atomic_load(&table->length) >= s_init_table_size) {
            printf("kv_store_capacity_reached= %d >= %d\n", table->length, s_init_table_size);
        }
        // Key not found, insert new key-value pair
        PRINTV("PUT(ADD) *table->entries[%u].k:%u = %u len(%d)\n\n", index, k, v, table->length);
        table->entries[index].k = &k;
        table->entries[index].v = atomic_load(&v);
        atomic_fetch_add(&table->length, 1);
    }
     pthread_mutex_unlock(&table->mutex[index]);
}

// This function is used to retrieve the value associated with a given key from the store.
// If the key is not found, it returns 0.
void get(key_type k, value_type *v)
{
    // if (k == 0)
    //     return; // todo: may be find better way to filter out empty requsts
    atomic_uint index = hash_function(k, s_init_table_size);
    pthread_mutex_lock(&table->mutex[index]);
    if (atomic_load(&table->entries[index].k) != NULL) 
    {
        *v = table->entries[index].v;
    }
    else {
        *v = 0;
    }
    pthread_mutex_unlock(&table->mutex[index]);
}

/*
 * Function that's run by each thread
 * @param arg context for this thread
 */
void *thread_function(void *arg)
{
   
    // struct thread_context *ctx = arg;
    // PRINTV("Num reqs is %d, threadID is %d\n", ctx->num_reqs, ctx->tid);
    sleep(1);
    while (true)
    {
         struct buffer_descriptor *bd;
         bd = malloc(sizeof(struct buffer_descriptor));
         int cnt = 0;
         struct buffer_descriptor *result;
        do
        {
            
            ring_get(ring, bd);
            
           //PRINTV("-----ring_get looping -----%d\n",cnt++);
        } while (bd->k == 0);
       //PRINTV("-----ring_get(%d) - %u \n",bd.req_type, bd.k);
        result = (struct buffer_descriptor *)(shared_mem_start + bd->res_off);
        memcpy(result, bd, sizeof(struct buffer_descriptor));
       result->ready = 1;
        if (result->req_type == PUT)
        {
            put(result->k, result->v);
        }
        else
        {
            get(result->k, &result->v);
        }
        free(bd);
        // PRINTV("READY(%d) %u %u\n",result->req_type, result->k, result->v);
    }
}

// implements the server main() function with the following command line arguments:
// -n: number of server threads
// -s: the initial hashtable size
int main(int argc, char *argv[])
{
    int op;
    while ((op = getopt(argc, argv, "n:t:s:v")) != -1)
    {
        switch (op)
        {
        case 'n':
            num_threads = atoi(optarg);
            break;

        case 't':
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
    if (fd < 0) {
        perror("open");
    } else if (fstat(fd, &file_info) == -1)
    {
        perror("open");
    }
    // points to the beginning of the shared memory region
    shared_mem_start = mmap(NULL, file_info.st_size - 1, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (shared_mem_start == (void *)-1) {
        perror("mmap");
    }
    /* mmap dups the fd, no longer needed */
    close(fd);
    ring = (struct ring *)shared_mem_start;

    // // start threads
    for (int i = 0; i < num_threads; i++)
    {
        struct thread_context context;
        context.tid = i;
        // context.num_reqs = reqs_per_th;
        // context.reqs = r;
        if (pthread_create(&threads[i], NULL, &thread_function, &context)) {
            perror("pthread_create");
        }
        // r += reqs_per_th;
    }

    /// wait for threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL)) {
            perror("pthread_join");
        }
    }
    ht_destroy(table);
}
