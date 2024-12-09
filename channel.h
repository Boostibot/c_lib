#ifndef JOT_CHANNEL
#define JOT_CHANNEL

//==========================================================================
// Channel (high throuput concurrent queue)
//==========================================================================
// An linearizable blocking concurrent queue based on the design described in 
// "T. R. W. Scogland - Design and Evaluation of Scalable Concurrent Queues for Many-Core Architectures, 2015" 
// which can be found at https://synergy.cs.vt.edu/pubs/papers/scogland-queues-icpe15.pdf.
// 
// We differ from the implementation in the paper in that we support proper thread blocking via futexes and
// employ more useful semantics around closing.
// 
// The channel acts pretty much as a Go buffered channel augmented with additional
// non-blocking and ticket interfaces. These allow us to for example only push if the
// channel is not full or wait for item to be processed.
//
// The basic idea is to do a very fine grained locking: each item in the channel has a dedicated
// ticket lock. On push/pop we perform atomic fetch and add (FAA) one to the tail/head indices, which yields
// a number used to calculate our slot and operation id. This slot is potentially shared with other 
// pushes or pops because the queue has finite capacity. We go to that slot and wait on its ticket lock 
// to signal id corresponding to this push/pop operation. Only then we push/pop the item then advance 
// the ticket lock, allowing a next operation on that slot to proceed.
// 
// This procedure means that unless the queue is full/empty a single push/pop contains only 
// one atomic FAA on the critical path (ticket locks are uncontested), resulting in extremely
// high throughput pretty much only limited by the FAA contention.

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
    #include <atomic>
    #define CHAN_ATOMIC(T) std::atomic<T>
#else
    #include <stdatomic.h>
    #include <stdalign.h>
    #define CHAN_ATOMIC(T) _Atomic(T) 
#endif

#if defined(_MSC_VER)
    #define _CHAN_INLINE_ALWAYS   __forceinline
    #define _CHAN_INLINE_NEVER    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define _CHAN_INLINE_ALWAYS   __attribute__((always_inline)) inline
    #define _CHAN_INLINE_NEVER    __attribute__((noinline))
#else
    #define _CHAN_INLINE_ALWAYS   inline
    #define _CHAN_INLINE_NEVER
#endif

#ifndef CHAN_CUSTOM
    #define CHANAPI        _CHAN_INLINE_ALWAYS static
    #define CHAN_INTRINSIC _CHAN_INLINE_ALWAYS static //TODO remove
    #define CHAN_OS_API    static
    #define JOT_CHANNEL_IMPL
    #define CHAN_CACHE_LINE 64
#endif

typedef int64_t isize;

typedef bool (*Sync_Wait_Func)(volatile void* state, uint32_t undesired, double timeout_or_negative_if_infinite);
typedef void (*Sync_Wake_Func)(volatile void* state);

typedef struct Channel_Info {
    isize item_size;
    Sync_Wait_Func wait;
    Sync_Wake_Func wake;
} Channel_Info;

typedef struct Channel {
    alignas(CHAN_CACHE_LINE) 
    CHAN_ATOMIC(uint64_t) head;
    CHAN_ATOMIC(uint64_t) head_barrier;
    CHAN_ATOMIC(uint64_t) head_cancel_count;
    uint64_t _head_pad[5];

    alignas(CHAN_CACHE_LINE) 
    CHAN_ATOMIC(uint64_t) tail;
    CHAN_ATOMIC(uint64_t) tail_barrier;
    CHAN_ATOMIC(uint64_t) tail_cancel_count;

    //stuffed here so that the structure is smaller
    CHAN_ATOMIC(uint32_t) allocated;
    uint32_t _tail_pad[9];

    alignas(CHAN_CACHE_LINE) 
    Channel_Info info;
    isize capacity; 
    uint8_t* items; 
    CHAN_ATOMIC(uint32_t)* ids; 
    CHAN_ATOMIC(uint32_t) ref_count; 
    CHAN_ATOMIC(uint32_t) closing_state;
    CHAN_ATOMIC(uint32_t) closing_lock_requested;
    CHAN_ATOMIC(uint32_t) closing_lock_completed;
} Channel;

typedef enum Channel_Res {
    CHANNEL_OK = 0,
    CHANNEL_CLOSED = 1,
    CHANNEL_LOST_RACE = 2,
    CHANNEL_FULL = 3,
    CHANNEL_EMPTY = 4,
} Channel_Res;

//Allocates a new channel on the heap and returns pointer to it. If the allocation fails returns 0.
//capacity >= 0. If capacity == 0 then creates an "unbuffered" channel which acts as unbuffered channel in Go.
// That is there is just one slot in the channel and after a call to channel_push the calling thread is
// waiting until channel_pop is called (or until the channel is closed).
CHANAPI Channel* channel_malloc(isize capacity, Channel_Info info);

//Increments the ref count of the channel. Returns the passed in channel.
CHANAPI Channel* channel_share(Channel* chan);

//Decrements the ref count and if it reaches zero deinitializes the channel. 
//If the channel was allocated through channel_malloc frees it.
//If it was created through channel_init only memsets the channel to zero.
CHANAPI int32_t channel_deinit(Channel* chan);

CHANAPI void     channel_init(Channel* chan, void* items, uint32_t* ids, isize capacity, Channel_Info info);
CHANAPI isize    channel_memory_size(isize capacity, Channel_Info info); //Obtains the combined needed size for the Channel struct and capacity items
CHANAPI Channel* channel_init_into_memory(void* aligned_memory, isize capacity, Channel_Info info); //Places and initializes the Channel struct into the given memory

//Pushes an item, waiting if channel is full. If the channel (side) is closed returns false instead of waiting else returns true.
CHANAPI bool channel_push(Channel* chan, const void* item, Channel_Info info);
//Pops an item, waiting if channel is empty. If the channel (side) is closed returns false instead of waiting else returns true.
CHANAPI bool channel_pop(Channel* chan, void* item, Channel_Info info);

//Attempts to push an item stored in item without blocking returning CHANNEL_OK on success.
// If the channel (side) is closed returns CHANNEL_CLOSED
// If the channel is full returns CHANNEL_FULL
// If lost a race to concurrent call to this function returns CHANNEL_LOST_RACE.
CHANAPI Channel_Res channel_try_push_weak(Channel* chan, const void* item, Channel_Info info);
//Attempts to pop an item storing it in item without blocking returning CHANNEL_OK on success.
// If the channel (side) is closed returns CHANNEL_CLOSED
// If the channel is empty returns CHANNEL_EMPTY
// If lost a race to concurrent call to this function returns CHANNEL_LOST_RACE.
CHANAPI Channel_Res channel_try_pop_weak(Channel* chan, void* item, Channel_Info info); 

