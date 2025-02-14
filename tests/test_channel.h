#ifndef MODULE_TEST_CHANNEL_H
#define MODULE_TEST_CHANNEL_H

#include <stdint.h>

typedef long long int       lli;
typedef long long unsigned  llu;

//Inject debug stuff
#define CHANNEL_DEBUG
#ifdef CHANNEL_DEBUG
    static void _chan_wait_n(int n);
    static void _chan_mem_log(const char* msg, uint64_t custom1, uint64_t custom2);

    #define chan_debug_log(msg, ...)              chan_debug_log2(msg, ##__VA_ARGS__, 0, 0)
    #define chan_debug_log2(msg, val1, val2, ...) _chan_mem_log(msg, val1, val2)
    #define chan_debug_wait(n)                   _chan_wait_n(n)
#endif

#include "../channel.h"
#include "../sync.h"

#ifdef CHANNEL_DEBUG
    static _Thread_local const char* _thread_name = "<undefined>";
    static _Thread_local bool _thread_name_allocated = false;
    static const char* chan_thread_name()
    {
        return _thread_name;
    }

    static void chan_set_thread_name(const char* new_name, bool allocated)
    {
        if(_thread_name_allocated)
            free((void*) _thread_name);

        _thread_name = new_name;
        _thread_name_allocated = allocated;
    }
    
    static void _chan_wait_n(int n)
    {
        static CHAN_ATOMIC(uint32_t) dummy = 0;
        for(int i = 0; i < n; i++)
            atomic_fetch_add_explicit(&dummy, 1, memory_order_relaxed);
    }

    typedef struct Sync_Mem_Log {
        const char* n;
        const char* m;
        uint64_t c1;
        uint64_t c2;
    } Sync_Mem_Log;
    
    #define _SYNC_MEM_LOG_CAP (1 << 20)
    static CHAN_ATOMIC(uint64_t) _mem_log_pos = 0;
    static Sync_Mem_Log _mem_logs[_SYNC_MEM_LOG_CAP] = {0};

    static void _chan_mem_log(const char* msg, uint64_t custom1, uint64_t custom2)
    {
        uint64_t curr = atomic_fetch_add_explicit(&_mem_log_pos, 1, memory_order_relaxed);
        uint64_t index = curr & (_SYNC_MEM_LOG_CAP - 1);

        Sync_Mem_Log log = {chan_thread_name(), msg, custom1, custom2};
        _mem_logs[index] = log;
    }
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef TEST
    #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
#endif // !TEST

#define THROUGHPUT_INT_CHAN_INFO _CHAN_SINIT(Channel_Info){sizeof(int)}

#define TEST_CHAN_MAX_THREADS 64 

enum {
    _TEST_CHANNEL_REQUEST_RUN = 1,
    _TEST_CHANNEL_REQUEST_EXIT = 2,
};

typedef struct _Test_Channel_Lin_Point {
    uint64_t value;
    uint32_t thread_id;
    int32_t _;
} _Test_Channel_Lin_Point;

typedef struct _Test_Channel_Linearization_Thread {
    Channel* chan;
    Channel* requests;
    Wait_Group* done;
    
    CHAN_ATOMIC(uint64_t) done_ticket;
    uint32_t _;
    CHAN_ATOMIC(uint32_t) id;

    char name[30];
    bool print;
    bool okay;
} _Test_Channel_Linearization_Thread;

