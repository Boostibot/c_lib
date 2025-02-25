#ifndef MODULE_SPMC_QUEUE
#define MODULE_SPMC_QUEUE

//This is SPMC (Single Producer Multiple Consumer) growing queue. 
// Another queue impelemntation that does basically the same thing is the
// Rigtorp queue, see here: https://rigtorp.se/ringbuffer/.

// It is faster than Chase-Lev or similar queues because it drastically reduces the need to
// read other thread's data, thus lowering contention. This is done by keeping an estimate
// of the other threads data and only updating that estimate when something exceptional
// happens, in this case the queue being perceived as empty or full.
//
// The queue functions marked with *_st should be read as Single Thread and as the name
// suggests should be called from a single thread at a time. The push has only st. variant
// while the pop has both st and non-st variant. The st. variant runs a bit faster because
// it doesnt have to use any synchronization with other popping threads, thus should be 
// used when we are only dealing with SPSC situation.

#if defined(_MSC_VER)
    #define SPMC_QUEUE_INLINE_ALWAYS   __forceinline
    #define SPMC_QUEUE_INLINE_NEVER    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define SPMC_QUEUE_INLINE_ALWAYS   __attribute__((always_inline)) inline
    #define SPMC_QUEUE_INLINE_NEVER    __attribute__((noinline))
#else
    #define SPMC_QUEUE_INLINE_ALWAYS   inline
    #define SPMC_QUEUE_INLINE_NEVER
#endif

#ifndef SPMC_QUEUE_API
    #define SPMC_QUEUE_API_INLINE         SPMC_QUEUE_INLINE_ALWAYS static
    #define SPMC_QUEUE_API                static
    #define MODULE_SPMC_QUEUE_IMPL
#endif

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
    #include <atomic>
    #define SPMC_QUEUE_ATOMIC(T)    std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define SPMC_QUEUE_ATOMIC(T)    _Atomic(T) 
#endif

typedef int64_t isize;

typedef struct SPMC_Queue_Block {
    struct SPMC_Queue_Block* next;
    uint64_t mask; //capacity - 1
    //items here...
} SPMC_Queue_Block;

typedef struct SPMC_Queue {
    alignas(64)
    SPMC_QUEUE_ATOMIC(uint64_t) top; //changed by pop
    SPMC_QUEUE_ATOMIC(uint64_t) estimate_bot;
    uint64_t _pad1[6];

    alignas(64)
    SPMC_QUEUE_ATOMIC(uint64_t) bot; //changed by push
    uint64_t estimate_top;
    uint64_t _pad2[6];

    alignas(64)
    SPMC_QUEUE_ATOMIC(SPMC_Queue_Block*) block;
    SPMC_QUEUE_ATOMIC(uint32_t) item_size;
    SPMC_QUEUE_ATOMIC(uint32_t) max_capacity_log2; //0 means max capacity off!
    uint64_t _pad3[6];
} SPMC_Queue;

SPMC_QUEUE_API void spmc_queue_deinit(SPMC_Queue* queue);
SPMC_QUEUE_API void spmc_queue_init(SPMC_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite);
SPMC_QUEUE_API void spmc_queue_reserve(SPMC_Queue* queue, isize to_size);
SPMC_QUEUE_API_INLINE bool spmc_queue_push_st(SPMC_Queue *q, const void* item, isize item_size);
SPMC_QUEUE_API_INLINE bool spmc_queue_pop_st(SPMC_Queue *q, void* item, isize item_size);
SPMC_QUEUE_API_INLINE bool spmc_queue_pop(SPMC_Queue *q, void* item, isize item_size);
SPMC_QUEUE_API_INLINE isize spmc_queue_capacity(const SPMC_Queue *q);
SPMC_QUEUE_API_INLINE isize spmc_queue_count(const SPMC_Queue *q);

//Result interface - is sometimes needed when using this queue as a building block for other DS
typedef enum SPMC_Queue_State{
    SPMC_QUEUE_OK = 0,
    SPMC_QUEUE_EMPTY,
    SPMC_QUEUE_FULL,
    SPMC_QUEUE_FAILED_RACE, //only returned from spmc_queue_result_pop_weak functions
} SPMC_Queue_State;

//contains the state indicator as well as block, bot, top 
// which hold values obtained *before* the call to the said function
typedef struct SPMC_Queue_Result {
    uint64_t bot;
    uint64_t top;
    SPMC_Queue_State state;
    int _;
} SPMC_Queue_Result;

SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_push_st(SPMC_Queue *q, const void* item, isize item_size);
SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_pop_st(SPMC_Queue *q, void* item, isize item_size);
SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_pop(SPMC_Queue *q, void* item, isize item_size);
SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_pop_weak(SPMC_Queue *q, void* item, isize item_size);
#endif