//Same as channel_try_push/pop_weak but never returns CHANNEL_LOST_RACE.
//Instead retries until the operation completes successfully or some other error appears.
CHANAPI Channel_Res channel_try_push(Channel* chan, const void* item, Channel_Info info);
CHANAPI Channel_Res channel_try_pop(Channel* chan, void* item, Channel_Info info);

CHANAPI bool channel_close_push(Channel* chan, Channel_Info info);
CHANAPI bool channel_close_soft(Channel* chan, Channel_Info info);
CHANAPI bool channel_close_hard(Channel* chan, Channel_Info info);

CHANAPI bool channel_reopen(Channel* chan, Channel_Info info);
CHANAPI bool channel_is_closed(const Channel* chan); 

CHANAPI bool channel_hard_reopen(Channel* chan, Channel_Info info);
CHANAPI bool channel_is_hard_closed(const Channel* chan); 

//Returns upper bound to the distance between head and tail indices. 
//This can be used to approximately check the number of blocked threads or get the number of items in the channel.
CHANAPI isize channel_signed_distance(const Channel* chan);

//Returns upper bound to the number of items in the channel. Returned value is in range [0, chan->capacity]
CHANAPI isize channel_count(const Channel* chan);
CHANAPI bool channel_is_empty(const Channel* chan);

CHANAPI bool channel_is_invariant_converged_state(Channel* chan, Channel_Info info);

//==========================================================================
// Channel ticket interface 
//==========================================================================
// These functions work just like their regular counterparts but can also return the ticket of the completed operation.
// The ticket can be used to signal completion using the channel_ticket_is_less function. 
//
// For example when producer pushes into a channel and wants to wait for the consumer to process the pushed item, 
// it uses these functions to also obtain a ticket. The consumer also pops and takes and receives a ticket. 
// After each processed item it sets its ticket to global variable. The producer thus simply waits for the 
// the global variable to becomes not less then the received ticket using channel_ticket_is_less.

#define CHANNEL_MAX_TICKET (UINT64_MAX/4)

//Returns whether ticket_a came before ticket_b. 
//Unless unsigned number overflow happens this is just `ticket_a < ticket_b`.
CHANAPI bool channel_ticket_is_less(uint64_t ticket_a, uint64_t ticket_b);

//Returns whether ticket_a came before or is equal to ticket_b. 
//Unless unsigned number overflow happens this is just `ticket_a <= ticket_b`.
CHANAPI bool channel_ticket_is_less_or_eq(uint64_t ticket_a, uint64_t ticket_b);

CHANAPI bool channel_ticket_push(Channel* chan, const void* item, uint64_t* ticket_or_null, Channel_Info info);
CHANAPI bool channel_ticket_pop(Channel* chan, void* item, uint64_t* ticket_or_null, Channel_Info info);
CHANAPI Channel_Res channel_ticket_try_push(Channel* chan, const void* item, uint64_t* ticket_or_null, Channel_Info info);
CHANAPI Channel_Res channel_ticket_try_pop(Channel* chan, void* item, uint64_t* ticket_or_null, Channel_Info info);
CHANAPI Channel_Res channel_ticket_try_push_weak(Channel* chan, const void* item, uint64_t* ticket_or_null, Channel_Info info);
CHANAPI Channel_Res channel_ticket_try_pop_weak(Channel* chan, void* item, uint64_t* ticket_or_null, Channel_Info info);

//These functions can be used for Sync_Wait_Func/Sync_Wake_Func interfaces in the channel.
CHAN_INTRINSIC void chan_pause();

CHAN_OS_API void chan_wake_block(volatile void* state);
CHAN_OS_API bool chan_wait_block(volatile void* state, uint32_t undesired, double timeout_or_negatove_if_infinite);
CHAN_OS_API bool chan_wait_yield(volatile void* state, uint32_t undesired, double timeout_or_negatove_if_infinite);

CHAN_OS_API void chan_futex_wake_all(volatile uint32_t* state);
CHAN_OS_API void chan_futex_wake_single(volatile uint32_t* state);
CHAN_OS_API bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite);
CHAN_OS_API void chan_yield();
CHAN_OS_API void chan_sleep(double seconds);
CHAN_OS_API int64_t chan_perf_counter();
CHAN_OS_API int64_t chan_perf_frequency();
CHAN_OS_API bool chan_start_thread(void (*func)(void* context), void* context);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_CHANNEL_IMPL)) && !defined(JOT_CHANNEL_HAS_IMPL)
#define JOT_CHANNEL_HAS_IMPL

#ifdef JOT_COUPLED
    #include "assert.h"
#endif
#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

#ifndef chan_debug_log
    //cheaply logs into memory msg static string followed by up to two uint64_t values
    #define chan_debug_log(msg, ...) (void) sizeof((msg), ##__VA_ARGS__)   
    //performs n atomic additions on piece of global memory causing the caller to wait for a bit   
    // is used to make certain states more likely then others (increases the window between two instructions)
    #define chan_debug_wait(n)      (void) sizeof(n) 
#endif

#define _CHAN_ID_WAITING_BIT            ((uint32_t) 1)
#define _CHAN_ID_CLOSE_NOTIFY_BIT       ((uint32_t) 2)
#define _CHAN_ID_FILLED_BIT             ((uint32_t) 4)

#define _CHAN_TICKET_PUSH_CLOSED_BIT    ((uint64_t) 1)
#define _CHAN_TICKET_POP_CLOSED_BIT     ((uint64_t) 2)
#define _CHAN_TICKET_INCREMENT          ((uint64_t) 4)

#define _CHAN_CLOSING_PUSH              ((uint32_t) 1) 
#define _CHAN_CLOSING_POP               ((uint32_t) 2) 
#define _CHAN_CLOSING_CLOSED            ((uint32_t) 4) 
#define _CHAN_CLOSING_HARD              ((uint32_t) 8)

CHANAPI uint64_t _channel_get_target(const Channel* chan, uint64_t ticket)
{
    return ticket % (uint64_t) chan->capacity;
}

CHANAPI uint32_t _channel_get_id(const Channel* chan, uint64_t ticket)
{
    return ((uint32_t) (ticket / (uint64_t) chan->capacity)*_CHAN_ID_FILLED_BIT*2);
}

CHANAPI bool _channel_id_equals(uint32_t id1, uint32_t id2)
{
    return ((id1 ^ id2) / _CHAN_ID_FILLED_BIT) == 0;
}

CHANAPI void _channel_advance_id(Channel* chan, uint64_t target, uint32_t id, Channel_Info info)
{
    CHAN_ATOMIC(uint32_t)* id_ptr = &chan->ids[target];
    
    uint32_t new_id = (uint32_t) (id + _CHAN_ID_FILLED_BIT);
    if(info.wake)
    {
        uint32_t prev_id = atomic_exchange(id_ptr, new_id);
        ASSERT(_channel_id_equals(prev_id + _CHAN_ID_FILLED_BIT, new_id));
        if(prev_id & _CHAN_ID_WAITING_BIT)
            info.wake((void*) id_ptr);
    }
    else
        atomic_store(id_ptr, new_id);
}

