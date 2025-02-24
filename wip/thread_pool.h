#include "../spmc_queue.h"


typedef struct Thread_Pool {
    int xxx;
} Thread_Pool;

typedef enum {
    THREAD_POOL_IDLE,
    THREAD_POOL_RUNNING,
    THREAD_POOL_BLOCKING,
    THREAD_POOL_REMOVED,
} Thread_Pool_State;

void thread_pool_init(Thread_Pool* pool, int num_threads);
void thread_pool_add_thread(Thread_Pool* pool);

void thread_pool_add_job(Thread_Pool* pool);
void thread_pool_yield(Thread_Pool* pool);

void thread_pool_block_begin(Thread_Pool* pool);
void thread_pool_block_end(Thread_Pool* pool);
