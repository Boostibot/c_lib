#pragma once

#include "lib/chase_lev_queue.h"

#define ATOMIC(T) T

typedef struct Sync_Pool Sync_Pool;

typedef struct Sync_Pool_Thread {
    CL_Queue queue;
    Sync_Pool* pool;
    Sync_Pool_Thread* stealing_from;
    Sync_Pool_Thread* next;
} Sync_Pool_Thread;

typedef struct Sync_Pool {
    ATOMIC(Sync_Pool_Thread*) threads;
    ATOMIC(isize) thread_count; 
    ATOMIC(isize) item_count;

    isize max_capacity;
    isize initial_capacity;
    isize item_size;
} Sync_Pool;

void sync_pool_init(Sync_Pool* pool, isize item_size, isize max_capacity_or_minus_one);
void sync_pool_deinit(Sync_Pool* pool);

Sync_Pool_Thread* sync_pool_thread_add(Sync_Pool* pool);

bool sync_pool_pop(Sync_Pool_Thread* pool, void* data);
bool sync_pool_push(Sync_Pool_Thread* pool, const void* data);

void sync_pool_init(Sync_Pool* pool, isize item_size, isize initial_capacity_or_minus_one, isize max_capacity_or_minus_one)
{
    memset(pool, 0, sizeof *pool);
    pool->item_size = item_size;
    pool->initial_capacity = initial_capacity_or_minus_one;
    pool->max_capacity = max_capacity_or_minus_one;

    atomic_store(&pool->threads, 0);
    atomic_store(&pool->thread_count, 0);
    atomic_store(&pool->item_count, 0);
}

void sync_pool_deinit(Sync_Pool* pool)
{
    isize threads_counted = 0;
    for(Sync_Pool_Thread* curr = pool->threads; curr != NULL; )
    {
        Sync_Pool_Thread* next = curr->next;
        cl_queue_deinit(&next->queue);
        free(curr);
        curr = next;
    }
    
    memset(pool, 0, sizeof *pool);
    atomic_store(&pool->item_count, 0);
}

Sync_Pool_Thread* sync_pool_thread_add(Sync_Pool* pool)
{
    Sync_Pool_Thread* thread = (Sync_Pool_Thread*) malloc(sizeof(Sync_Pool_Thread));
    thread->pool = pool;
    thread->stealing_from = thread;
    cl_queue_init(&thread->queue, pool->item_size, pool->max_capacity);
    cl_queue_reserve(&thread->queue, pool->initial_capacity);
    
    //atomically make thread availible to be stolen from
    for(;;) {
        Sync_Pool_Thread* tail = atomic_load(&pool->threads);
        //its fine we dont atomic store because the CAS is seq_cst order...
        thread->next = tail; 
        if(atomic_compare_exchange_weak(&pool->threads, &tail, thread))
            break;
    }
    
    atomic_fetch_add(&pool->thread_count, 1);
    return thread;
}

bool sync_pool_pop(Sync_Pool_Thread* thread, void* data)
{
    Sync_Pool* pool = thread->pool;
    Sync_Pool_Thread* steal = thread->stealing_from;
    bool out = false;
    for(;;) {
        isize item_count = atomic_load(&pool->item_count);
        ASSERT(item_count >= 0);
        if(item_count == 0)
            break;

        if(cl_queue_pop(&steal->queue, data, pool->item_size)) {
            atomic_fetch_sub(&pool->item_count, 1);
            out = true;
        }

        steal = steal->next;
        if(steal == NULL)
            steal = pool->threads;
    }
    thread->stealing_from = steal;
    return out;
}

bool sync_pool_push(Sync_Pool_Thread* thread, const void* data)
{
    Sync_Pool* pool = thread->pool;
    atomic_fetch_add(&pool->item_count, 1);
    return cl_queue_push(&thread->queue, data, pool->item_size);
}