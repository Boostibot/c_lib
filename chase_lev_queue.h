#ifndef JOT_CHASE_LEV_QUEUE
#define JOT_CHASE_LEV_QUEUE

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
    #include <atomic>
    #define CL_QUEUE_ATOMIC(T)    std::atomic<T>
    #define CL_QUEUE_THREAD_LOCAL thread_local
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define CL_QUEUE_ATOMIC(T)    _Atomic(T) 
    #define CL_QUEUE_THREAD_LOCAL _Thread_local
#endif

#if defined(_MSC_VER)
    #define CL_QUEUE_INLINE_ALWAYS   __forceinline
    #define CL_QUEUE_INLINE_NEVER    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define CL_QUEUE_INLINE_ALWAYS   __attribute__((always_inline)) inline
    #define CL_QUEUE_INLINE_NEVER    __attribute__((noinline))
#else
    #define CL_QUEUE_INLINE_ALWAYS   inline
    #define CL_QUEUE_INLINE_NEVER
#endif

#ifndef CL_QUEUE_CUSTOM
    #define CL_QUEUE_API_INLINE         CL_QUEUE_INLINE_ALWAYS static
    #define CL_QUEUE_API                static
    #define CL_QUEUE_CACHE_LINE         64
    #define JOT_CHASE_LEV_QUEUE_IMPL
#endif

//for development so that MSVC doesnt lose its mind
//#define CL_QUEUE_ATOMIC(T) T
//#define CL_QUEUE_THREAD_LOCAL 
//#define alignas(x) 

typedef int64_t isize;

typedef struct CL_Queue_Block {
    struct CL_Queue_Block* next;
    uint64_t mask; //capacity - 1
    //items here...
} CL_Queue_Block;

typedef struct CL_Queue {
    alignas(CL_QUEUE_CACHE_LINE) 
    CL_QUEUE_ATOMIC(uint64_t) top; //owned by consumers

    alignas(CL_QUEUE_CACHE_LINE) 
    CL_QUEUE_ATOMIC(uint64_t) bot; //owned by producer = "owner"
    CL_QUEUE_ATOMIC(int*) owner;
    
    alignas(CL_QUEUE_CACHE_LINE) 
    CL_QUEUE_ATOMIC(CL_Queue_Block*) block;
    CL_QUEUE_ATOMIC(uint64_t) max_capacity;
    CL_QUEUE_ATOMIC(int32_t) references;
    CL_QUEUE_ATOMIC(uint32_t) item_size;
} CL_Queue;

typedef enum CL_Queue_Pop_State{
    CL_QUEUE_POP_OK = 0,
    CL_QUEUE_POP_FAILED_RACE = 1,
    CL_QUEUE_POP_EMPTY = 2,
} CL_Queue_Pop_State;

CL_QUEUE_API void cl_queue_take_ownership(CL_Queue* queue);
CL_QUEUE_API void cl_queue_deinit(CL_Queue* queue);
CL_QUEUE_API void cl_queue_init(CL_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite);
CL_QUEUE_API CL_Queue* cl_queue_share(CL_Queue* queue);
CL_QUEUE_API CL_Queue_Block* cl_queue_reserve(CL_Queue* queue, isize to_size);
CL_QUEUE_API_INLINE bool cl_queue_push(CL_Queue *q, const void* item, isize item_size);
CL_QUEUE_API_INLINE bool cl_queue_pop(CL_Queue *q, void* item, isize item_size);
CL_QUEUE_API_INLINE bool cl_queue_pop_back(CL_Queue *q, void* item, isize item_size); 
CL_QUEUE_API_INLINE CL_Queue_Pop_State cl_queue_pop_weak(CL_Queue *q, void* item, isize item_size);
CL_QUEUE_API_INLINE uint64_t cl_queue_capacity(const CL_Queue *q);
CL_QUEUE_API_INLINE uint64_t cl_queue_count(const CL_Queue *q);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_CHASE_LEV_QUEUE_IMPL)) && !defined(JOT_CHASE_LEV_QUEUE_HAS_IMPL)
#define JOT_CHASE_LEV_QUEUE_HAS_IMPL