void _test_channel_linearization_consumer(void* arg)
{
    _Test_Channel_Linearization_Thread* context = (_Test_Channel_Linearization_Thread*) arg;
    chan_set_thread_name(context->name, false);
    if(context->print) 
        printf("   %s created\n", context->name);

    uint64_t max_per_thread[TEST_CHAN_MAX_THREADS] = {0};
    for(int i = 0;; i++)
    {
        uint32_t request = 0;
        uint64_t ticket = 0;
        channel_ticket_pop(context->requests, &request, &ticket, context->requests->info);
        if(request == _TEST_CHANNEL_REQUEST_RUN)
        {
            chan_debug_log("consumer ran");
            if(context->print) 
                printf("   %s run #%i\n", context->name, i+1);

            for(;;)
            {
                //chan_debug_log("consumer pop");
                _Test_Channel_Lin_Point point = {0};
                if(channel_pop(context->chan, &point, context->chan->info) == false)
                    break;
        
                //check linearizibility
                if(point.thread_id > TEST_CHAN_MAX_THREADS)
                {
                    context->okay = false;
                    for(int k = 0; k < 10; k++)
                    printf("   %s encountered thread id %i out of range!\n", 
                        context->name, point.thread_id);
                }
                else if(max_per_thread[point.thread_id] >= point.value)
                {
                    context->okay = false;
                    for(int k = 0; k < 10; k++)
                    printf("   %s encountered value %lli which was not more than previous %lli\n", 
                        context->name, (lli) point.value, (lli) max_per_thread[point.thread_id]);
                    max_per_thread[point.thread_id] = point.value;
                }
                else
                    max_per_thread[point.thread_id] = point.value;
            }
            
            chan_debug_log("consumer stopped");
            if(context->print) 
                printf("   %s stopped #%i\n", context->name, i+1);

            atomic_store(&context->done_ticket, ticket);
            wait_group_pop(context->done, 1, SYNC_WAIT_BLOCK);
        }
        else if(request == _TEST_CHANNEL_REQUEST_EXIT)
        {
            if(context->print) 
                printf("   %s exited with %s\n", context->name, context->okay ? "okay" : "fail");

            atomic_store(&context->done_ticket, ticket);
            wait_group_pop(context->done, 1, SYNC_WAIT_BLOCK);
            break;
        }
        else
        {
            TEST(false);
        }
    }
}

void _test_channel_linearization_producer(void* arg)
{
    _Test_Channel_Linearization_Thread* context = (_Test_Channel_Linearization_Thread*) arg;
    chan_set_thread_name(context->name, false);
    if(context->print) 
        printf("   %s created\n", context->name);

    uint64_t curr_max = 1;
    for(int i = 0;; i++)
    {
        uint32_t request = 0;
        uint64_t ticket = 0;
        channel_ticket_pop(context->requests, &request, &ticket, context->requests->info);
        if(request == _TEST_CHANNEL_REQUEST_RUN)
        {
            chan_debug_log("prdoucer ran");
            if(context->print) 
                printf("   %s run #%i\n", context->name, i+1);
            
            for(;; curr_max += 1)
            {
                _Test_Channel_Lin_Point point = {0};
                point.thread_id = context->id;
                point.value = curr_max;
                //chan_debug_log("prdoucer push");
                if(channel_push(context->chan, &point, context->chan->info) == false)
                    break;
            }
        
            if(context->print) 
                printf("   %s stopped #%i\n", context->name, i+1);
            
            chan_debug_log("prdoucer stopped");
            atomic_store(&context->done_ticket, ticket);
            wait_group_pop(context->done, 1, SYNC_WAIT_BLOCK);
        }
        else if(request == _TEST_CHANNEL_REQUEST_EXIT)
        {
            if(context->print) 
                printf("   %s exited\n", context->name);
        
            atomic_store(&context->done_ticket, ticket);
            wait_group_pop(context->done, 1, SYNC_WAIT_BLOCK);
            break;
        }
        else
        {
            TEST(false);
        }
    }

}

#define _TEST_CHANNEL_REQUEST_INFO _CHAN_SINIT(Channel_Info){sizeof(uint32_t), chan_wait_block, chan_wake_block}

