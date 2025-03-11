#include "../assert.h"
#include "../platform.h"
#include <stdbool.h>
#include <stdarg.h>
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

typedef struct Thread_Cache_Config {
    isize preallocate_threads_or_negative;
    isize stack_space_or_negative;
    isize max_capacity_or_negative;
    void (*thread_init)(void* context);
    void (*thread_deinit)(void* context);
    void (*thread_before_func)(void* context);
    void (*thread_after_func)(void* context);
    void* thread_context;
};

typedef struct Thread_Cache {
    const char* name;
    ATOMIC(Thread_Cache_Thread*) threads;
    ATOMIC(uint64_t) threads_started;
    ATOMIC(uint64_t) threads_finished;
    ATOMIC(uint32_t) threads_init;
    ATOMIC(uint32_t) threads_deinit;
    ATOMIC(bool) is_closed;

    Thread_Cache_Config config;
} Thread_Cache;

typedef enum {
    THREAD_CACHE_IDLE,
    THREAD_CACHE_RUNNING,
    THREAD_CACHE_STARTING,
} Thread_Cache_State;

typedef struct Thread_Cache_Thread {
    ATOMIC(Thread_Cache_Thread*) next;
    ATOMIC(Thread_Cache_Thread*) created_from;
    ATOMIC(Thread_Cache_State) state;
    ATOMIC(uint64_t) lunch_id; //ticks up on every completion

    Thread_Cache* cache;
    void* handle;
    void (*func)(void* context);
    void* args;
    isize args_capacity;
    isize args_size;
    char name[256];
} Thread_Cache_Thread;

void thread_cache_init(Thread_Cache* cache, const char* debug_name, const Thread_Cache_Config* config_or_null);
void thread_cache_deinit(Thread_Cache* cache);
void thread_cache_lunch_thread(Thread_Cache* cache, void (*func)(void* context), const void* args, isize args_size, const char* thread_name_fmt, ...);
Thread_Cache_Thread* thread_cache_self();
const char*          thread_cache_self_name();

#ifdef __cplusplus
    #define _THREAD_CACHE_USE_ATOMICS using namespace std
#else
    #define _THREAD_CACHE_USE_ATOMICS
#endif

void thread_cache_init(Thread_Cache* cache, const char* debug_name, const Thread_Cache_Config* config_or_null)
{
    thread_cache_deinit(cache);
    cache->name = debug_name;
    if(config_or_null)
        cache->config = *config_or_null;
    for(isize i = 0; i < cache->config.preallocate_threads_or_negative; i++)
        thread_cache_lunch_thread(cache, NULL, NULL, 0, "init task %i", (int) i);
}

void thread_cache_deinit(Thread_Cache* cache)
{
    _THREAD_CACHE_USE_ATOMICS;
    cache->is_closed = true;

    //wait for all threads to exit
    for(;;) {
        uint32_t thread_count = atomic_load_explicit(&cache->threads_init, memory_order_relaxed);
        uint32_t threads_deinit = atomic_load_explicit(&cache->threads_deinit, memory_order_relaxed);
        if(thread_count == threads_deinit)
            break;

        platform_futex_wait(&cache->threads_deinit, threads_deinit, -1);
    }

    memset(cache, 0, sizeof *cache);
}

#define _Thread_local 

_Thread_local Thread_Cache_Thread* t_thread_cache_thread = NULL; 
void _thread_cache_run_func(void* context)
{
    _THREAD_CACHE_USE_ATOMICS;
    Thread_Cache_Thread* self = (Thread_Cache_Thread*) context;
    t_thread_cache_thread = self;
    Thread_Cache* cache = self->cache;
    Thread_Cache_Config* config = &cache->config;
    if(config->thread_init) config->thread_init(config->thread_context);
    
    while(atomic_load_explicit(&cache->is_closed, memory_order_relaxed) == false) {
        Thread_Cache_State state = atomic_load_explicit(&self->state, memory_order_relaxed);
        if(state != THREAD_CACHE_RUNNING)
            platform_futex_wait(&self->state, state, -1);
        else {
            if(config->thread_before_func) config->thread_before_func(config->thread_context);
            if(self->func) self->func(self->args);
            if(config->thread_after_func) config->thread_after_func(config->thread_context);

            atomic_store_explicit(&self->state, THREAD_CACHE_IDLE, memory_order_relaxed);
            atomic_fetch_add_explicit(&self->lunch_id, 1, memory_order_relaxed);
        }
    }

    if(config->thread_deinit) config->thread_deinit(config->thread_context);

    atomic_fetch_add(&cache->threads_deinit, 1);
    platform_futex_wake_all(&cache->threads_deinit);

    free(self->args);
    free(self);
}