_CHAN_INLINE_NEVER
static bool _channel_ticket_push_potentially_cancel(Channel* chan, uint64_t ticket, uint32_t closing)
{
    bool canceled = false;
    if(closing & _CHAN_CLOSING_HARD)
        canceled = true;
    else
    {
        uint64_t new_tail = atomic_load(&chan->tail);
        uint64_t new_head = atomic_load(&chan->head);
        uint64_t barrier = atomic_load(&chan->tail_barrier);

        if((new_head & _CHAN_TICKET_PUSH_CLOSED_BIT) || (new_tail & _CHAN_TICKET_PUSH_CLOSED_BIT))
            if(channel_ticket_is_less_or_eq(barrier, ticket))
                canceled = true; 
    }

    if(canceled)
    {
        atomic_fetch_add(&chan->tail_cancel_count, _CHAN_TICKET_INCREMENT);
        atomic_fetch_sub(&chan->tail, _CHAN_TICKET_INCREMENT);
        return false;
    }
    else
        return true;
}

//Must load tail aftter loading curr ticket because:
//  we cannot do "load curr, check if matching, else check if past barrier" because that does not respect barriers 
//  we cannot do "load tail and check if past else check mathcing" because the following could happen:
//   t1: push to a full queue
//   t1: push checks for past barrier but there is no barrier
//   t1: push is just before the check for curr 
//   t3: close tail placing barrier to head + capacity (so just before the ticket of the push above)
//   t2: pop first 
//   t1: push succeeds but by now we should have detected closed!
//Thus the only option is to load, load check check in this order
CHANAPI bool channel_ticket_push(Channel* chan, const void* item, uint64_t* out_ticket_or_null, Channel_Info info) 
{
    ASSERT(memcmp(&chan->info, &info, sizeof info) == 0, "info must be matching");
    ASSERT(item || (item == NULL && info.item_size == 0), "item must be provided");
    
    uint64_t tail = atomic_fetch_add(&chan->tail, _CHAN_TICKET_INCREMENT);
    uint64_t ticket = tail / _CHAN_TICKET_INCREMENT;
    uint64_t target = _channel_get_target(chan, ticket);
    uint32_t id = _channel_get_id(chan, ticket);
    chan_debug_log("push called", ticket);
    
    for(;;) {
        uint32_t curr = atomic_load(&chan->ids[target]);
        chan_debug_wait(3);
        uint32_t closing = atomic_load(&chan->closing_state);
        if(closing) {
            if(_channel_ticket_push_potentially_cancel(chan, ticket, closing) == false) {
                chan_debug_log("push canceled", ticket);
                return false;
            }
        }

        chan_debug_wait(3);
        if(_channel_id_equals(curr, id))
            break;
            
        if(info.wake) {
            atomic_fetch_or(&chan->ids[target], _CHAN_ID_WAITING_BIT);
            curr |= _CHAN_ID_WAITING_BIT;
        }

        chan_debug_log("push waiting", ticket);
        if(info.wait)
            info.wait((void*) &chan->ids[target], curr, -1);
        else
            chan_pause();
        chan_debug_log("push woken", ticket);
    }
    
    memcpy(chan->items + target*info.item_size, item, info.item_size);
    
    #ifdef CHANNEL_DEBUG
        uint32_t closing = atomic_load(&chan->closing_state);
        if((closing & ~_CHAN_CLOSING_HARD))
        {
            uint64_t new_tail = atomic_load(&chan->tail);
            uint64_t new_head = atomic_load(&chan->head);
            uint64_t barrier = atomic_load(&chan->tail_barrier);
            chan_debug_wait(1);
            
            if((new_head & _CHAN_TICKET_PUSH_CLOSED_BIT) || (new_tail & _CHAN_TICKET_PUSH_CLOSED_BIT))
                ASSERT(channel_ticket_is_less(ticket, barrier));
        }
    #endif
    _channel_advance_id(chan, target, id, info);

    if(out_ticket_or_null)
        *out_ticket_or_null = ticket;

    chan_debug_log("push done", ticket);
    return true;
}

_CHAN_INLINE_NEVER
bool channel_push_int(Channel* chan, const int* item) 
{
    Channel_Info info = {sizeof(int)};
    return channel_ticket_push(chan, item, NULL, info);
}

_CHAN_INLINE_NEVER 
static bool _channel_ticket_pop_potentially_cancel(Channel* chan, uint64_t ticket, uint32_t closing)
{
    bool canceled = false;
    if(closing & _CHAN_CLOSING_HARD)
        canceled = true;
    else
    {
        uint64_t new_head = atomic_load(&chan->head);
        uint64_t barrier = atomic_load(&chan->head_barrier);

        canceled = (new_head & _CHAN_TICKET_POP_CLOSED_BIT) && channel_ticket_is_less_or_eq(barrier, ticket); 
    }

    if(canceled)
    {
        chan_debug_log("push canceled", ticket);
        atomic_fetch_add(&chan->head_cancel_count, _CHAN_TICKET_INCREMENT);
        atomic_fetch_sub(&chan->head, _CHAN_TICKET_INCREMENT);
        return false;
    }
    return true;
}

CHANAPI bool channel_ticket_pop(Channel* chan, void* item, uint64_t* out_ticket_or_null, Channel_Info info) 
{
    ASSERT(memcmp(&chan->info, &info, sizeof info) == 0, "info must be matching");
    ASSERT(item || (item == NULL && info.item_size == 0), "item must be provided");

    uint64_t head = atomic_fetch_add(&chan->head, _CHAN_TICKET_INCREMENT);
    uint64_t ticket = head / _CHAN_TICKET_INCREMENT;
    uint64_t target = _channel_get_target(chan, ticket);
    uint32_t id = _channel_get_id(chan, ticket) + _CHAN_ID_FILLED_BIT;
    chan_debug_log("pop called", ticket);

    for(;;) {
        uint32_t curr = atomic_load(&chan->ids[target]);
        chan_debug_log("pop loaded curr", curr);
        uint32_t closing = atomic_load(&chan->closing_state);
        if(closing) {
            if(_channel_ticket_pop_potentially_cancel(chan, ticket, closing) == false) {
                chan_debug_log("pop canceled", ticket);
                return false;
            }
        }
        
        chan_debug_log("pop loaded closing", closing);
        chan_debug_wait(10);
        if(_channel_id_equals(curr, id))
            break;
        
        if(info.wake) {
            atomic_fetch_or(&chan->ids[target], _CHAN_ID_WAITING_BIT);
            curr |= _CHAN_ID_WAITING_BIT;
        }
        
        chan_debug_log("pop waiting", ticket);
        if(info.wait)
            info.wait((void*) &chan->ids[target], curr, -1);
        else
            chan_pause();
        chan_debug_log("pop woken", ticket);
    }
    
    memcpy(item, chan->items + target*info.item_size, info.item_size);
    
    #ifdef CHANNEL_DEBUG
        uint32_t closing = atomic_load(&chan->closing_state);
        if((closing & ~_CHAN_CLOSING_HARD) != 0)
        {
            uint64_t new_head = atomic_load(&chan->head);
            uint64_t barrier = atomic_load(&chan->head_barrier);

            if(new_head & _CHAN_TICKET_POP_CLOSED_BIT)
                ASSERT(channel_ticket_is_less(ticket, barrier));
        }
        memset(chan->items + target*info.item_size, -1, info.item_size);
    #endif
    _channel_advance_id(chan, target, id, info);
    if(out_ticket_or_null)
        *out_ticket_or_null = ticket;
    
    chan_debug_log("pop done", ticket);
    return true;
}

