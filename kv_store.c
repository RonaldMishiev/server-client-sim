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
char *shmem_area = NULL;
struct ring *ring = NULL;
pthread_t threads[MAX_THREADS];  
int num_threads = 1;
uint32_t table_size = RING_SIZE;
int verbose;

#define PRINTV(...)         \
    if (verbose)            \
        printf("Server: "); \
    if (verbose)            \
    printf(__VA_ARGS__)

/////////////all the kill flag stuff/////////////////
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

struct thread_context
{
    int tid;
};

typedef struct pair
{
    key_type k;
    value_type v;
    struct pair *next;
} kv_pair;

typedef struct
{
    kv_pair *pairs;
    int size;
} kv_list;

typedef struct
{
    kv_list *entries;
    pthread_mutex_t *mutex;
} hash_table;

hash_table *table;

void put(key_type k, value_type v)
{
    // atomic_int index = hash_function(k, initial_table_size);
    // pthread_mutex_lock(&table->mutex[index]);
    // if (table->entries[index].k != 0)
    // {
    //     int tmp = -1;
    //     if (table->entries[index].k != k)
    //     {
    //         int start = index;
    //         do
    //         {
    //             pthread_mutex_unlock(&table->mutex[index]);
    //             index = (index + 1) % initial_table_size;
    //             pthread_mutex_lock(&table->mutex[index]);
    //             if (tmp == -1 && table->entries[index].k == 0)
    //             {
    //                 tmp = index;
    //             }
    //         } while (table->entries[index].k != 0 && table->entries[index].k != k && index != start);
    //     }
    //     if (table->entries[index].k == 0)
    //     {
    //         pthread_mutex_unlock(&table->mutex[index]);
    //         index = tmp;
    //         pthread_mutex_lock(&table->mutex[index]);
    //     }
    //     table->entries[index].v = v;
    // }
    // else
    // {  
         
    //     table->entries[index].k = k;
    //     table->entries[index].v = v;
    // }
    // pthread_mutex_unlock(&table->mutex[index]);

    int index = hash_function(k, table_size);
    pthread_mutex_lock(&table->mutex[index]);
    for (int i = 0; i < table->entries[index].size; i++)
    {
        if (table->entries[index].pairs[i].k == k)
        {
            table->entries[index].pairs[i].v = v;
            pthread_mutex_unlock(&table->mutex[index]);
            return;
        }
    }

    // didnt find, insert new key-value pair
    table->entries[index].pairs = realloc(table->entries[index].pairs, (table->entries[index].size + 1) * sizeof(kv_pair));
    table->entries[index].pairs[table->entries[index].size].k = k;
    table->entries[index].pairs[table->entries[index].size].v = v;
    atomic_fetch_add(&table->entries[index].size, 1);
    pthread_mutex_unlock(&table->mutex[index]);
}

void get(key_type k, key_type *v)
{
    // atomic_int index = hash_function(k, initial_table_size);
    // pthread_mutex_lock(&table->mutex[index]);
    // if (table->entries[index].k != 0)
    // {

    //     if (table->entries[index].k != k)
    //     {
    //         do
    //         {
    //             pthread_mutex_unlock(&table->mutex[index]);
    //             index = (index + 1) % initial_table_size;
    //             pthread_mutex_lock(&table->mutex[index]);
    //         } while (table->entries[index].k != 0 && table->entries[index].k != k);
    //     }
    // }
    // if (table->entries[index].k != 0 && table->entries[index].k == k)
    // {
    //     *v = table->entries[index].v;
    // }
    // else
    // {
    //     *v = 0;
    // }
    // pthread_mutex_unlock(&table->mutex[index]);

    int index = hash_function(k, table_size);
    pthread_mutex_lock(&table->mutex[index]);
    for (int i = 0; i < table->entries[index].size; i++)
    {
        if (table->entries[index].pairs[i].k == k)
        {
            *v = atomic_load(&table->entries[index].pairs[i].v);
            pthread_mutex_unlock(&table->mutex[index]);
            return;
        }
    }
    *v = 0;
    pthread_mutex_unlock(&table->mutex[index]);
}

void *thread_function(void *arg)
{
    struct buffer_descriptor bd;
    struct buffer_descriptor *result;
    while (1)
    {
        ring_get(ring, &bd);
        result = (struct buffer_descriptor *)(shmem_area + bd.res_off);
        memcpy(result, &bd, sizeof(struct buffer_descriptor));
        if (result->req_type == PUT)
        {
            put(result->k, result->v);
        }
        else
        {
            get(result->k,&result->v);
        }
        result->ready = 1;
    }
    return NULL;
}

static int parse_args(int argc, char **argv)
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
            table_size = atoi(optarg);
            break;
        default:
            printf("failed getting arg in main %c\n", op);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (parse_args(argc, argv) != 0)
    {
        exit(1);
    }

    // Alloc space for hash table
    table = malloc(sizeof(hash_table));
    if (table == NULL)
    {
        exit(1);
    }
    // Alloc space for entry buckets
    table->entries = calloc(table_size, sizeof(kv_list));
    if (table->entries == NULL)
    {
        free(table);
        perror("error");
        exit(1);
    }

    for (int i = 0; i < table_size; i++)
    {
        table->entries[i].size = 0;
        table->entries[i].pairs = NULL;
    }

    table->mutex = calloc(table_size, sizeof(pthread_mutex_t));
    if (table->mutex == NULL)
    {
        free(table); // error
        perror("error");
        exit(1);
    }

    char shm_file[] = "shmem_file";
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
    shmem_area = mmap(NULL, file_info.st_size - 1, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (shmem_area == (void *)-1)
    {
        perror("mmap");
    }
    // mmap dups the fd, no longer needed
    close(fd);
    ring = (struct ring *)shmem_area;

    // // start threads
    for (int i = 0; i < num_threads; i++)
    {
        struct thread_context context;
        context.tid = i;
        if (pthread_create(&threads[i], NULL, &thread_function, &context))
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

    // hash_table_destroy(table);
}

// void hash_table_destroy(hash_table *table)
// {
//     // First free allocated keys.
//     // for (size_t i = 0; i < initial_table_size; i++)
//     // {
//     //     free((void *)table->entries[i].k);
//     // }
//     // Then free entries array and table itself.
//     free(table->entries);
//     free(table);
// }