void test_channel_linearization(isize buffer_capacity, isize producer_count, isize consumers_count, isize stop_count, double seconds, bool block, bool printing, bool thread_printing)
{
    if(printing)
        printf("Channel: Testing liearizability with buffer capacity %lli producers:%lli consumers:%lli block:%s for %.2lfs\n", 
            (lli) buffer_capacity, (lli) producer_count, (lli) consumers_count, block ? "true" : "false", seconds);

    Channel_Info info = {0};
    if(block)
        info = _CHAN_SINIT(Channel_Info){sizeof(_Test_Channel_Lin_Point), chan_wait_block, chan_wake_block};
    else
        info = _CHAN_SINIT(Channel_Info){sizeof(_Test_Channel_Lin_Point), chan_wait_yield};

    Channel* chan = channel_malloc(buffer_capacity, info);
    isize thread_count = producer_count + consumers_count;

    Wait_Group done = {0};
    wait_group_push(&done, thread_count);
    
    Channel* requests[2*TEST_CHAN_MAX_THREADS] = {0};
    for(isize i = 0; i < thread_count; i++)
        requests[i] = channel_malloc(1, _TEST_CHANNEL_REQUEST_INFO);

    _Test_Channel_Linearization_Thread producers[TEST_CHAN_MAX_THREADS] = {0};
    _Test_Channel_Linearization_Thread consumers[TEST_CHAN_MAX_THREADS] = {0};
    for(isize i = 0; i < producer_count; i++)
    {
        producers[i].chan = chan;
        producers[i].id = (uint32_t) i;
        producers[i].done_ticket = CHANNEL_MAX_TICKET;
        producers[i].print = thread_printing;
        producers[i].done = &done;
        producers[i].requests = requests[i];
        snprintf(producers[i].name, sizeof producers[i].name, "producer #%02lli", (lli) i);
        TEST(chan_start_thread(_test_channel_linearization_producer, &producers[i]));
    }
    
    for(isize i = 0; i < consumers_count; i++)
    {
        consumers[i].chan = chan;
        consumers[i].print = thread_printing;
        consumers[i].id = (uint32_t) i;
        consumers[i].done_ticket = CHANNEL_MAX_TICKET;
        consumers[i].done = &done;
        consumers[i].okay = true;
        consumers[i].requests = requests[producer_count + i];
        snprintf(consumers[i].name, sizeof consumers[i].name, "consumer #%02lli", (lli) i);
        TEST(chan_start_thread(_test_channel_linearization_consumer, &consumers[i]));
    }

    //Run threads for some time and repeatedly interrupt them with calls to close. 
    // Linearizibility should be maintained
    for(uint32_t gen = 0; gen < (uint32_t) stop_count; gen++)
    {
        //Run threads
        if(printing) printf("   Enabling threads to run #%i for %.2lfs\n", gen, seconds/stop_count);
        
        uint32_t request = _TEST_CHANNEL_REQUEST_RUN;
        for(isize i = 0; i < thread_count; i++)
            channel_push(requests[i], &request, _TEST_CHANNEL_REQUEST_INFO);

        //wait for some time
        chan_sleep(seconds/stop_count);
        
        //stop threads (using some close function)
        if(printing) printf("   Stopping threads #%i\n", gen);
        if(gen == stop_count)
            channel_close_hard(chan, info);
        //else
        else if(gen % 2 == 0)
            channel_close_soft(chan, info);
        else
            channel_close_push(chan, info);
        
        //wait for all threads to stop
        while(wait_group_wait_timed(&done, 2, SYNC_WAIT_BLOCK) == false)
        {
            printf("   Wait stuck\n");
            for(isize i = 0; i < producer_count; i++)
                if(atomic_load(&producers[i].done_ticket) != gen)
                    printf("   producer #%lli stuck\n", (lli) i);
            for(isize i = 0; i < consumers_count; i++)
                if(atomic_load(&consumers[i].done_ticket) != gen)
                    printf("   consumer #%lli stuck\n", (lli) i);
            printf("   Wait stuck done\n");
        }
        
        if(printing) printf("   All threads stopped #%i\n", gen);
        TEST(channel_is_invariant_converged_state(chan, info));

        //Everything ok?
        for(isize k = 0; k < consumers_count; k++)
            TEST(consumers[k].okay);

        //reopen and reset
        channel_reopen(chan, info);
        wait_group_push(&done,  thread_count); 
    }
    
    if(printing) printf("   Finishing threads\n");
        
    //tell threads to exit run threads for one last time
    {
        uint32_t request = _TEST_CHANNEL_REQUEST_EXIT;
        for(isize i = 0; i < thread_count; i++)
            channel_push(requests[i], &request, _TEST_CHANNEL_REQUEST_INFO);

        wait_group_wait(&done, SYNC_WAIT_BLOCK);
    }

    if(printing) printf("   All threads finished\n");

    for(isize i = 0; i < thread_count; i++)
        channel_deinit(requests[i]);

    channel_deinit(chan);
}