CHANAPI Channel_Res channel_ticket_try_push_weak(Channel* chan, const void* item, uint64_t* out_ticket_or_null, Channel_Info info) 
{
    ASSERT(memcmp(&chan->info, &info, sizeof info) == 0, "info must be matching");
    ASSERT(item || (item == NULL && info.item_size == 0), "item must be provided");

    uint64_t tail = atomic_load(&chan->tail);
    uint64_t ticket = tail / _CHAN_TICKET_INCREMENT;
    uint64_t target = _channel_get_target(chan, ticket);
    uint32_t id = _channel_get_id(chan, ticket);
    
    chan_debug_wait(3);
    uint32_t curr_id = atomic_load(&chan->ids[target]);
    chan_debug_wait(3);
    uint32_t closing = atomic_load(&chan->closing_state);
    if(closing)
    {
        if(closing & _CHAN_CLOSING_HARD)
            return CHANNEL_CLOSED;
        else
        {
            uint64_t new_tail = atomic_load(&chan->tail);
            chan_debug_wait(10);
            uint64_t new_head = atomic_load(&chan->head);
            chan_debug_wait(10);
            uint64_t barrier = atomic_load(&chan->tail_barrier);

            if((new_head & _CHAN_TICKET_PUSH_CLOSED_BIT) || (new_tail & _CHAN_TICKET_PUSH_CLOSED_BIT))
                if(channel_ticket_is_less_or_eq(barrier, ticket))
                    return CHANNEL_CLOSED;
        }
    }

    if(_channel_id_equals(curr_id, id) == false)
        return CHANNEL_FULL;
        
    chan_debug_wait(3);
    if(atomic_compare_exchange_strong(&chan->tail, &tail, tail+_CHAN_TICKET_INCREMENT) == false)
        return CHANNEL_LOST_RACE;

    memcpy(chan->items + target*info.item_size, item, info.item_size);
    _channel_advance_id(chan, target, id, info);
    if(out_ticket_or_null)
        *out_ticket_or_null = ticket;

    return CHANNEL_OK;
}

CHANAPI Channel_Res channel_ticket_try_pop_weak(Channel* chan, void* item, uint64_t* out_ticket_or_null, Channel_Info info) 
{
    ASSERT(memcmp(&chan->info, &info, sizeof info) == 0, "info must be matching");
    ASSERT(item || (item == NULL && info.item_size == 0), "item must be provided");

    uint64_t head = atomic_load(&chan->head);
    uint64_t ticket = head / _CHAN_TICKET_INCREMENT;
    uint64_t target = _channel_get_target(chan, ticket);
    uint32_t id = _channel_get_id(chan, ticket) + _CHAN_ID_FILLED_BIT;
    
    chan_debug_wait(3);
    uint32_t curr_id = atomic_load(&chan->ids[target]);
    chan_debug_wait(3);
    uint32_t closing = atomic_load(&chan->closing_state);
    if(closing)
    {
        if(closing & _CHAN_CLOSING_HARD)
            return CHANNEL_CLOSED;
        else
        {
            chan_debug_wait(10);
            uint64_t new_head = atomic_load(&chan->head);
            chan_debug_wait(10);
            uint64_t barrier = atomic_load(&chan->head_barrier);

            if((new_head & _CHAN_TICKET_POP_CLOSED_BIT))
                if(channel_ticket_is_less_or_eq(barrier, ticket))
                    return CHANNEL_CLOSED;
        }
    }

    if(_channel_id_equals(curr_id, id) == false)
        return CHANNEL_EMPTY;
        
    chan_debug_wait(3);
    if(atomic_compare_exchange_strong(&chan->head, &head, head+_CHAN_TICKET_INCREMENT) == false)
        return CHANNEL_LOST_RACE;
        
    memcpy(item, chan->items + target*info.item_size, info.item_size);
    #ifdef CHANNEL_DEBUG
        memset(chan->items + target*info.item_size, -1, info.item_size);
    #endif
    _channel_advance_id(chan, target, id, info);
    if(out_ticket_or_null)
        *out_ticket_or_null = ticket;

    return CHANNEL_OK;
}

CHANAPI void _channel_close_lock(Channel* chan, Channel_Info info)
{
    uint32_t ticket = atomic_fetch_add(&chan->closing_lock_requested, 1);
    for(;;) {
        uint32_t curr_completed = atomic_load(&chan->closing_lock_completed);
        if(curr_completed == ticket)
            break;

        if(info.wait)
            info.wait((void*) &chan->closing_lock_completed, curr_completed, -1);
        else
            chan_pause();
    }
}

CHANAPI void _channel_close_unlock(Channel* chan, Channel_Info info)
{
    atomic_fetch_add(&chan->closing_lock_completed, 1);
    if(info.wake)
        info.wake((void*) &chan->closing_lock_completed);
}

CHANAPI void _channel_close_wakeup_ticket_range(Channel* chan, uint64_t from, uint64_t to, Channel_Info info)
{
    chan_debug_log("close waking up range", from, to);
    //no need to iterate any portion twice
    if(channel_ticket_is_less(from + chan->capacity, to))
        to = (from + chan->capacity) % CHANNEL_MAX_TICKET;

    for(uint64_t ticket = from; channel_ticket_is_less(ticket, to); ticket++)
    {
        uint64_t target = _channel_get_target(chan, ticket);
        atomic_fetch_or(&chan->ids[target], _CHAN_ID_CLOSE_NOTIFY_BIT);
        uint32_t id = atomic_load(&chan->ids[target]);
        if(info.wake && id & _CHAN_ID_WAITING_BIT)
        {
            atomic_fetch_and(&chan->ids[target], ~_CHAN_ID_WAITING_BIT);
            chan_debug_log("close waken up", ticket, id);
            info.wake((void*) &chan->ids[target]);
        }
        else
        {
            chan_debug_log("close ored", id, id & ~_CHAN_ID_CLOSE_NOTIFY_BIT);
        }
    }
    chan_debug_log("close waking up range done", from, to);
}

