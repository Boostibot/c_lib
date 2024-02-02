#ifndef JOT_PROFILE
#define JOT_PROFILE

#include "platform.h"
#include "defines.h"
#include "assert.h"
#include "perf.h"

// This file provides a simple and performant API
// for tracking running time across the whole aplication.
// Does not require any intialization, allocations or locks and
// works across files and compilation units.

// See below for example. 

#ifdef _PROFILE_EXAMPLE
void main()
{
	//========== 1: capture stats ===============
    for(isize i = 0; i < 100000; i++)
	{
		PERF_COUNTER_START(my_counter);
        //Run some code
		    PERF_COUNTER_START(my_counter2);
            //Run some code
            //printf("%d ", (int) i);
		    PERF_COUNTER_END(my_counter2);
		PERF_COUNTER_END(my_counter);
	}
    
	//========== 2: print stats ===============
	for(Global_Perf_Counter* counter = profile_get_counters(); counter != NULL; counter = counter->next)
	{
		printf("total: %09lf ms average: %09lf ms counter \"%s\" : %s : %d\n", 
			profile_get_counter_total_running_time_s(*counter)*1000,
			profile_get_counter_average_running_time_s(*counter)*1000,
			counter->name,
			counter->function,
			(int) counter->line
		);
	}

	//On my machine the output is
	
	//without any optimalizations:
	//00.000131 ms counter "my_counter" : main : 10
    //00.000041 ms counter "my_counter2" : main : 12

	//With -O2 and #define PROFILE_NO_DEBUG:
    //00.000067 ms counter "my_counter" : main : 10
    //00.000020 ms counter "my_counter2" : main : 12

	//With -O2 and #define PROFILE_NO_DEBUG: and #define PROFILE_NO_ATOMICS
    //00.000056 ms counter "my_counter" : main : 10
    //00.000020 ms counter "my_counter2" : main : 12

	//Update: PROFILE_NO_ATOMICS is no longer supported

	//As you can see the profiling incrus minimal overhead. 
	//This is especially noticable with the #define PROFILE_NO_ATOMICS
	//which esentially strips it down to only the bare necessities without 
	//improving the performance in any cosiderable way.
}
#endif

//Dissables counting of currently active perf counters
//#define PROFILE_NO_DEBUG

//Locally enables perf counters (can be toggled just like ASSERT macros)
#define DO_PERF_COUNTERS

//Makes all counters detailed. This is the default.
#define PROFILE_DO_ONLY_DETAILED_COUNTERS

//typedef void (*Global_Perf_Counter_User_Format_Func)(const Global_Perf_Counter* counter, String_Builder* into);

typedef struct Global_Perf_Counter
{
	//Sometimes we want to add some extra piece of data we track to our counters
	//such as: number of hash collisions, number of times a certain branch was taken etc.
	//We cannot track this 
	//void* user_format_context;
	//Global_Perf_Counter_User_Format_Func* user_format_func;

	struct Global_Perf_Counter* next;
	i32 line;
	i32 concurrent_running_counters; 
	//the number of concurrent running counters actiong upon this counter.
	//Useful for debugging. Is 0 if PROFILE_NO_DEBUG is defines

	const char* file;
	const char* function;
	const char* name;

	bool is_detailed;
	Perf_Counter counter;
} Global_Perf_Counter;

typedef struct Global_Perf_Counter_Running
{
	Global_Perf_Counter* my_counter;
	i64 running;
	i64 line;
	const char* file;
	const char* function;
	const char* name;
	bool stopped;
} Global_Perf_Counter_Running;

EXPORT Global_Perf_Counter_Running global_perf_counter_start(Global_Perf_Counter* my_counter, i32 line, const char* file, const char* function, const char* name);
EXPORT void global_perf_counter_end(Global_Perf_Counter_Running* running);
EXPORT void global_perf_counter_end_detailed(Global_Perf_Counter_Running* running);
EXPORT void global_perf_counter_end_discard(Global_Perf_Counter_Running* running);

EXPORT Global_Perf_Counter* profile_get_counters();
EXPORT i64 profile_get_total_running_counters_count();
EXPORT f64 profile_get_counter_total_running_time_s(Global_Perf_Counter counter);
EXPORT f64 profile_get_counter_average_running_time_s(Global_Perf_Counter counter);

#define PERF_COUNTER_START(name)		PP_ID(PP_CONCAT(_IF_NOT_PERF_START_,		DO_PERF_COUNTERS)(name))
#define PERF_COUNTER_END(name)			PP_ID(PP_CONCAT(_IF_NOT_PERF_END_,			DO_PERF_COUNTERS)(name))
#define PERF_COUNTER_END_DETAILED(name)	PP_ID(PP_CONCAT(_IF_NOT_PERF_END_DETAILED_, DO_PERF_COUNTERS)(name))
#define PERF_COUNTER_END_DISCARD(name)	PP_ID(PP_CONCAT(_IF_NOT_PERF_END_DISCARD_,	DO_PERF_COUNTERS)(name))

#ifdef PROFILE_DO_ONLY_DETAILED_COUNTERS
	#undef PERF_COUNTER_END
	#define PERF_COUNTER_END(name) PERF_COUNTER_END_DETAILED(name)
#endif