typedef struct _Test_Channel_Cycle_Thread {
    Channel* a;
    Channel* b;
    Channel* lost;
    Channel* requests;
    Wait_Group* done;
    
    CHAN_ATOMIC(uint64_t) done_ticket;
    CHAN_ATOMIC(uint32_t) id;
    uint32_t _;

    char name[31];
    bool print;
} _Test_Channel_Cycle_Thread;

void _test_channel_cycle_runner(void* arg)
{
    _Test_Channel_Cycle_Thread* context = (_Test_Channel_Cycle_Thread*) arg;
    chan_set_thread_name(context->name, false);
    if(context->print) 
        printf("   %s created\n", context->name);

    for(int i = 0;; i++)
    {
        uint32_t request = 0;
        uint64_t ticket = 0;
        channel_ticket_pop(context->requests, &request, &ticket, context->requests->info);
        if(request == _TEST_CHANNEL_REQUEST_RUN)
        {
            if(context->print) 
                printf("   %s run #%i\n", context->name, i+1);
            
            for(;;)
            {
                int val = 0;
                if(channel_pop(context->b, &val, context->b->info) == false)
                {
                    chan_debug_log("pop failed (closed)");
                    break;
                }
        
                if(channel_push(context->a, &val, context->a->info) == false)
                {
                    chan_debug_log("lost (adding to lost channel)", (uint32_t) val);
                    channel_push(context->lost, &val, context->lost->info);
                    break;
                }
            }
            
            if(context->print) 
                printf("   %s stopped #%i\n", context->name, i+1);
        
            atomic_store(&context->done_ticket, ticket);
            wait_group_pop(context->done, 1, SYNC_WAIT_BLOCK);
        }
        else if(request == _TEST_CHANNEL_REQUEST_EXIT)
        {
            if(context->print) 
                printf("   %s exited\n", context->name);
        
            atomic_store(&context->done_ticket, ticket);
            wait_group_pop(context->done, 1, SYNC_WAIT_BLOCK);
            break;
        }
        else
        {
            TEST(false);
        }
    }
}

int _int_comp_fn(const void* a, const void* b)
{
    return (*(int*) a > *(int*) b) - (*(int*) a < *(int*) b);
}