CHANAPI bool _channel_close_soft_custom(Channel* chan, Channel_Info info, bool push_close) 
{
    bool out = false;
    if(channel_is_closed(chan) == false)
    {
        _channel_close_lock(chan, info);
        if(channel_is_closed(chan) == false)
        {
            out = true;
            
            uint64_t tail = 0;
            uint64_t head = 0;
            uint64_t tail_barrier = 0;
            uint64_t head_barrier = 0;

            atomic_fetch_or(&chan->closing_state, _CHAN_CLOSING_PUSH);
            for(;;) {
                tail = atomic_load(&chan->tail);
                head = atomic_load(&chan->head);

                uint64_t barrier_from_head = (head/_CHAN_TICKET_INCREMENT + chan->capacity) % CHANNEL_MAX_TICKET;
                uint64_t barrier_from_tail = tail/_CHAN_TICKET_INCREMENT;

                if(channel_ticket_is_less(barrier_from_head, barrier_from_tail))
                {
                    tail_barrier = barrier_from_head;
                    atomic_store(&chan->tail_barrier, tail_barrier);
                    if(atomic_compare_exchange_weak(&chan->head, &head, head | _CHAN_TICKET_PUSH_CLOSED_BIT))
                    {
                        //since we didnt CAS tail we dont know if it hasnt changed
                        // if it has changed. Thus we need to load it.
                        // - it has changed between the load at the start of the loop and the CAS 
                        //   then: the new load is accurate
                        // - it has changed between the CAS and this load 
                        //   then: the change must have been a result of backing off (thus is LESS)
                        //         because of this we also keep count of the number of backoffs
                        //         and add it to get upper estimate on the tail at the time of CAS.
                        chan_debug_wait(20); 
                        uint64_t tail_after_backoff = atomic_load(&chan->tail);
                        chan_debug_wait(10);
                        uint64_t tail_backed_off_count = atomic_load(&chan->tail_cancel_count);
                        tail = (tail_after_backoff + tail_backed_off_count) % CHANNEL_MAX_TICKET;
                        break;
                    }
                }
                else
                {
                    tail_barrier = barrier_from_tail;
                    atomic_store(&chan->tail_barrier, tail_barrier);
                    if(atomic_compare_exchange_weak(&chan->tail, &tail, tail | _CHAN_TICKET_PUSH_CLOSED_BIT))
                        break;
                }
            }
            
            chan_debug_log("_channel_close_soft tail_barrier", tail_barrier, tail/_CHAN_TICKET_INCREMENT);

            chan_debug_wait(10);
            atomic_fetch_or(&chan->closing_state, _CHAN_CLOSING_POP);
            if(push_close)
            {
                head_barrier = tail_barrier;
                atomic_store(&chan->head_barrier, head_barrier);
                head = atomic_fetch_or(&chan->head, _CHAN_TICKET_POP_CLOSED_BIT);
            }
            else
            {
                for(;;) {
                    head = atomic_load(&chan->head);
                
                    uint64_t barrier_from_head = head/_CHAN_TICKET_INCREMENT;  // owo
                    uint64_t barrier_from_tail = tail_barrier;
                
                    head_barrier = channel_ticket_is_less(barrier_from_head, barrier_from_tail) ? barrier_from_head : barrier_from_tail;
                    atomic_store(&chan->head_barrier, head_barrier);
                    if(atomic_compare_exchange_weak(&chan->head, &head, head | _CHAN_TICKET_POP_CLOSED_BIT))
                        break;
                }
            }

            uint64_t head_ticket = head/_CHAN_TICKET_INCREMENT;
            uint64_t tail_ticket = tail/_CHAN_TICKET_INCREMENT;

            bool limited = channel_ticket_is_less(tail_barrier, tail_ticket);
            ASSERT(channel_ticket_is_less_or_eq(head_barrier, tail_barrier));
            ASSERT(channel_ticket_is_less_or_eq(tail_barrier, tail_ticket));
            if(push_close == false)
                ASSERT(channel_ticket_is_less_or_eq(head_barrier, head_ticket));

            chan_debug_log("_channel_close_soft head_barrier", head_barrier, head_ticket);
            chan_debug_log("_channel_close_soft limiting", (uint64_t) limited, (uint64_t) chan->capacity);

            _channel_close_wakeup_ticket_range(chan, head_barrier, head_ticket, info);
            _channel_close_wakeup_ticket_range(chan, tail_barrier, tail_ticket, info);
            
            atomic_fetch_or(&chan->closing_state, _CHAN_CLOSING_CLOSED);
        }
        _channel_close_unlock(chan, info);
    }

    return out;
}

CHANAPI bool channel_close_soft(Channel* chan, Channel_Info info) 
{
    chan_debug_log("channel_close_soft called");
    bool out = _channel_close_soft_custom(chan, info, false);
    chan_debug_log("channel_close_soft done");
    return out;
}

CHANAPI bool channel_close_push(Channel* chan, Channel_Info info) 
{
    chan_debug_log("channel_close_push called");
    bool out = _channel_close_soft_custom(chan, info, true);
    chan_debug_log("channel_close_push done");
    return out;
}