#ifdef JOT_COUPLED
    #include "assert.h"
#endif

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

#ifdef _MSC_VER
    #define cl_queue_aligned_alloc(size, align) _aligned_malloc((size), (align))
    #define cl_queue_aligned_free _aligned_free
#else
    #define cl_queue_aligned_alloc(size, align) aligned_alloc((align), (size))
    #define cl_queue_aligned_free free
#endif

static CL_QUEUE_THREAD_LOCAL int _cl_queue_thread_dummy = 0;
CL_QUEUE_API void cl_queue_take_ownership(CL_Queue* queue)
{
    atomic_store(&queue->owner, &_cl_queue_thread_dummy);
}

CL_QUEUE_API void cl_queue_deinit(CL_Queue* queue)
{
    int32_t refs = atomic_fetch_sub(&queue->references, 1);
    ASSERT(refs >= 0);
    if(refs == 0)
    {
        for(CL_Queue_Block* curr = queue->block; curr; )
        {
            CL_Queue_Block* next = curr->next;
            cl_queue_aligned_free(curr);
            curr = next;
        }
        memset(queue, 0, sizeof *queue);
        atomic_thread_fence(memory_order_seq_cst);
    }
}

CL_QUEUE_API void cl_queue_init(CL_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite)
{
    memset(queue, 0, sizeof *queue);
    queue->item_size = (uint32_t) item_size;
    queue->max_capacity = max_capacity_or_negative_if_infinite >= 0 ? (uint64_t) max_capacity_or_negative_if_infinite : UINT64_MAX;
    cl_queue_take_ownership(queue);
    cl_queue_share(queue);
    atomic_thread_fence(memory_order_seq_cst);
}

CL_QUEUE_API CL_Queue* cl_queue_share(CL_Queue* queue)
{
    atomic_fetch_add(&queue->references, 1);
    return queue;
}

CL_QUEUE_API_INLINE void* _cl_queue_slot(CL_Queue_Block* block, uint64_t i, isize item_size)
{
    uint8_t* data = (int*) (void*) (block + 1);
    return data + (i & block->mask)*item_size;
}

CL_QUEUE_API CL_Queue_Block* cl_queue_reserve(CL_Queue* queue, isize to_size)
{
    ASSERT(atomic_load_explicit(&queue->owner, memory_order_relaxed) == &_cl_queue_thread_dummy, 
        "Must only be called by the owning thread."
        "If the queue was transfered, the owning thread must call cl_queue_take_ownership");

    CL_Queue_Block* old_block = atomic_load(&queue->block);
    CL_Queue_Block* out_block = old_block;
    uint64_t old_cap = old_block ? (old_block->mask + 1) : 0;
    uint64_t max_capacity  = queue->max_capacity;
    isize item_size = queue->item_size;

    if(old_cap < to_size && to_size <= max_capacity)
    {
        uint64_t new_cap = 16;
        while((isize) new_cap < to_size)
            new_cap *= 2;

        CL_Queue_Block* new_block = (CL_Queue_Block*) cl_queue_aligned_alloc(sizeof(CL_Queue_Block) + new_cap*item_size, NULL, CL_QUEUE_CACHE_LINE);
        new_block->next = old_block;
        new_block->mask = new_cap - 1;

        if(old_block)
        {
            uint64_t t = atomic_load(&queue->top);
            uint64_t b = atomic_load(&queue->bot);
            for(uint64_t i = t; i < b; i++)
                memcpy(_cl_queue_slot(new_block, i, item_size), _cl_queue_slot(old_block, i, item_size), item_size);
        }

        atomic_store(&queue->block, new_block);
        out_block = new_block;
        
    }

    return out_block;
}