void thread_cache_lunch_thread(Thread_Cache* cache, void (*func)(void* context), const void* args, isize args_size, const char* thread_name_fmt, ...)
{
    _THREAD_CACHE_USE_ATOMICS;

    if(atomic_load_explicit(&cache->is_closed, memory_order_relaxed) && t_thread_cache_thread == NULL)
        PANIC("Thread_Cache: thread_cache_lunch_thread after deinit from outside thread");

    //Attempt to find an idle thread. Go through all threads and simply CAS to try take one.
    //If something changed between the start of the search and end we restart.
    Thread_Cache_Thread* thread = NULL;
    while(true) {
        uint64_t threads_started = cache->threads_started;
        uint64_t threads_finished = cache->threads_finished;
        isize threads_init = cache->threads_init;
        isize running = (isize) threads_started - (isize) threads_finished;
        
        ASSERT(threads_init >= 0);
        ASSERT(running >= 0);
        if(running < threads_init)
        {
            for(Thread_Cache_Thread* curr = atomic_load_explicit(&cache->threads, memory_order_relaxed); curr; curr = curr->next) {
                Thread_Cache_State state = atomic_load_explicit(&curr->state, memory_order_relaxed);
                if(state == THREAD_CACHE_IDLE) {
                    if(atomic_compare_exchange_strong_explicit(&curr->state, &state, THREAD_CACHE_STARTING, memory_order_relaxed, memory_order_relaxed)) {
                        thread = curr;
                        goto outer_loop_end;
                    }
                }
            }
        }
        
        uint64_t new_threads_started = cache->threads_started;
        uint64_t new_threads_finished = cache->threads_finished;
        
        //if nothing changed than we are truly empty and we should create a new thread
        if(new_threads_started == threads_started && threads_finished == new_threads_finished) 
            break;
    }
    outer_loop_end:

    //if didnt find created one
    if(thread == NULL) {
        thread = (Thread_Cache_Thread*) calloc(1, sizeof(Thread_Cache_Thread));
        if(thread == NULL)
            PANIC("Thread_Cache: OUT of memory");

        thread->state = THREAD_CACHE_STARTING;
        thread->args_capacity = 256;
        thread->args = malloc(thread->args_capacity);
        thread->cache = cache;
        thread->created_from = thread_cache_self();

        //push to atomic Treiber stack
        for(;;) {
            Thread_Cache_Thread* first = atomic_load_explicit(&cache->threads, memory_order_relaxed);
            atomic_store_explicit(&thread->next, first, memory_order_relaxed);
            if(atomic_compare_exchange_weak_explicit(&cache->threads, &first, thread, 
                memory_order_release, memory_order_relaxed))
                break;
        }

        //launch thread
        atomic_fetch_add_explicit(&cache->threads_init, 1, memory_order_relaxed);
        if(platform_thread_launch(cache->config.stack_space_or_negative, _thread_cache_run_func, thread, 
            "Thread_Cache name:%s thread %i", cache->name ? cache->name : "empty", (int) cache->threads_init + 1) != 0)
            PANIC("Thread_Cache: failed to make os thread");
    }

    //copy over the argument data
    if(thread->args_capacity < args_size) {
        isize new_cap = thread->args_capacity;
        while(new_cap < args_size)
            new_cap *= 2;
        thread->args = realloc(thread->args, new_cap);
        thread->args_capacity = new_cap;
    }
    memcpy(thread->args, args, args_size);
    
    //copy over the name
    va_list name_args;
    va_start(name_args, thread_name_fmt);
    vsnprintf(thread->name, sizeof thread->name, thread_name_fmt, name_args);
    va_end(name_args);

    thread->func = func;
    thread->state = THREAD_CACHE_RUNNING;
    platform_futex_wake_all(&thread->state);
}

Thread_Cache_Thread* thread_cache_self()
{
    return t_thread_cache_thread;
}
const char* thread_cache_self_name()
{
    if(t_thread_cache_thread)
        return t_thread_cache_thread->name;
    else
        //platform_thread_name();
        return "<not-thread-cache-thread>";
}
