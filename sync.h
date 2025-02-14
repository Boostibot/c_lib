#include "channel.h"

//TODO SIMPLIFY AND ALSO ISOLATE

//==========================================================================
// Wait free list 
//==========================================================================
// A simple growing stack where no ABA problem can occur. 
// Before call to sync_list_push() the "pusher" thread has exclusive ownership over the node. 
// After push it must no longer touch it.
// Once a node is popped using sync_list_pop_all() the "popper" thread has exclusive ownership of that node
//  and can even dealloc the node without any risk of use after free.
#define sync_list_push(head_ptr_ptr, node_ptr) sync_list_push_chain(head_ptr_ptr, node_ptr, node_ptr)
#define sync_list_pop_all(head_ptr_ptr) atomic_exchange((head_ptr_ptr), NULL)
#define sync_list_push_chain(head_ptr_ptr, first_node_ptr, last_node_ptr)                   \
    for(;;) {                                                                               \
        CHAN_ATOMIC(void*)* __head = (void*) (head_ptr_ptr);                                \
        CHAN_ATOMIC(void*)* __last_next = (void*) &(last_node_ptr)->next;                   \
                                                                                            \
        void* __curr = atomic_load(__head);                                                 \
        atomic_store(__last_next, __curr);                                                  \
        if(atomic_compare_exchange_weak(__head, &__curr, (void*) (first_node_ptr)))         \
            break;                                                                          \
    }                                                                                       \
    
//==========================================================================
// Wait/Wake helpers
//==========================================================================
// Provide helpers for waiting for certain condition. 
// This can be used to implement wait groups, semaphores and much more.
// Alternatively there is also timed version which gives up after certain 
// amount of time and returns failure (false).
typedef struct Sync_Wait {
    Sync_Wait_Func wait;
    Sync_Wake_Func wake;
    uint32_t notify_bit;
    uint32_t _;
} Sync_Wait;

#ifdef __cplusplus
    #define _CHAN_SINIT(T) T
#else
    #define _CHAN_SINIT(T) (T)
#endif
#define SYNC_WAIT_BLOCK          _CHAN_SINIT(Sync_Wait){chan_wait_block, chan_wake_block}
#define SYNC_WAIT_YIELD          _CHAN_SINIT(Sync_Wait){chan_wait_yield}
#define SYNC_WAIT_SPIN           _CHAN_SINIT(Sync_Wait){}
#define SYNC_WAIT_BLOCK_BIT(bit) _CHAN_SINIT(Sync_Wait){chan_wait_block, chan_wake_block, 1u << bit}

CHANAPI bool sync_wait(volatile void* state, uint32_t current, isize timeout, Sync_Wait wait);
CHANAPI void sync_wake(volatile void* state, uint32_t prev, Sync_Wait wait);
CHANAPI void sync_set_and_wake(volatile void* state, uint32_t to, Sync_Wait wait);
CHANAPI uint32_t sync_wait_for_equal(volatile void* state, uint32_t desired, Sync_Wait wait);
CHANAPI uint32_t sync_wait_for_not_equal(volatile void* state, uint32_t desired, Sync_Wait wait);
CHANAPI uint32_t sync_wait_for_smaller(volatile void* state, uint32_t desired, Sync_Wait wait);
CHANAPI uint32_t sync_wait_for_greater(volatile void* state, uint32_t desired, Sync_Wait wait);

//sync_wait_for(&state, condition_func, wait)

typedef struct Sync_Timed_Wait {
    double freq_s;
    isize wait_ticks;
    isize start_ticks;
} Sync_Timed_Wait;

CHANAPI Sync_Timed_Wait sync_timed_wait_start(double wait);
CHANAPI bool sync_timed_wait(volatile void* state, uint32_t current, Sync_Timed_Wait timeout, Sync_Wait wait);
CHANAPI bool sync_timed_wait_for_equal(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait);
CHANAPI bool sync_timed_wait_for_not_equal(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait);
CHANAPI bool sync_timed_wait_for_smaller(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait);
CHANAPI bool sync_timed_wait_for_greater(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait);