CHANAPI bool channel_is_invariant_converged_state(Channel* chan, Channel_Info info) 
{
    (void) info;
    bool out = true;
    if(channel_is_hard_closed(chan) == false)
    {
        uint64_t tail_and_closed = atomic_load(&chan->tail);
        uint64_t head_and_closed = atomic_load(&chan->head);

        uint64_t tail = tail_and_closed/_CHAN_TICKET_INCREMENT;
        uint64_t head = head_and_closed/_CHAN_TICKET_INCREMENT;
        
        uint64_t tail_barrier = atomic_load(&chan->tail_barrier);
        uint64_t head_barrier = atomic_load(&chan->head_barrier);

        uint32_t closing = atomic_load(&chan->closing_state);
        if(closing & _CHAN_CLOSING_CLOSED)
        {
            int64_t dist_barrier = (int64_t)(tail_barrier - head_barrier);
            out = out && 0 <= dist_barrier && dist_barrier <= chan->capacity;
            out = out && ((tail_and_closed & _CHAN_TICKET_PUSH_CLOSED_BIT) || (head_and_closed & _CHAN_TICKET_PUSH_CLOSED_BIT));
            out = out && (head_and_closed & _CHAN_TICKET_POP_CLOSED_BIT);
        }
        else
        {
            out = out && tail*_CHAN_TICKET_INCREMENT == tail_and_closed;
            out = out && head*_CHAN_TICKET_INCREMENT == head_and_closed;
            out = out && tail_barrier == 0;
            out = out && head_barrier == 0;
        }
        //ASSERT(out);

        uint64_t head_p_cap = (head + chan->capacity) % CHANNEL_MAX_TICKET;
        uint64_t max_filled = channel_ticket_is_less(tail, head_p_cap) ? tail : head_p_cap;

        for(uint64_t ticket = head; channel_ticket_is_less(ticket, max_filled); ticket ++)
        {
            uint64_t target = _channel_get_target(chan, ticket);
            uint32_t id = _channel_get_id(chan, ticket) + _CHAN_ID_FILLED_BIT;
            uint32_t curr_id = chan->ids[target];
            out = out && _channel_id_equals(curr_id, id);
        }

        for(uint64_t ticket = max_filled; channel_ticket_is_less(ticket, head_p_cap); ticket ++)
        {
            uint64_t target = _channel_get_target(chan, ticket);
            uint32_t id = _channel_get_id(chan, ticket);
            uint32_t curr_id = chan->ids[target];
            out = out && _channel_id_equals(curr_id, id);
                
            #ifdef CHANNEL_DEBUG
            uint8_t* item = chan->items + target*info.item_size;
            bool is_empty_invariant = true;
            for(isize i = 0; i < info.item_size; i++)
                is_empty_invariant = is_empty_invariant && item[i] == (uint8_t) -1;

            out = out && is_empty_invariant;
            //ASSERT(is_empty_invariant);
            #endif
        }
    }

    return out;
}

CHANAPI bool channel_reopen(Channel* chan, Channel_Info info) 
{
    ASSERT(memcmp(&chan->info, &info, sizeof info) == 0, "info must be matching");

    chan_debug_log("channel_reopen called");
    bool out = false;
    if(channel_is_closed(chan))
    {
        _channel_close_lock(chan, info);
        if(channel_is_closed(chan) && channel_is_hard_closed(chan) == false)
        {
            chan_debug_log("channel_reopen lock start");
            atomic_store(&chan->closing_state, 0);
            for(isize i = 0; i < chan->capacity; i++)
                atomic_fetch_and(&chan->ids[i], ~_CHAN_ID_CLOSE_NOTIFY_BIT);

            atomic_fetch_and(&chan->head, ~(_CHAN_TICKET_PUSH_CLOSED_BIT | _CHAN_TICKET_POP_CLOSED_BIT));
            atomic_fetch_and(&chan->tail, ~(_CHAN_TICKET_PUSH_CLOSED_BIT | _CHAN_TICKET_POP_CLOSED_BIT));
            atomic_store(&chan->head_barrier, 0);
            atomic_store(&chan->head_cancel_count, 0);
            atomic_store(&chan->tail_barrier, 0);
            atomic_store(&chan->tail_cancel_count, 0);

            out = true;
            chan_debug_log("channel_reopen lock end");
        }
        _channel_close_unlock(chan, info);
    }
    chan_debug_log("channel_reopen done");
    return out;
}

CHANAPI bool channel_close_hard(Channel* chan, Channel_Info info)
{
    ASSERT(memcmp(&chan->info, &info, sizeof info) == 0, "info must be matching");
    
    chan_debug_log("channel_close_hard called");
    bool out = (atomic_fetch_or(&chan->closing_state, _CHAN_CLOSING_HARD) & _CHAN_CLOSING_HARD) == 0;
    chan_debug_log("channel_close_hard done");
    return out;
}

CHANAPI Channel_Res channel_ticket_try_push(Channel* chan, const void* item, uint64_t* out_ticket_or_null, Channel_Info info)
{
    for(;;) {
        Channel_Res res = channel_ticket_try_push_weak(chan, item, out_ticket_or_null, info);
        if(res != CHANNEL_LOST_RACE)
            return res;
    }
}

CHANAPI Channel_Res channel_ticket_try_pop(Channel* chan, void* item, uint64_t* out_ticket_or_null, Channel_Info info)
{
    for(;;) {
        Channel_Res res = channel_ticket_try_pop_weak(chan, item, out_ticket_or_null, info);
        if(res != CHANNEL_LOST_RACE)
            return res;
    }
}

CHANAPI bool channel_push(Channel* chan, const void* item, Channel_Info info)
{
    return channel_ticket_push(chan, item, NULL, info);
}
CHANAPI bool channel_pop(Channel* chan, void* item, Channel_Info info)
{
    return channel_ticket_pop(chan, item, NULL, info);
}
CHANAPI Channel_Res channel_try_push_weak(Channel* chan, const void* item, Channel_Info info)
{
    return channel_ticket_try_push_weak(chan, item, NULL, info);
}
CHANAPI Channel_Res channel_try_pop_weak(Channel* chan, void* item, Channel_Info info)
{
    return channel_ticket_try_pop_weak(chan, item, NULL, info);
}
CHANAPI Channel_Res channel_try_push(Channel* chan, const void* item, Channel_Info info)
{
    return channel_ticket_try_push(chan, item, NULL, info);
}
CHANAPI Channel_Res channel_try_pop(Channel* chan, void* item, Channel_Info info)
{
    return channel_ticket_try_pop(chan, item, NULL, info);
}

CHANAPI isize channel_signed_distance(const Channel* chan)
{
    uint64_t head = atomic_load(&chan->head);
    uint64_t tail = atomic_load(&chan->tail);

    uint64_t diff = tail/_CHAN_TICKET_INCREMENT - head/_CHAN_TICKET_INCREMENT;
    return (isize) diff;
}

CHANAPI isize channel_count(const Channel* chan) 
{
    isize dist = channel_signed_distance(chan);
    if(dist <= 0)
        return 0;
    if(dist >= chan->capacity)
        return chan->capacity;
    else
        return dist;
}

CHANAPI bool channel_is_empty(const Channel* chan) 
{
    return channel_signed_distance(chan) <= 0;
}

CHANAPI bool channel_is_closed(const Channel* chan) 
{
    uint32_t closing = atomic_load(&chan->closing_state);
    return closing != 0;
}

CHANAPI bool channel_is_hard_closed(const Channel* chan) 
{
    uint32_t closing = atomic_load(&chan->closing_state);
    return (closing & _CHAN_CLOSING_HARD) != 0;
}

CHANAPI bool channel_ticket_is_less(uint64_t ticket_a, uint64_t ticket_b)
{
    uint64_t diff = ticket_a - ticket_b;
    int64_t signed_diff = (int64_t) diff; 
    return signed_diff < 0;
}

CHANAPI bool channel_ticket_is_less_or_eq(uint64_t ticket_a, uint64_t ticket_b)
{
    uint64_t diff = ticket_a - ticket_b;
    int64_t signed_diff = (int64_t) diff; 
    return signed_diff <= 0;
}