void test_channel_cycle(isize buffer_capacity, isize a_count, isize b_count, isize stop_count, double seconds, bool block, bool printing, bool thread_printing)
{
    if(printing)
        printf("Channel: Testing cycle with buffer capacity %lli threads A:%lli threads B:%lli block:%s for %.2lfs\n", 
            (lli) buffer_capacity, (lli) a_count, (lli) b_count, block ? "true" : "false", seconds);
            
    Channel_Info info = {0};
    if(block)
        info = _CHAN_SINIT(Channel_Info){sizeof(int), chan_wait_block, chan_wake_block};
    else
        info = _CHAN_SINIT(Channel_Info){sizeof(int), chan_wait_yield};

    Channel* a_chan = channel_malloc(buffer_capacity, info);
    Channel* b_chan = channel_malloc(buffer_capacity, info);
    Channel* lost_chan = channel_malloc((a_count + b_count)*(stop_count + 1), info);
    
    for(int i = 0; i < a_chan->capacity; i++)
        channel_push(a_chan, &i, info);
        
    isize thread_count = a_count + b_count;

    Wait_Group done = {0};
    wait_group_push(&done, thread_count);
    
    Channel* requests[2*TEST_CHAN_MAX_THREADS] = {0};
    for(isize i = 0; i < thread_count; i++)
        requests[i] = channel_malloc(1, _TEST_CHANNEL_REQUEST_INFO);

    _Test_Channel_Cycle_Thread a_threads[TEST_CHAN_MAX_THREADS] = {0};
    _Test_Channel_Cycle_Thread b_threads[TEST_CHAN_MAX_THREADS] = {0};

    for(isize i = 0; i < a_count; i++)
    {
        _Test_Channel_Cycle_Thread state = {0};
        state.a = a_chan;
        state.b = b_chan;
        state.lost = lost_chan;
        state.requests = requests[i];
        state.done_ticket = CHANNEL_MAX_TICKET;
        state.print = thread_printing;
        state.done = &done;
        snprintf(state.name, sizeof state.name, "A -> B #%lli", (lli) i);

        a_threads[i] = state;
        TEST(chan_start_thread(_test_channel_cycle_runner, a_threads + i));
    }
    
    for(isize i = 0; i < b_count; i++)
    {
        _Test_Channel_Cycle_Thread state = {0};
        state.b = a_chan;
        state.a = b_chan;
        state.lost = lost_chan;
        state.requests = requests[i + a_count];
        state.done_ticket = CHANNEL_MAX_TICKET;
        state.print = thread_printing;
        state.done = &done;
        snprintf(state.name, sizeof state.name, "B -> A #%lli", (lli) i);

        b_threads[i] = state;
        TEST(chan_start_thread(_test_channel_cycle_runner, b_threads + i));
    }

    for(uint32_t gen = 0; gen < (uint32_t) stop_count; gen++)
    {
        if(printing) printf("   Enabling threads to run #%i for %.2lfs\n", gen, seconds/stop_count);
        
        uint32_t request = _TEST_CHANNEL_REQUEST_RUN;
        for(isize i = 0; i < thread_count; i++)
            channel_push(requests[i], &request, _TEST_CHANNEL_REQUEST_INFO);

        chan_sleep(seconds/stop_count);
        
        if(printing) printf("   Stopping threads #%i\n", gen);
        
        if(gen % 2 == 0)
        {
            channel_close_soft(a_chan, info);
            channel_close_soft(b_chan, info);
        }
        else
        {
            channel_close_push(a_chan, info);
            channel_close_push(b_chan, info);
        }
        
        while(wait_group_wait_timed(&done, 2, SYNC_WAIT_BLOCK) == false)
        {
            printf("   Wait stuck\n");
            for(isize i = 0; i < a_count; i++)
                if(atomic_load(&a_threads[i].done_ticket) != gen)
                    printf("   a #%lli stuck\n", (lli) i);
            for(isize i = 0; i < b_count; i++)
                if(atomic_load(&b_threads[i].done_ticket) != gen)
                    printf("   b #%lli stuck\n", (lli) i);
            printf("   Wait stuck done\n");
        }

        if(printing) printf("   All threads stopped #%i\n", gen);
        TEST(channel_is_invariant_converged_state(a_chan, info));
        TEST(channel_is_invariant_converged_state(b_chan, info));

        //Everything ok?
        isize a_chan_count = channel_count(a_chan);
        isize b_chan_count = channel_count(b_chan);
        isize lost_count = channel_count(lost_chan);

        isize count_sum = a_chan_count + b_chan_count + lost_count;
        TEST(count_sum == buffer_capacity);

        //reopen and reset
        channel_reopen(a_chan, info);
        channel_reopen(b_chan, info);
        
        wait_group_push(&done, thread_count);
    }
    
    //signal to threads to exit
    {
        uint32_t request = _TEST_CHANNEL_REQUEST_EXIT;
        for(isize i = 0; i < thread_count; i++)
            channel_push(requests[i], &request, _TEST_CHANNEL_REQUEST_INFO);

        wait_group_wait(&done, SYNC_WAIT_BLOCK);
    }

    if(printing) printf("   All threads finished\n");

    //pop everything
    isize everything_i = 0;
    int* everything = (int*) malloc(buffer_capacity*sizeof(int));
    for(isize i = 0;; i++)
    {
        int x = 0;
        Channel_Res res = channel_try_pop(a_chan, &x, info);
        TEST(res != CHANNEL_LOST_RACE);
        if(res)
            break;

        TEST(everything_i < buffer_capacity);
        everything[everything_i++] = x;
    }

    for(isize i = 0;; i++)
    {
        int x = 0;
        Channel_Res res = channel_try_pop(b_chan, &x, info);
        TEST(res != CHANNEL_LOST_RACE);
        if(res)
            break;

        TEST(everything_i < buffer_capacity);
        everything[everything_i++] = x;
    }

    for(isize i = 0;; i++)
    {
        int x = 0;
        Channel_Res res = channel_try_pop(lost_chan, &x, info);
        TEST(res != CHANNEL_LOST_RACE);
        if(res)
            break;

        TEST(everything_i < buffer_capacity);
        everything[everything_i++] = x;
    }

    //check if nothing was lost
    TEST(everything_i == buffer_capacity);
    qsort(everything, buffer_capacity, sizeof(int), _int_comp_fn);
    for(isize i = 0; i < buffer_capacity; i++)
        TEST(everything[i] == i);

    free(everything);
    
    for(isize i = 0; i < thread_count; i++)
        channel_deinit(requests[i]);

    channel_deinit(a_chan);
    channel_deinit(b_chan);
    channel_deinit(lost_chan);
}