CL_QUEUE_API_INLINE bool cl_queue_pop_back(CL_Queue *q, void* item, isize item_size) 
{
    ASSERT(atomic_load_explicit(&item_size, memory_order_relaxed) == item_size);
    ASSERT(atomic_load_explicit(&q->owner, memory_order_relaxed) == &_cl_queue_thread_dummy, 
        "Must only be called by the owning thread."
        "If the queue was transfered, the owning thread must call cl_queue_take_ownership");

    uint64_t b = atomic_load_explicit(&q->bot, memory_order_relaxed) - 1;
    CL_Queue_Block* a = atomic_load(&q->block, memory_order_relaxed);
    atomic_store_explicit(&q->bot, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    uint64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);

    bool out = true;
    if (t <= b) {
        if (t == b) {
            // Single last element in queue. 
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
                // Failed race (=> the queue must be empty since we are the sole producer)
                goto fail;

            atomic_store_explicit(&q->bot, b + 1, memory_order_relaxed);
        }

        //@NOTE: copy out once a slot has been secured. 
        //We can do this because this function can only be called by the owner
        // (thus there is no risk of push overwriting the data).
        //This is possibly the only major change we have made compared to the reference paper
        void* slot = _cl_queue_slot(a, b, item_size);
        memcpy(item, slot, item_size);
    } 
    //Empty queue
    else { 
        fail:
        atomic_store_explicit(&q->bot, b + 1, memory_order_relaxed);
        out = false;
    }
    return out;
}

CL_QUEUE_API_INLINE bool cl_queue_push(CL_Queue *q, const void* item, isize item_size) 
{
    ASSERT(atomic_load_explicit(&item_size, memory_order_relaxed) == item_size);
    ASSERT(atomic_load_explicit(&q->owner, memory_order_relaxed) == &_cl_queue_thread_dummy, 
        "Must only be called by the owning thread."
        "If the queue was transfered, the owning thread must call cl_queue_take_ownership");

    uint64_t b = atomic_load_explicit(&q->bot, memory_order_relaxed);
    uint64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    CL_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_relaxed);
    if (a == NULL || b - t > a->mask) { 
        CL_Queue_Block* new_a = cl_queue_reserve(q, a->mask);
        if(new_a == a)
            return false;

        a = new_a;
    }
    
    void* slot = _cl_queue_slot(a, b, item_size);
    memcpy(slot, item, item_size);

    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&q->bot, b + 1, memory_order_relaxed);
    return true;
}

CL_QUEUE_API_INLINE CL_Queue_Pop_State cl_queue_pop_weak(CL_Queue *q, void* item, isize item_size) 
{
    ASSERT(atomic_load_explicit(&item_size, memory_order_relaxed) == item_size);

    uint64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    uint64_t b = atomic_load_explicit(&q->bot, memory_order_acquire);

    CL_Queue_Pop_State out = CL_QUEUE_POP_EMPTY;
    if (t < b) {
        CL_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_consume);
        
        void* slot = _cl_queue_slot(a, b, item_size);
        memcpy(item, slot, item_size);

        if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
            out = CL_QUEUE_POP_FAILED_RACE;
        else
            out = CL_QUEUE_POP_OK;
    }
    return out;
}

CL_QUEUE_API_INLINE bool cl_queue_pop(CL_Queue *q, void* item, isize item_size) 
{
    for(;;) {
        CL_Queue_Pop_State state = cl_queue_pop_weak(q, item, item_size);
        if(state != CL_QUEUE_POP_FAILED_RACE)
            return state == CL_QUEUE_POP_OK;
    }
}

CL_QUEUE_API_INLINE uint64_t cl_queue_capacity(const CL_Queue *q)
{
    CL_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_relaxed);
    return a ? a->mask + 1 : 0;
}

CL_QUEUE_API_INLINE uint64_t cl_queue_count(const CL_Queue *q)
{
    uint64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    uint64_t b = atomic_load_explicit(&q->bot, memory_order_relaxed);
    if (t < b)
        return t - b;
    else
        return 0;
}

#endif