CHANAPI void channel_init(Channel* chan, void* items, uint32_t* ids, isize capacity, Channel_Info info)
{
    ASSERT(ids);
    ASSERT(capacity > 0 && "must be nonzero");
    ASSERT(items != NULL || (items == NULL && info.item_size == 0));
;
    memset(chan, 0, sizeof* chan);
    chan->items = (uint8_t*) items;
    chan->ids = (CHAN_ATOMIC(uint32_t)*) (void*) ids;
    chan->capacity = capacity; 
    chan->info = info;
    chan->ref_count = 1;

    memset(ids, 0, (size_t) capacity*sizeof *ids);
    #ifdef CHANNEL_DEBUG
        memset(items, -1, (size_t) capacity*info.item_size);
    #endif

    //essentially a memory fence with respect to any other function
    atomic_store(&chan->head, 0);
    atomic_store(&chan->tail, 0);
    atomic_store(&chan->closing_state, 0);
}

CHANAPI isize channel_memory_size(isize capacity, Channel_Info info)
{
    return sizeof(Channel) + capacity*sizeof(uint32_t) + capacity*info.item_size;
}

CHANAPI Channel* channel_init_into_memory(void* aligned_memory, isize capacity, Channel_Info info)
{
    Channel* chan = (Channel*) aligned_memory;
    if(chan)
    {
        uint32_t* ids = (uint32_t*) (void*) (chan + 1);
        void* items = ids + capacity;

        channel_init(chan, items, ids, capacity, info);
        atomic_store(&chan->allocated, true);
    }
    return chan;
}

#ifdef _MSC_VER
    #define chan_aligned_alloc(size, align) _aligned_malloc((size), (align))
    #define chan_aligned_free _aligned_free
#else
    #define chan_aligned_alloc(size, align) aligned_alloc((align), (size))
    #define chan_aligned_free free
#endif

CHANAPI Channel* channel_malloc(isize capacity, Channel_Info info)
{
    isize total_size = channel_memory_size(capacity, info);
    void* mem = chan_aligned_alloc(total_size, CHAN_CACHE_LINE);
    return channel_init_into_memory(mem, capacity, info);
}

CHANAPI Channel* channel_share(Channel* chan)
{
    if(chan != NULL)
        atomic_fetch_add(&chan->ref_count, 1);
    return chan;
}

CHANAPI int32_t channel_deinit(Channel* chan)
{
    if(chan == NULL)
        return 0;

    int32_t refs = (int32_t) atomic_fetch_sub(&chan->ref_count, 1) - 1;
    if(refs == 0)
    {
        if(atomic_load(&chan->allocated))
            chan_aligned_free(chan);
        else
            memset(chan, 0, sizeof *chan);
    }

    return refs;
}