//==========================================================================
// Wait Group
//==========================================================================
// A simple counter that allows incrementing and decrementing (push/pop) and waiting
// for it to hit zero. Is based on Go's wait groups and can be used in exactly the same way.
// This behaviour could be also achieved by using sync_wait_for_equal() and similar at a cost 
// of potentially more wakeups. This implementation guarantees that the waiting thread will 
// get woken up exactly once. 
// The wake field is incremented every time the count crosses to or below zero, preventing
// the ABA problem.
typedef union Wait_Group {
    CHAN_ATOMIC(uint64_t) combined;
    struct {
        CHAN_ATOMIC(int32_t) atomic_count;
        CHAN_ATOMIC(uint32_t) atomic_wakes;
    };
    struct {
        int32_t count;
        uint32_t wakes;
    };
} Wait_Group; 

CHANAPI int32_t wait_group_count(volatile Wait_Group* wg);
CHANAPI Wait_Group* wait_group_push(volatile Wait_Group* wg, isize count);
CHANAPI bool wait_group_pop(volatile Wait_Group* wg, isize count, Sync_Wait wait);
CHANAPI void wait_group_wait(volatile Wait_Group* wg, Sync_Wait wait);
CHANAPI bool wait_group_wait_timed(volatile Wait_Group* wg, double timeout, Sync_Wait wait);

//==========================================================================
// Once
//==========================================================================
//Calls the provided function exactly once. Can also be used like
// static Sync_Once once = 0;
// if(sync_once_begin(&once)) {
//   //init code here...
//   sync_once_end(&once);
// }
typedef CHAN_ATOMIC(uint32_t) Sync_Once;
enum {
    SYNC_ONCE_UNINIT = 0,
    SYNC_ONCE_INIT = 1,
    SYNC_ONCE_INITIALIZING = 2,
};

CHANAPI bool sync_once(volatile Sync_Once* once, void(*func)(void* context), void* context, Sync_Wait wait);
CHANAPI bool sync_once_begin(volatile Sync_Once* once, Sync_Wait wait);
CHANAPI void sync_once_end(volatile Sync_Once* once, Sync_Wait wait);


typedef union Ticket_Lock {
    struct {
        uint32_t requested;
        uint32_t completed;
    };
    struct {
        CHAN_ATOMIC(uint32_t) atomic_requested;
        CHAN_ATOMIC(uint32_t) atomic_completed;
    };

    uint64_t combined;
    CHAN_ATOMIC(uint64_t) atomic_combined;
} Ticket_Lock;

CHANAPI void ticket_lock(Ticket_Lock* lock, Sync_Wait wait);
CHANAPI void ticket_unlock(Ticket_Lock* lock, Sync_Wait wait);

CHANAPI void ticket_lock(Ticket_Lock* lock, Sync_Wait wait)
{
    uint32_t ticket = atomic_fetch_add(&lock->atomic_requested, 1);
    for(;;) {
        uint32_t curr_completed = atomic_load(&lock->atomic_completed);
        if(curr_completed == ticket)
            break;

        if(wait.wait)
            wait.wait((void*) &lock->atomic_completed, curr_completed, -1);
        else
            chan_pause();
    }
}

CHANAPI void ticket_unlock(Ticket_Lock* lock, Sync_Wait wait)
{
    atomic_fetch_add(&lock->atomic_completed, 1);
    if(wait.wake)
        wait.wake((void*) &lock->atomic_completed);
}


#if 0
CHANAPI bool sync_wait(volatile void* state, uint32_t current, isize timeout, Sync_Wait wait)
{
    if(wait.notify_bit) 
    {
        atomic_fetch_or(state, wait.notify_bit);
        current |= wait.notify_bit;
    }

    if(wait.wait)
        return wait.wait((void*) state, current, timeout);
    else
    {
        chan_pause();
        return true;
    }
}

CHANAPI void sync_wake(volatile void* state, uint32_t prev, Sync_Wait wait)
{
    if(wait.wake)
    {
        if(wait.notify_bit)
        {
            if(prev & wait.notify_bit)
                wait.wake((void*) state);
        }
        else
            wait.wake((void*) state);
    }
}

CHANAPI void sync_set_and_wake(volatile void* state, uint32_t to, Sync_Wait wait)
{
    if(wait.wake == NULL)
        atomic_store(state, to);
    else
    {
        uint32_t prev = atomic_exchange(state, to);
        sync_wake(state, prev, wait);
    }
}