#if (defined(MODULE_ALL_IMPL) || defined(MODULE_SPMC_QUEUE_IMPL)) && !defined(MODULE_SPMC_QUEUE_HAS_IMPL)
#define MODULE_SPMC_QUEUE_HAS_IMPL

#ifdef MODULE_COUPLED
    #include "assert.h"
#endif

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

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

SPMC_QUEUE_API void spmc_queue_deinit(SPMC_Queue* queue)
{
    for(SPMC_Queue_Block* curr = queue->block; curr; )
    {
        SPMC_Queue_Block* next = curr->next;
        free(curr);
        curr = next;
    }
    memset(queue, 0, sizeof *queue);
    atomic_store(&queue->block, NULL);
}

SPMC_QUEUE_API void spmc_queue_init(SPMC_Queue* queue, isize item_size, isize max_capacity_or_negative_if_infinite)
{
    spmc_queue_deinit(queue);
    queue->item_size = (uint32_t) item_size;
    if(max_capacity_or_negative_if_infinite >= 0)
    {
        while((uint64_t) 1 << queue->max_capacity_log2 < (uint64_t) max_capacity_or_negative_if_infinite)
            queue->max_capacity_log2 ++;

        queue->max_capacity_log2 ++;
    }

    atomic_store(&queue->block, NULL);
}

SPMC_QUEUE_API_INLINE void* _spmc_queue_slot(SPMC_Queue_Block* block, uint64_t i, isize item_size)
{
    uint64_t mapped = i & block->mask;
    uint8_t* data = (uint8_t*) (void*) (block + 1);
    return data + mapped*item_size;
}

SPMC_QUEUE_INLINE_NEVER
SPMC_QUEUE_API SPMC_Queue_Block* _spmc_queue_reserve(SPMC_Queue* queue, isize to_size)
{
    SPMC_Queue_Block* old_block = atomic_load(&queue->block);
    SPMC_Queue_Block* out_block = old_block;
    isize old_cap = old_block ? (isize) (old_block->mask + 1) : 0;
    isize item_size = queue->item_size;
    isize max_capacity = queue->max_capacity_log2 > 0 
        ? (isize) 1 << (queue->max_capacity_log2 - 1) 
        : INT64_MAX;

    if(old_cap < to_size && to_size <= max_capacity)
    {
        uint64_t new_cap = 64;
        while((isize) new_cap < to_size)
            new_cap *= 2;

        SPMC_Queue_Block* new_block = (SPMC_Queue_Block*) malloc(sizeof(SPMC_Queue_Block) + new_cap*item_size);
        if(new_block)
        {
            new_block->next = old_block;
            new_block->mask = new_cap - 1;

            if(old_block)
            {
                uint64_t t = atomic_load(&queue->top);
                uint64_t b = atomic_load(&queue->bot);
                for(uint64_t i = t; (int64_t) (i - b) < 0; i++) //i < b
                    memcpy(_spmc_queue_slot(new_block, i, item_size), _spmc_queue_slot(old_block, i, item_size), item_size);
            }

            atomic_store(&queue->block, new_block);
            out_block = new_block;
        }
        
    }

    return out_block;
}

SPMC_QUEUE_API void spmc_queue_reserve(SPMC_Queue* queue, isize to_size)
{
    _spmc_queue_reserve(queue, to_size);
}

SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_push_st(SPMC_Queue *q, const void* item, isize item_size)
{
    _SPMC_QUEUE_USE_ATOMICS;
    ASSERT(atomic_load_explicit(&q->item_size, memory_order_relaxed) == item_size);

    SPMC_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_relaxed);
    uint64_t b = atomic_load_explicit(&q->bot, memory_order_relaxed);
    uint64_t t = q->estimate_top;

    if (a == NULL || (int64_t)(b - t) > (int64_t) a->mask) { 
        t = atomic_load_explicit(&q->top, memory_order_acquire);
        q->estimate_top = t;
        if (a == NULL || (int64_t)(b - t) > (int64_t) a->mask) { 
            SPMC_Queue_Block* new_a = _spmc_queue_reserve(q, b - t + 1);
            if(new_a == a)
            {
                SPMC_Queue_Result out = {b, t, SPMC_QUEUE_FULL};
                return out;
            }

            a = new_a;
        }
    }
    
    void* slot = _spmc_queue_slot(a, b, item_size);
    memcpy(slot, item, item_size);

    atomic_store_explicit(&q->bot, b + 1, memory_order_release);
    SPMC_Queue_Result out = {b, t, SPMC_QUEUE_OK};
    return out;
}

SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_pop_st(SPMC_Queue *q, void* item, isize item_size)
{
    _SPMC_QUEUE_USE_ATOMICS;
    ASSERT(atomic_load_explicit(&q->item_size, memory_order_relaxed) == item_size);
    uint64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    uint64_t b = atomic_load_explicit(&q->estimate_bot, memory_order_relaxed);
    
    SPMC_Queue_Result out = {b, t, SPMC_QUEUE_EMPTY};

    //if empty reload bot estimate
    if ((int64_t) (b - t) <= 0) {
        b = atomic_load_explicit(&q->bot, memory_order_relaxed);
        atomic_store_explicit(&q->estimate_bot, b, memory_order_relaxed);
        out.bot = b;
        if ((int64_t) (b - t) <= 0) 
            return out;
    }
    
    //seq cst because we must ensure we dont get updated t,b and old block! 
    // Then we would assume there are items to pop, copy over uninitialized memory from old block and succeed. (bad!)
    // For x86 the generated assembly is identical even if we replace it by memory_order_acquire.
    // For weak memory model architectures it wont be. 
    // If you dont like this you can instead store all of the fields of queue (top, estimate_bot, bot...)
    //  in the block header instead. That way it will be again impossible to get top, bot and old block.
    //  I dont bother with this as I primarily care about x86 and I find the code written like this be easier to read. 
    SPMC_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_seq_cst);

    void* slot = _spmc_queue_slot(a, t, item_size);
    memcpy(item, slot, item_size);

    atomic_store_explicit(&q->top, t + 1, memory_order_relaxed);
    out.state = SPMC_QUEUE_OK;

    return out;
}

SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_pop_weak(SPMC_Queue *q, void* item, isize item_size)
{
    _SPMC_QUEUE_USE_ATOMICS;
    ASSERT(atomic_load_explicit(&q->item_size, memory_order_relaxed) == item_size);
    uint64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    uint64_t b = atomic_load_explicit(&q->estimate_bot, memory_order_relaxed);
    
    SPMC_Queue_Result out = {b, t, SPMC_QUEUE_EMPTY};

    //if empty reload bot estimate
    if ((int64_t) (t - b) >= 0) {
        b = atomic_load_explicit(&q->bot, memory_order_relaxed);
        atomic_store_explicit(&q->estimate_bot, b, memory_order_relaxed);
        out.bot = b;
        if ((int64_t) (t - b) >= 0) 
            return out;
    }
    
    SPMC_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_seq_cst);

    void* slot = _spmc_queue_slot(a, t, item_size);
    memcpy(item, slot, item_size);

    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
        out.state = SPMC_QUEUE_FAILED_RACE;
    else
        out.state = SPMC_QUEUE_OK;

    return out;
}

SPMC_QUEUE_API_INLINE SPMC_Queue_Result spmc_queue_result_pop(SPMC_Queue *q, void* item, isize item_size)
{
    for(;;) {
        SPMC_Queue_Result result = spmc_queue_result_pop_weak(q, item, item_size);
        if(result.state != SPMC_QUEUE_FAILED_RACE)
            return result;
    }
}

SPMC_QUEUE_API_INLINE bool spmc_queue_push_st(SPMC_Queue *q, const void* item, isize item_size)
{
    return spmc_queue_result_push_st(q, item, item_size).state == SPMC_QUEUE_OK;
}

SPMC_QUEUE_API_INLINE bool spmc_queue_pop_st(SPMC_Queue *q, void* items, isize item_size)
{
    return spmc_queue_result_pop_st(q, items, item_size).state == SPMC_QUEUE_OK;
}

SPMC_QUEUE_API_INLINE bool spmc_queue_pop(SPMC_Queue *q, void* item, isize item_size)
{
    return spmc_queue_result_pop(q, item, item_size).state == SPMC_QUEUE_OK;
}

SPMC_QUEUE_API_INLINE isize spmc_queue_capacity(const SPMC_Queue *q)
{
    _SPMC_QUEUE_USE_ATOMICS;
    SPMC_Queue_Block *a = atomic_load_explicit(&q->block, memory_order_relaxed);
    return a ? (isize) a->mask + 1 : 0;
}

SPMC_QUEUE_API_INLINE isize spmc_queue_count(const SPMC_Queue *q)
{
    _SPMC_QUEUE_USE_ATOMICS;
    uint64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    uint64_t b = atomic_load_explicit(&q->bot, memory_order_relaxed);
    uint64_t diff = (isize) (b - t);
    return diff >= 0 ? diff : 0;
}

#endif