// ========= MACRO IMPLMENTATION ==========
	#define _IF_NOT_PERF_START_DO_PERF_COUNTERS(name) Global_Perf_Counter_Running name = {0}
	#define _IF_NOT_PERF_START_(name) \
		MODIFIER_ALIGNED(64) static Global_Perf_Counter _##name = {0}; \
		Global_Perf_Counter_Running name = global_perf_counter_start(&_##name, __LINE__, __FILE__, __FUNCTION__, #name); 

	#define _IF_NOT_PERF_END_DO_PERF_COUNTERS(name) (void) (name)
	#define _IF_NOT_PERF_END_(name) global_perf_counter_end(&(name))
	
	#define _IF_NOT_PERF_END_DETAILED_DO_PERF_COUNTERS(name) (void) (name)
	#define _IF_NOT_PERF_END_DETAILED_(name) global_perf_counter_end_detailed(&(name))

	#define _IF_NOT_PERF_END_DISCARD_DO_PERF_COUNTERS(name) (void) (name)
	#define _IF_NOT_PERF_END_DISCARD_(name) global_perf_counter_end_discard(&(name))
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PROFILE_IMPL)) && !defined(JOT_PROFILE_HAS_IMPL)
#define JOT_PROFILE_HAS_IMPL

	//Must be correctly sized for optimal performance
	//need to be on separed cache lines to eliminate false sharing
	STATIC_ASSERT(sizeof(Global_Perf_Counter) >= 64);

	static Global_Perf_Counter* perf_counters_linked_list = NULL;
	static i32 perf_counters_running_count = 0;
	
	EXPORT Global_Perf_Counter_Running global_perf_counter_start(Global_Perf_Counter* my_counter, i32 line, const char* file, const char* function, const char* name)
	{
		Global_Perf_Counter_Running running = {0};
		running.running = perf_start();
		running.my_counter = my_counter;
		running.line = line;
		running.file = file;
		running.function = function;
		running.name = name;
	
		#if !defined(PROFILE_NO_DEBUG)
			platform_atomic_add32(&perf_counters_running_count, 1);
			platform_atomic_add32(&my_counter->concurrent_running_counters, 1);
		#endif

		return running;
	}

	INTERNAL void _perf_counter_end(Global_Perf_Counter_Running* running, bool is_detailed)
	{
		Global_Perf_Counter* counter = running->my_counter;
		int64_t delta = platform_perf_counter() - running->running;
		i64 runs = perf_end_atomic_delta(&counter->counter, delta, is_detailed);
		ASSERT_MSG(running->stopped == false, "Global_Perf_Counter_Running running counter stopped more than once!");

		//only save the stats that dont need to be updated on the first run
		if(runs == 1)
		{
			//platform_atomic_excahnge64 sets the value pointed to by the first argument to the second argument and returns the original value
			//We use this to set the head to the newly added counter and save the old previous first node so we can reference it from our counter
			Global_Perf_Counter* prev_head = (Global_Perf_Counter*) platform_atomic_excahnge64((volatile i64*) (void*) &perf_counters_linked_list, (i64) counter);

			counter->next = prev_head;
			counter->file = running->file;
			counter->line = (i32) running->line;
			counter->function = running->function;
			counter->name = running->name;
			counter->is_detailed = is_detailed;
		}
	
		#if !defined(PROFILE_NO_DEBUG)
			platform_atomic_sub32(&perf_counters_running_count, 1);
			platform_atomic_sub32(&counter->concurrent_running_counters, 1);
			ASSERT(perf_counters_running_count >= 0 && counter->concurrent_running_counters >= 0);
		#endif

		running->stopped = true;
	}

	EXPORT void global_perf_counter_end(Global_Perf_Counter_Running* running)
	{
		_perf_counter_end(running, false);
	}
	
	EXPORT void global_perf_counter_end_detailed(Global_Perf_Counter_Running* running)
	{
		_perf_counter_end(running, true);
	}

	EXPORT void global_perf_counter_end_discard(Global_Perf_Counter_Running* running)
	{
		Global_Perf_Counter* counter = running->my_counter;
		#if !defined(PROFILE_NO_DEBUG)
			platform_atomic_sub32(&perf_counters_running_count, 1);
			platform_atomic_sub32(&counter->concurrent_running_counters, 1);
			ASSERT(perf_counters_running_count >= 0 && counter->concurrent_running_counters >= 0);
		#endif
	}
	
	INTERNAL f64 _safe_div(f64 num, f64 den, f64 if_zero)
	{
		if(den == 0)
			return if_zero;
		else
			return num/den;
	}

	//INTERNAL i64 _profile_get_counter_freq(Global_Perf_Counter counter)
	//{
	//	return counter.counter.frquency ? counter.counter.frquency : platform_perf_counter_frequency();
	//}

	EXPORT f64 profile_get_counter_total_running_time_s(Global_Perf_Counter counter)
	{
		return _safe_div((f64) counter.counter.counter, (f64) counter.counter.frquency, 0);
	}
	EXPORT f64 profile_get_counter_average_running_time_s(Global_Perf_Counter counter)
	{
		return _safe_div((f64) counter.counter.counter, (f64) (counter.counter.frquency * counter.counter.runs), 0);
	}
	
	EXPORT Global_Perf_Counter* profile_get_counters()
	{
		return perf_counters_linked_list;
	}

	EXPORT i64 profile_get_total_running_counters_count()
	{
		return perf_counters_running_count;
	}

#endif