CHANAPI uint32_t sync_wait_for_equal(volatile void* state, uint32_t desired, Sync_Wait wait)
{
    for(;;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) == (desired & ~wait.notify_bit))
            return current & ~wait.notify_bit;

        sync_wait(state, current, -1, wait);
    }
}

CHANAPI uint32_t sync_wait_for_not_equal(volatile void* state, uint32_t desired, Sync_Wait wait)
{
    for(;;) {
        uint32_t current = atomic_load(state);
        chan_debug_log("wait neq", current);
        if((current & ~wait.notify_bit) != (desired & ~wait.notify_bit))
        {
            chan_debug_log("wait neq done", desired);
            return current & ~wait.notify_bit;
        }
        
        chan_debug_log("wait neq still waiting", current);
        sync_wait(state, current, -1, wait);
    }
}

CHANAPI uint32_t sync_wait_for_smaller(volatile void* state, uint32_t desired, Sync_Wait wait)
{
    for(;;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) < (desired & ~wait.notify_bit))
            return current & ~wait.notify_bit;
            
        sync_wait(state, current, -1, wait);
    }
}

#define SYNC_WAIT_FOR(state, cond_, wait)
    

CHANAPI uint32_t sync_wait_for_greater(volatile void* state, uint32_t desired, Sync_Wait wait)
{
    for(;;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) > (desired & ~wait.notify_bit))
            return current & ~wait.notify_bit;
            
        sync_wait(state, current, -1, wait);
    }
}

CHANAPI Sync_Timed_Wait sync_timed_wait_start(double wait)
{
    static double freq_s = 0;
    if(freq_s == 0)
        freq_s = (double) chan_perf_frequency();

    Sync_Timed_Wait out = {0};
    out.freq_s = freq_s;
    out.wait_ticks = (int64_t) (wait*freq_s);
    out.start_ticks = chan_perf_counter();
    return out;
}

CHANAPI bool sync_timed_wait(volatile void* state, uint32_t current, Sync_Timed_Wait timeout, Sync_Wait wait)
{
    int64_t curr_ticks = chan_perf_counter();
    int64_t ellapsed_ticks = curr_ticks - timeout.start_ticks;
    if(ellapsed_ticks >= timeout.wait_ticks)
        return false;
        
    double wait_s = (double) (timeout.wait_ticks - ellapsed_ticks)/timeout.freq_us;
    return sync_wait(state, current, wait_s, wait);
}

CHANAPI bool sync_timed_wait_for_equal(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait)
{
    for(Sync_Timed_Wait timed_wait = sync_timed_wait_start(timeout);;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) == (desired & ~wait.notify_bit))
            return true;

        if(sync_timed_wait(state, current, timed_wait, wait) == false)
            return false;
    }
}

CHANAPI bool sync_timed_wait_for_not_equal(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait)
{
    for(Sync_Timed_Wait timed_wait = sync_timed_wait_start(timeout);;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) != (desired & ~wait.notify_bit))
            return true;

        if(sync_timed_wait(state, current, timed_wait, wait) == false)
            return false;
    }
}

CHANAPI bool sync_timed_wait_for_smaller(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait)
{
    for(Sync_Timed_Wait timed_wait = sync_timed_wait_start(timeout);;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) < (desired & ~wait.notify_bit))
            return true;

        if(sync_timed_wait(state, current, timed_wait, wait) == false)
            return false;
    }
}

CHANAPI bool sync_timed_wait_for_greater(volatile void* state, uint32_t desired, double timeout, Sync_Wait wait)
{
    for(Sync_Timed_Wait timed_wait = sync_timed_wait_start(timeout);;) {
        uint32_t current = atomic_load(state);
        if((current & ~wait.notify_bit) > (desired & ~wait.notify_bit))
            return true;

        if(sync_timed_wait(state, current, timed_wait, wait) == false)
            return false;
    }
}
#endif