void test_channel_sequential(isize capacity, bool block)
{
    Channel_Info info = {0};
    if(block)
        info = _CHAN_SINIT(Channel_Info){sizeof(int), chan_wait_block, chan_wake_block};
    else
        info = _CHAN_SINIT(Channel_Info){sizeof(int), chan_wait_yield};

    int dummy = 0;
    {
        Channel* chan = channel_malloc(1, info);
        channel_deinit(chan);
    }
    {

        Channel* chan = channel_malloc(1, info);
        TEST(channel_push(chan, &dummy, info));
        channel_deinit(chan);
    }

    Channel* chan = channel_malloc(capacity, info);
    
    //Test blocking interface
    {
        TEST(channel_is_invariant_converged_state(chan, info));
        //Push all
        for(int i = 0; i < chan->capacity; i++) {
            TEST(channel_push(chan, &i, info));
            TEST(channel_is_invariant_converged_state(chan, info));
        }

        TEST(channel_try_push(chan, &dummy, info) == CHANNEL_FULL);
    
        //Close reopen and check info
        TEST(channel_count(chan) == capacity);
        
        TEST(channel_close_soft(chan, info));
        TEST(channel_close_soft(chan, info) == false);
        TEST(channel_is_closed(chan));
        TEST(channel_push(chan, &dummy, info) == false);
        TEST(channel_pop(chan, &dummy, info) == false);
        TEST(channel_is_invariant_converged_state(chan, info));

        TEST(channel_count(chan) == capacity);
        TEST(channel_reopen(chan, info));
        //TEST(channel_reopen(chan, -1, info) == false);
        TEST(channel_count(chan) == capacity);

        //Pop all
        for(int i = 0; i < chan->capacity; i++)
        {
            int popped = 0;
            TEST(channel_pop(chan, &popped, info));
            TEST(popped == i);
            TEST(channel_is_invariant_converged_state(chan, info));
        }

        TEST(channel_count(chan) == 0);
        TEST(channel_try_pop(chan, &dummy, info) == CHANNEL_EMPTY);
        TEST(channel_count(chan) == 0);
    }

    //Push up to capacity then close, then pop until 
    //if(0)
    {
        int push_count = (int) chan->capacity - 1;
        for(int i = 0; i < push_count; i++) {
            TEST(channel_push(chan, &i, info));
            TEST(channel_is_invariant_converged_state(chan, info));
        }

        TEST(channel_close_push(chan, info));
        TEST(channel_push(chan, &dummy, info) == false);

        int popped = 0;
        int pop_count = 0;
        for(; channel_pop(chan, &popped, info); pop_count++)
        {
            TEST(popped == pop_count);
            TEST(channel_is_invariant_converged_state(chan, info));
        }

        TEST(pop_count == push_count);
        TEST(channel_count(chan) == 0);
        TEST(channel_is_invariant_converged_state(chan, info));

        TEST(channel_reopen(chan, info));
    }

    //Test nonblocking interface
    {
        for(int i = 0;; i++)
        {
            Channel_Res res = channel_try_push(chan, &i, info);
            if(res != CHANNEL_OK)
            {
                TEST(res == CHANNEL_FULL);
                break;
            }
        }

        TEST(channel_count(chan) == chan->capacity);
        TEST(channel_close_soft(chan, info));
        
        TEST(channel_try_push(chan, &dummy, info) == CHANNEL_CLOSED);
        TEST(channel_count(chan) == chan->capacity);
        
        TEST(channel_reopen(chan, info));
        TEST(channel_count(chan) == chan->capacity);
        
        int pop_count = 0;
        for(;; pop_count++)
        {
            int popped = 0;
            Channel_Res res = channel_try_pop(chan, &popped, info);
            if(res != CHANNEL_OK)
            {
                TEST(res == CHANNEL_EMPTY);
                break;
            }
            
            TEST(channel_is_invariant_converged_state(chan, info));
            TEST(popped == pop_count);
        }
        
        TEST(channel_is_invariant_converged_state(chan, info));
        TEST(channel_count(chan) == 0);
    }

    
    //Test nonblocking interface after close
    //if(0)
    {
        int push_count = (int) chan->capacity - 1;
        for(int i = 0; i < push_count; i++)
        {
            Channel_Res res = channel_try_push(chan, &i, info);
            TEST(res == CHANNEL_OK);
            TEST(channel_is_invariant_converged_state(chan, info));
        }

        TEST(channel_count(chan) == push_count);
        TEST(channel_close_push(chan, info));
        
        TEST(channel_try_push(chan, &dummy, info) == CHANNEL_CLOSED);
        TEST(channel_count(chan) == push_count);
        
        TEST(channel_is_invariant_converged_state(chan, info));

        int pop_count = 0;
        for(;; pop_count++)
        {
            int popped = 0;
            Channel_Res res = channel_try_pop(chan, &popped, info);
            if(res != CHANNEL_OK)
            {
                TEST(res == CHANNEL_CLOSED);
                break;
            }

            TEST(popped == pop_count);
        }

        TEST(pop_count == push_count);
        TEST(channel_count(chan) == 0);
        TEST(channel_is_invariant_converged_state(chan, info));
    }

    channel_deinit(chan);
}

