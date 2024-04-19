#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ring_buffer.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>

#define MAX_THREADS 10
int verbose = 0;
/* prints "Client" before each line of output because the child will also be printing
 * to the same terminal */
#define PRINTV(...)         \
    if (verbose)            \
        printf("Server: "); \
    if (verbose)            \
    printf(__VA_ARGS__)

////////////////////////////////////////////
atomic_bool shutdown_flag = ATOMIC_VAR_INIT(false);

#include <signal.h>

void handle_signal(int sig)
{
    atomic_store(&shutdown_flag, true);
}

void setup_signal_handlers()
{
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

//////////////////////////////////////////////
char shm_file[] = "shmem_file";
char *shmem_area = NULL;
struct ring *ring = NULL;
pthread_t threads[MAX_THREADS];
int num_threads = 1;
uint32_t initial_table_size = 1000000;

struct thread_context
{
    int tid; /* thread ID */
};

typedef struct
{
    key_type k; // key is NULL if this slot is empty
    value_type v;
} kv_entry;

// Hash table structure: create with hash_table_create, free with hash_table_destroy.
typedef struct
{
    kv_entry *entries;      // hash slots
    pthread_mutex_t *mutex; // locks for individual buckets
    uint32_t size;          // number of items in hash table
    int num_locks;
} hash_table;

hash_table *table;

void hash_table_destroy(hash_table *table)
{
    // First free allocated keys.
    // for (size_t i = 0; i < initial_table_size; i++)
    // {
    //     free((void *)table->entries[i].k);
    // }
    // Then free entries array and table itself.
    free(table->entries);
    free(table);
}

// This function is used to insert a key-value pair into the store. If the key already exists,
// it updates the associated value.
void put(key_type k, value_type v)
{
    atomic_int index = hash_function(k, initial_table_size);

    pthread_mutex_lock(&table->mutex[index]);
    if (table->entries[index].k != 0)
    {
        int tmp = -1;
        if (table->entries[index].k != k)
        {
            int start = index;
            do
            {
                pthread_mutex_unlock(&table->mutex[index]);
                index = (index + 1) % initial_table_size;
                pthread_mutex_lock(&table->mutex[index]);
                if (tmp == -1 && table->entries[index].k == 0)
                {
                    tmp = index;
                }
            } while (table->entries[index].k != 0 && table->entries[index].k != k && index != start);
        }

        if (table->entries[index].k == 0)
        {
            pthread_mutex_unlock(&table->mutex[index]);
            index = tmp;
            pthread_mutex_lock(&table->mutex[index]);
        }
        table->entries[index].v = v;
    }
    else
    {  
        /*if (table->size >= initial_table_size) {
            printf("kv_store_capacity_reached= %d >= %d\n", table->size, initial_table_size);
        }*/
         
        table->entries[index].k = k;
        table->entries[index].v = v;
        // atomic_fetch_add(&table->size, 1);
    }
    pthread_mutex_unlock(&table->mutex[index]);
}

// This function is used to retrieve the value associated with a given key from the store.
// If the key is not found, it returns 0.
void get(key_type k, value_type *v)
{
     
    atomic_int index = hash_function(k, initial_table_size);

    pthread_mutex_lock(&table->mutex[index]);
    if (table->entries[index].k != 0)
    {

        if (table->entries[index].k != k)
        {
            do
            {
                pthread_mutex_unlock(&table->mutex[index]);
                index = (index + 1) % initial_table_size;
                pthread_mutex_lock(&table->mutex[index]);
            } while (table->entries[index].k != 0 && table->entries[index].k != k);
        }
    }

    if (table->entries[index].k != 0 && table->entries[index].k == k)
    {
        *v = table->entries[index].v;
    }
    else
    {
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
    while (1)
    {
        struct buffer_descriptor *bd = malloc(sizeof(struct buffer_descriptor));
        struct buffer_descriptor *result;
            ring_get(ring, bd);

        result = (struct buffer_descriptor *)(shmem_area + bd->res_off);
        memcpy(result, bd, sizeof(struct buffer_descriptor));
        if (result->req_type == PUT)
        {
            put(result->k, result->v);
        }
        else
        {
            get(result->k, &result->v);
        }
        result->ready = 1;
        free(bd);
    }
    return NULL;
}

static int parse_args(int argc, char **argv)
{
    int op;
    while ((op = getopt(argc, argv, "n:s:v")) != -1)
    {
        switch (op)
        {
        case 'n':
            num_threads = atoi(optarg);
            break;
        case 's':
            initial_table_size = atoi(optarg);
            break;
        case 'v':
            verbose = 1;
            break;

        default:
            printf("failed getting arg in main %d;\n", op);
            return 1;
        }
    }

    return 0;
}

// implements the server main() function with the following command line arguments:
// -n: number of server threads
// -s: the initial hashash_tableable size
int main(int argc, char *argv[])
{
    if (parse_args(argc, argv) != 0)
    {
        exit(1);
    }

    setup_signal_handlers(); // shutdown flag
    // Allocate space for hash table struct.
    table = malloc(sizeof(hash_table));
    if (table == NULL)
    {
        exit(1);
    }
    table->size = 0;
    // Allocate (zero'd) space for entry buckets.
    table->entries = calloc(initial_table_size, sizeof(kv_entry));
    if (table->entries == NULL)
    {
        free(table); // error, free table before we return!
        perror("error");
        exit(1);
    }

    table->mutex = calloc(initial_table_size, sizeof(pthread_mutex_t));
    if (table->mutex == NULL)
    {
        free(table); // error, free table before we return!
        perror("error");
        exit(1);
    }

    struct stat file_info;
    int fd = open(shm_file, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0)
    {
        perror("open");
    }

    if (fstat(fd, &file_info) == -1)
    {
        perror("open");
    }
    // points to the beginning of the shared memory region
    shmem_area = mmap(NULL, file_info.st_size - 1, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (shmem_area == (void *)-1)
    {
        perror("mmap");
    }
    /* mmap dups the fd, no longer needed */
    close(fd);
    ring = (struct ring *)shmem_area;

    // // start threads
    for (int i = 0; i < num_threads; i++)
    {
        // struct thread_context context;
        // context.tid = i;
        if (pthread_create(&threads[i], NULL, &thread_function, NULL))
        {
            perror("pthread_create");
        }
    }

    /// wait for threads
    for (int i = 0; i < num_threads; i++)
    {
        if (pthread_join(threads[i], NULL))
        {
            perror("pthread_join");
        }
    }

    hash_table_destroy(table);
}