CHANAPI bool sync_once_begin(volatile Sync_Once* once, Sync_Wait wait)
{
    uint32_t before_value = atomic_load(once);
    if(before_value != SYNC_ONCE_INIT)
        return false;

    uint32_t curr_val = SYNC_ONCE_UNINIT;
    if(atomic_compare_exchange_strong(once, &curr_val, SYNC_ONCE_INITIALIZING))
        return true;
    else
    {
        for(;;) {
            uint32_t current = atomic_load(once);
            if(current == SYNC_ONCE_INIT)
                break;

            if(wait.wait)
                return wait.wait((void*) once, current, -1);
            else
                chan_pause();
        }
    }
    return false;
}
CHANAPI void sync_once_end(volatile Sync_Once* once, Sync_Wait wait)
{
    atomic_store(once, SYNC_ONCE_INIT);
    if(wait.wake)
        wait.wake((void*) once);
}
CHANAPI bool sync_once(volatile Sync_Once* once, void(*func)(void* context), void* context, Sync_Wait wait)
{
    if(sync_once_begin(once, wait))
    {
        func(context);
        sync_once_end(once, wait);
        return true;
    }
    return false;
}

CHANAPI int32_t wait_group_count(volatile Wait_Group* wg)
{
    return (int32_t) atomic_load(&wg->atomic_count);
}
CHANAPI Wait_Group* wait_group_push(volatile Wait_Group* wg, isize count)
{
    if(count > 0)
        atomic_fetch_add(&wg->atomic_count, (uint32_t) count);
    return (Wait_Group*) wg;
}
CHANAPI bool wait_group_pop(volatile Wait_Group* wg, isize count, Sync_Wait wait)
{
    bool out = false;
    if(count <= 0)
        chan_debug_log("wait_group_pop negative", (uint64_t) -count);
    else
    {
        int32_t old_val = (int32_t) atomic_fetch_sub(&wg->atomic_count, (uint32_t) count);
        chan_debug_log("wait_group_pop", (uint32_t) old_val, (uint32_t) old_val - (uint32_t) count);

        //if this was the pop that got it over the line wake 
        if(old_val - count <= 0 && old_val > 0)
        {
            atomic_fetch_add(&wg->atomic_wakes, 1);
            chan_debug_log("wait_group_pop WAKE");
            wait.wake(wg);
            out = true;
        }
    }

    return out;
}
CHANAPI void wait_group_wait(volatile Wait_Group* wg, Sync_Wait wait)
{
    Wait_Group before = {atomic_load(&wg->combined)};
    Wait_Group curr = before;
    for(;; curr.combined = atomic_load(&wg->combined)) {
        chan_debug_log("wait_group_wait", (uint32_t) curr.count, curr.wakes);
        if(curr.count <= 0 || before.wakes != curr.wakes)
        {
            chan_debug_log("wait_group_wait done", (uint32_t) curr.count, curr.wakes);
            return;
        }

        chan_debug_log("wait_group_wait WAIT", curr.count);
        if(wait.wait)
            wait.wait(wg, (uint32_t) curr.count, -1);
        else
            chan_pause();
    }
}

CHANAPI bool wait_group_wait_timed(volatile Wait_Group* wg, double timeout, Sync_Wait wait)
{
    static double freq_s = 0;
    if(freq_s == 0)
        freq_s = (double) chan_perf_frequency();

    int64_t wait_ticks = (int64_t) (timeout*freq_s);
    int64_t start_ticks = chan_perf_counter();

    Wait_Group before = {atomic_load(&wg->combined)};
    Wait_Group curr = before;
    for(;; curr.combined = atomic_load(&wg->combined)) {
        chan_debug_log("wait_group_wait_timed", (uint32_t) curr.count, curr.wakes);
        if(curr.count <= 0 || before.wakes != curr.wakes)
        {
            chan_debug_log("wait_group_wait_timed done", (uint32_t) curr.count, curr.wakes);
            return true;
        }

        int64_t curr_ticks = chan_perf_counter();
        int64_t ellapsed_ticks = curr_ticks - start_ticks;
        if(ellapsed_ticks > wait_ticks)
        {
            chan_debug_log("wait_group_wait_timed given after ticks", (uint64_t) wait_ticks);
            return false;
        }
        
        double wait_s = (double) (wait_ticks - ellapsed_ticks)/freq_s;
        if(wait.wait)
            wait.wait(wg, (uint32_t) curr.count, wait_s);
        else
            chan_pause();
    }
}