void test_channel(double total_time)
{
    //channel_push_int(NULL, NULL);

    //Channel chan = {0};
    //channel_ticket_pop_int(&chan, NULL);
    srand(clock());

    TEST(channel_ticket_is_less(0, 1));
    TEST(channel_ticket_is_less(1, 2));
    TEST(channel_ticket_is_less(5, 2) == false);
    TEST(channel_ticket_is_less(UINT64_MAX/4, UINT64_MAX/2));
    TEST(channel_ticket_is_less(UINT64_MAX/2, UINT64_MAX/2 + 100));
    TEST(channel_ticket_is_less(UINT64_MAX/2 + 100, UINT64_MAX/2) == false);
    
    //if(0)
    {
        test_channel_sequential(1, false);
        test_channel_sequential(10, false);
        test_channel_sequential(100, false);
        test_channel_sequential(1000, false);
    
        test_channel_sequential(1, true);
        test_channel_sequential(10, true);
        test_channel_sequential(100, true);
        test_channel_sequential(1000, true);
    }
    
    //test_channel_cycle(100, 4, 4, 10, 0, true, true, true);
    bool main_print = true;
    bool thread_print = false;
    total_time = 0;

    double func_start = (double) clock()/CLOCKS_PER_SEC;
    for(isize test_i = 0;; test_i++)
    {
        double now = (double) clock()/CLOCKS_PER_SEC;
        double ellapsed = now - func_start;
        double remaining = total_time - ellapsed;
        (void) remaining;
        //if(remaining <= 0);
            //break;

        double test_duration = (exp2((double)rand()/RAND_MAX*4) - 1)/(1<<4);
        test_duration = 0;
        //if(test_duration > remaining)
            //test_duration = remaining;

        isize threads_a = (isize) pow(2, (double)rand()/RAND_MAX*5);
        isize threads_b = (isize) pow(2, (double)rand()/RAND_MAX*5);

        isize capacity = rand() % 1000 + 1;
        isize stop_count = 10;
        bool block = rand() % 2 == 0;

        //Higher affinity towards boundary values
        if(rand() % 20 == 0)
            capacity = 1;
        if(rand() % 20 == 0)
            threads_a = 1;
        if(rand() % 20 == 0)
            threads_b = 1;
        
        //capacity = 85;
        //block = true;
        //capacity = 1;
        //threads_a = 7;
        //threads_a = 1;

        test_channel_cycle(capacity, threads_a, threads_b, stop_count, test_duration, block, main_print, thread_print);
        test_channel_linearization(capacity, threads_a, threads_b, stop_count, test_duration, block, main_print, thread_print);
    }

    printf("done\n");
}