CHANAPI bool chan_wait_yield(volatile void* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
{
    (void) state; (void) undesired; (void) timeout_or_negatove_if_infinite;
    chan_yield();
    return true;
}

CHANAPI bool chan_wait_block(volatile void* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
{
    return chan_futex_wait((uint32_t*) state, undesired, timeout_or_negatove_if_infinite);
}

CHANAPI void chan_wake_block(volatile void* state)
{
    chan_futex_wake_all((uint32_t*) state);
}

//ARCH DETECTION
#define CHAN_ARCH_UNKNOWN   0
#define CHAN_ARCH_X86       1
#define CHAN_ARCH_X64       2
#define CHAN_ARCH_ARM32     3
#define CHAN_ARCH_ARM64     4

#ifndef CHAN_ARCH
    #if defined(_M_CEE_PURE) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
        #define CHAN_ARCH CHAN_ARCH_X86
    #elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__) && !defined(_M_ARM64EC) 
        #define CHAN_ARCH CHAN_ARCH_X64
    #elif defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__ARM_ARCH_ISA_A64)
        #define CHAN_ARCH CHAN_ARCH_ARM64
    #elif defined(_M_ARM32) || defined(_M_ARM32EC) || defined(__arm__) || defined(__ARM_ARCH)
        #define CHAN_ARCH CHAN_ARCH_ARM32
    #else
        #define CHAN_ARCH CHAN_ARCH_UNKNOWN
    #endif
#endif

//OS DETECTION
#define CHAN_OS_UNKNOWN     0 
#define CHAN_OS_WINDOWS     1
#define CHAN_OS_UNIX        2
#define CHAN_OS_APPLE_OSX   3

#if !defined(CHAN_OS)
    #undef CHAN_OS
    #if defined(_WIN32) || defined(_WIN64)
        #define CHAN_OS CHAN_OS_WINDOWS // Windows
    #elif defined(__linux__)
        #define CHAN_OS CHAN_OS_UNIX // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
    #elif defined(__APPLE__) && defined(__MACH__) // Apple OSX and iOS (Darwin)
        #define CHAN_OS CHAN_OS_APPLE_OSX
    #else
        #define CHAN_OS CHAN_OS_UNKNOWN
    #endif
#endif 

//Puase
#ifndef CHAN_CUSTOM_PAUSE
    #ifdef _MSC_VER
        #include <intrin.h>
        #if CHAN_ARCH == CHAN_ARCH_X86 || CHAN_ARCH == CHAN_ARCH_X64
            #define _CHAN_PAUSE_IMPL() _mm_pause()
        #elif CHAN_ARCH == CHAN_ARCH_ARM
            #define _CHAN_PAUSE_IMPL() __yield()
        #endif
    #elif defined(__GNUC__) || defined(__clang__) 
        #if CHAN_ARCH == CHAN_ARCH_X86 || CHAN_ARCH == CHAN_ARCH_X64
            #include <x86intrin.h>
            #define _CHAN_PAUSE_IMPL() _mm_pause()
        #elif CHAN_ARCH == CHAN_ARCH_ARM64
            #define _CHAN_PAUSE_IMPL() asm volatile("yield")
        #endif
    #endif

    CHAN_INTRINSIC void chan_pause() { 
        #ifdef _CHAN_PAUSE_IMPL
            _CHAN_PAUSE_IMPL();
        #endif
    } 
#endif

#if CHAN_OS == CHAN_OS_WINDOWS
    #pragma comment(lib, "synchronization.lib")

    //Instead of including windows.h we 
    typedef int BOOL;
    typedef unsigned long DWORD;
    void __stdcall WakeByAddressSingle(void*);
    void __stdcall WakeByAddressAll(void*);
    BOOL __stdcall WaitOnAddress(volatile void* Address, void* CompareAddress, size_t AddressSize, DWORD dwMilliseconds);
    BOOL __stdcall SwitchToThread(void);
    void __stdcall Sleep(DWORD);
    
    CHAN_OS_API void chan_futex_wake_all(volatile uint32_t* state) {
        WakeByAddressAll((void*) state);
    }
    
    CHAN_OS_API void chan_futex_wake_single(volatile uint32_t* state) {
        WakeByAddressSingle((void*) state);
    }
    
    CHAN_OS_API bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
    {
        DWORD wait = 0;
        if(timeout_or_negatove_if_infinite < 0)
            wait = (DWORD) -1; //INFINITE
        else
            wait = (DWORD) (timeout_or_negatove_if_infinite*1000);

        bool value_changed = (bool) WaitOnAddress(state, &undesired, sizeof undesired, wait);
        if(!value_changed)
            chan_debug_log("futex timed out", value_changed);
        return value_changed;
    }
    
    CHAN_OS_API void chan_yield() {
        SwitchToThread();
    }

    CHAN_OS_API void chan_sleep(double seconds)
    {
        if(seconds >= 0)
            Sleep((DWORD)(seconds * 1000));
    }
    
    typedef int BOOL;
    typedef unsigned long DWORD;
    typedef union _LARGE_INTEGER LARGE_INTEGER;
    BOOL __stdcall QueryPerformanceCounter(LARGE_INTEGER* ticks);
    BOOL __stdcall QueryPerformanceFrequency(LARGE_INTEGER* ticks);
    
    CHAN_OS_API int64_t chan_perf_counter()
    {
        int64_t ticks = 0;
        (void) QueryPerformanceCounter((LARGE_INTEGER*) (void*)  &ticks);
        return ticks;
    }

    CHAN_OS_API int64_t chan_perf_frequency()
    {
        int64_t ticks = 0;
        (void) QueryPerformanceFrequency((LARGE_INTEGER*) (void*) &ticks);
        return ticks;
    }

    uintptr_t _beginthread(
        void( __cdecl *start_address )( void * ),
        unsigned stack_size,
        void *arglist
    );

    CHAN_OS_API bool chan_start_thread(void (*func)(void* context), void* context)
    {
        return _beginthread(func, 0, context) != 0;
    }

#elif CHAN_OS == CHAN_OS_UNIX
    #include <linux/futex.h> 
    #include <sys/syscall.h> 
    #include <unistd.h>
    #include <sched.h>
    #include <errno.h>

    CHAN_OS_API void chan_futex_wake_all(volatile uint32_t* state) {
        syscall(SYS_futex, (void*) state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX, NULL, NULL, 0);
    }
    
    CHAN_OS_API void chan_futex_wake_single(volatile uint32_t* state) {
        syscall(SYS_futex, (void*) state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0);
    }
    
    CHAN_OS_API bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
    {
        struct timespec tm = {0};
        struct timespec* tm_ptr = NULL;
        if(timeout_or_negatove_if_infinite >= 0)
        {
            int64_t nanosecs = (int64_t) (timeout_or_negatove_if_infinite*1000000000LL);
            tm.tv_sec = nanosecs / 1000000000LL; 
            tm.tv_nsec = nanosecs % 1000000000LL; 
            tm_ptr = &tm;
        }
        long ret = syscall(SYS_futex, (void*) state, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, undesired, tm_ptr, NULL, 0);
        if (ret == -1 && errno == ETIMEDOUT) 
            return false;
        return true;
    }

#elif CHAN_OS == CHAN_OS_APPLE_OSX
    #error Add OSX support. The following is just a sketch that probably does not even compile (missing headers). \
         I do not have a OSX machine so testing this code is difficult

    //Taken from: https://github.com/colrdavidson/Odin/blob/auto_tracing/src/spall_native_auto.h#L575
    // and from: https://outerproduct.net/futex-dictionary.html#macos
    int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);
    int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);

    #define UL_COMPARE_AND_WAIT		1
    #define ULF_WAKE_ALL			0x00000100
    #define ULF_NO_ERRNO			0x01000000

    CHAN_OS_API void chan_futex_wake_all(volatile uint32_t* state) {
        __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL | ULF_NO_ERRNO, state, 0);
    }
    
    CHAN_OS_API void chan_futex_wake_single(volatile uint32_t* state) {
        __ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, state, 0);
    }
    
    CHAN_OS_API bool chan_futex_wait(volatile uint32_t* state, uint32_t undesired, double timeout_or_negatove_if_infinite)
    {
        uint32_t timeout = 0;
        if(timeout_or_negatove_if_infinite >= 0)
        {
            uint64_t microsecs = (uint64_t) (timeout_or_negatove_if_infinite*1000000LL);
            if(microsecs == 0)
                timeout = 1;
            else if (microsecs > UINT32_MAX)
                timeout = UINT32_MAX;
            else
                microsecs = (uint32_t) microsecs;
        }

        int ret = __ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, state, undesired, timeout);
        return ret >= 0;
    }
#endif

#if CHAN_OS == CHAN_OS_APPLE_OSX || CHAN_OS == CHAN_OS_UNIX
    #include <unistd.h>
    #include <sched.h>
    CHAN_OS_API void chan_yield()
    {
        sched_yield();
    }
    
    #include <time.h>
    CHAN_OS_API void chan_sleep(double seconds)
    {
        if(seconds > 0)
        {
            uint64_t nanosecs = (uint64_t) (seconds*1000000000LL);
            struct timespec ts = {0};
            ts.tv_sec = nanosecs / 1000000000LL; 
            ts.tv_nsec = nanosecs % 1000000000LL; 

            while(nanosleep(&ts, &ts) == -1);
        }
    }

    CHAN_OS_API int64_t chan_perf_counter()
    {
        struct timespec ts = {0};
        (void) clock_gettime(CLOCK_MONOTONIC_RAW , &ts);
        return (int64_t) ts.tv_nsec + ts.tv_sec * 1000000000LL;
    }
    
    CHAN_OS_API int64_t chan_perf_frequency()
    {
	    return (int64_t) 1000000000LL;
    }
    
    #include <pthread.h>
    CHAN_OS_API void* _chan_thread_func(void* func_and_context)
    {
        typedef void (*Void_Func)(void* context);

        Void_Func func = (Void_Func) ((void**) func_and_context)[0];
        void* context =              ((void**) func_and_context)[1];
        func(context);
        free(func_and_context);
        return NULL;
    }

    CHAN_OS_API bool chan_start_thread(void (*func)(void* context), void* context)
    {
        int error = 1;
        void** func_and_context = (void**) malloc(sizeof(void*)*2);
        if(func_and_context)
        {
            func_and_context[0] = func;
            func_and_context[1] = context;

            pthread_t handle = {0};
            pthread_attr_t attr = {0};
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            error = pthread_create(&handle, &attr, _chan_thread_func, func_and_context);
        }

        if(error)
            free(func_and_context);
        
        return error == 0;
    }
#endif
#endif