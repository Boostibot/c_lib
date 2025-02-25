#include "../spmc_queue.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
    #include <atomic>
    #define ATOMIC(T)    std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define ATOMIC(T)    _Atomic(T) 
#endif

typedef int64_t isize; 
typedef struct Thread_Cache_Thread Thread_Cache_Thread;

typedef struct Thread_Cache {
    const char* name;
    ATOMIC(Thread_Cache_Thread*) threads;
    ATOMIC(isize) threads_count;
    ATOMIC(uint64_t) threads_started;
    ATOMIC(uint64_t) threads_finished;
    
    isize max_capacity;
    void (*thread_init)(void* context);
    void (*thread_deinit)(void* context);
    void* thread_context;
} Thread_Cache;

typedef struct Thread_Cache_Thread {
    ATOMIC(Thread_Cache_Thread*) next;
    ATOMIC(Thread_Cache_Thread*) created_from;
    ATOMIC(uint64_t) lunch_id; //even if idle, odd if running

    Thread_Cache* cache;
    uint64_t stack_space;
    void* handle;
    isize data_capacity;
    isize data_size;
    void* data;
    char name[256];
} Thread_Cache_Thread;

void thread_cache_init(Thread_Cache* pool, const char* debug_name, isize max_capacity_or_negative, isize stack_space_or_negative);
void thread_cache_deinit(Thread_Cache* pool, uint32_t flags);
isize thread_cache_lunch_thread(Thread_Cache* pool, void (*thread_deinit)(void* context), const void* data, isize data_size);
Thread_Cache_Thread* thread_cache_self();
const char* thread_cache_self_name();

void* os_thread_start(size_t stack_space, void (*thread_deinit)(void* context), void* data);
void os_thread_join(void* thread);

//==========================================================

typedef struct Thread_Pool {
    int xxx;
} Thread_Pool;

void thread_pool_init(Thread_Pool* pool, int num_threads, void (*thread_init)(void* context), void* context);
void thread_pool_deinit(Thread_Pool* pool, void (*thread_deinit)(void* context), void* context);
void thread_pool_yield(Thread_Pool* pool);
isize thread_pool_lunch_ptr(Thread_Pool* pool, void (*func)(void* context), void* context);
isize thread_pool_lunch(Thread_Pool* pool, void (*func)(void* context), const void* data, isize data_size);


#ifdef __cplusplus
    #define _SPMC_QUEUE_USE_ATOMICS \
        using std::memory_order_acquire;\
        using std::memory_order_release;\
        using std::memory_order_seq_cst;\
        using std::memory_order_relaxed;\
        using std::memory_order_consume;
#else
    #define _SPMC_QUEUE_USE_ATOMICS
#endif

void thread_cache_init(Thread_Cache* pool, const char* debug_name, isize max_capacity_or_negative, isize stack_space_or_negative);
void thread_cache_deinit(Thread_Cache* pool, uint32_t flags);
isize thread_cache_lunch_thread(Thread_Cache* pool, void (*thread_deinit)(void* context), const void* data, isize data_size)
{
    _SPMC_QUEUE_USE_ATOMICS;
    Thread_Cache_Thread* self = NULL;
    Thread_Cache_Thread* found_thread = NULL;
    uint64_t backing_ids[64];

    uint64_t last_threads_started = 0; 
    uint64_t last_threads_finished = 0; 
    for(int repeat = 0; repeat < 2; repeat++)
    {
        uint64_t threads_started = pool->threads_started;
        uint64_t threads_finished = pool->threads_finished;
        isize threads_count = pool->threads_count;
        isize running = (isize) threads_started - (isize) threads_finished;
        
        ASSERT(threads_count >= 0);
        ASSERT(running >= 0);
        if(running < threads_count)
        {
            for(Thread_Cache_Thread* curr = first; curr; curr = curr->next) {
                uint64_t id = first->lunch_id;
                if(id % 2 == 0) {
                    if(atomic_compare_excahnge_strong(&first->lunch_id, &id, id + 1)) {
                        found_thread = curr;
                        goto outer_loop_end;
                    }
                }
            }
        }

        if(repeat == 0) {
            last_threads_started = threads_started;
            last_threads_finished = threads_finished;
        }
        //if something changes then go again
        else if(last_threads_started != threads_started 
                || threads_finished != last_threads_finished) {
            repeat = -1;
        }
    }
    outer_loop_end:

    //if didnt find created one
    if(found_thread == NULL) {
        Thread_Cache_Thread* thread = (Thread_Cache_Thread*) calloc(1, sizeof(Thread_Cache_Thread));
        thread->lunch_id = 1;
        // thread->handle = os_thread_start(pool->threa)
    }

}

Thread_Cache_Thread* thread_cache_self();
const char* thread_cache_self_name();