#if 0

typedef struct _Test_Channel_Throughput_Thread {
    Channel* chan;
    uint32_t* run_status;
    Wait_Group* wait_group_done;
    Wait_Group* wait_group_started;
    volatile isize operations;
    volatile isize ticks_before;
    volatile isize ticks_after;
    bool is_consumer;
    bool _[7];
} _Test_Channel_Throughput_Thread;

void _test_channel_throughput_runner(void* arg)
{
    _Test_Channel_Throughput_Thread* context = (_Test_Channel_Throughput_Thread*) arg;
    printf("%s created\n", context->is_consumer ? "consumer" : "producer");
    wait_group_pop(context->wait_group_started, 1, SYNC_WAIT_BLOCK);

    //wait for run status
    sync_wait_for_not_equal(context->run_status, _TEST_CHANNEL_STATUS_STOPPED, SYNC_WAIT_SPIN);
    printf("%s inside\n", context->is_consumer ? "consumer" : "producer");

    isize ticks_before = chan_perf_counter();
    isize operations = 0;
    int val = 0;

    if(context->is_consumer)
        while(channel_pop(context->chan, &val, THROUGHPUT_INT_CHAN_INFO))
            operations += 1;
    else
        while(channel_push(context->chan, &val, THROUGHPUT_INT_CHAN_INFO))
            operations += 1;

    isize ticks_after = chan_perf_counter();
    
    printf("%s completed\n", context->is_consumer ? "consumer" : "producer");

    atomic_store(&context->operations, operations);
    atomic_store(&context->ticks_before, ticks_before);
    atomic_store(&context->ticks_after, ticks_after);
    wait_group_pop(context->wait_group_done, 1, SYNC_WAIT_BLOCK);
}

void test_channel_throughput(isize buffer_capacity, isize a_count, isize b_count, double seconds)
{
    isize thread_count = a_count + b_count;
    _Test_Channel_Throughput_Thread threads[TEST_CHAN_MAX_THREADS] = {0};
    
    Channel* chan = channel_malloc(buffer_capacity, THROUGHPUT_INT_CHAN_INFO);

    uint32_t run_status = 0;
    Wait_Group wait_group_done = {0};
    Wait_Group wait_group_started = {0};
    wait_group_push(&wait_group_done, thread_count);
    wait_group_push(&wait_group_started, thread_count);

    for(isize i = 0; i < thread_count; i++)
    {
        threads[i].chan = chan;
        threads[i].run_status = &run_status;
        threads[i].wait_group_done = &wait_group_done;
        threads[i].wait_group_started = &wait_group_started;
        threads[i].is_consumer = i >= a_count;

        TEST(chan_start_thread(_test_channel_throughput_runner, threads + i));
    }

    sync_set_and_wake(&run_status, _TEST_CHANNEL_STATUS_RUN, SYNC_WAIT_SPIN);

    chan_sleep(seconds);
    channel_close_hard(chan, THROUGHPUT_INT_CHAN_INFO);
    
    wait_group_wait(&wait_group_done, SYNC_WAIT_BLOCK);

    double throughput_sum = 0;
    for(isize i = 0; i < thread_count; i++)
    {
        double duration = (double) (threads[i].ticks_after - threads[i].ticks_before)/chan_perf_frequency();
        double throughput = threads[i].operations / duration;

        printf("thread #%lli throughput %.2e ops/s\n", (lli) i+1, throughput);

        throughput_sum += throughput;
    }
    double avg_througput = throughput_sum/thread_count;
    printf("Average throughput %.2e ops/s\n", avg_througput);

    channel_deinit(chan);
}
#endif

#ifdef MODULE_TEST_CHANNEL_MAIN
//compile command: gcc -g -ggdb -DMODULE_TEST_CHANNEL_MAIN -x c _test_channel.h -lm -o build/_test_channel.out
int main()
{
    test_channel(1e18);
}
#endif

#endif