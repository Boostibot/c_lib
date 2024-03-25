#ifndef JOT_PERF
#define JOT_PERF

#ifndef __cplusplus
#include <stdbool.h>
#endif
#include <math.h>
#include <stdint.h>

#ifndef ASSERT
	#include <assert.h>
	#define ASSERT(x) assert(x)
#endif

#ifndef EXPORT
	#define EXPORT 
#endif

typedef struct Perf_Counter {
	int64_t counter;
	int64_t runs;
	int64_t frquency;
	int64_t mean_estimate;
	int64_t sum_of_squared_offset_counters;
	int64_t max_counter;
	int64_t min_counter;
} Perf_Counter;

typedef struct Perf_Stats {
	int64_t runs;
	int64_t batch_size;

	double total_s;
	double average_s;
	double min_s;
	double max_s;
	double standard_deviation_s;
	double normalized_standard_deviation_s; //(σ/μ)
} Perf_Stats;

EXPORT int64_t		perf_start();
EXPORT void			perf_end(Perf_Counter* counter, int64_t measure);
EXPORT void			perf_end_delta(Perf_Counter* counter, int64_t measure);
EXPORT void			perf_end_atomic(Perf_Counter* counter, int64_t measure);
EXPORT int64_t		perf_end_atomic_delta(Perf_Counter* counter, int64_t measure, bool detailed);
EXPORT Perf_Stats	perf_get_stats(Perf_Counter counter, int64_t batch_size);

//Prevents the compiler from optimizing the variable at the ptr and/or the ptr's value.
EXPORT void perf_do_not_optimize(const void* ptr);

typedef bool (*Benchamrk_Func)(isize iteration, void* context);

//bechmarks func and returns the resulting stats. Executes for total 'time' seconds but discards any results priot to 'warmup'.
//Calls the func 'batch_size' times per single measurement but corrects for it in returned stats.
// 'batch_size' should be set abover 1 for for very very short functions (typically non iterative math functions).
//If the 'func' returns false discards this measurement. This is good for functions that dont know when they will need to prepare another sets of data.
//'context' is passed into 'func'.
Perf_Stats perf_benchmark(f64 warmup, f64 time, isize batch_size, Benchamrk_Func func, void* context);

//Needs implementation:
int64_t platform_perf_counter();
int64_t platform_perf_counter_frequency();
inline static bool    platform_atomic_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value);
inline static int32_t platform_atomic_add32(volatile int32_t* target, int32_t value);
inline static int64_t platform_atomic_add64(volatile int64_t* target, int64_t value);
inline static int32_t platform_atomic_sub32(volatile int32_t* target, int32_t value);
inline static int64_t platform_atomic_sub64(volatile int64_t* target, int64_t value);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PERF_IMPL)) && !defined(JOT_PERF_HAS_IMPL)
#define JOT_PERF_HAS_IMPL

	
	EXPORT void perf_end_delta(Perf_Counter* counter, int64_t delta)
	{
		ASSERT(counter != NULL && delta >= 0 && "invalid Global_Perf_Counter_Running submitted");

		int64_t runs = counter->runs;
		counter->runs += 1; 
		if(runs == 0)
		{
			counter->frquency = platform_perf_counter_frequency();
			counter->max_counter = INT64_MIN;
			counter->min_counter = INT64_MAX;
			counter->mean_estimate = delta;
		}
	
		int64_t offset_delta = delta - counter->mean_estimate;
		counter->counter += delta;
		counter->sum_of_squared_offset_counters += offset_delta*offset_delta;
		counter->min_counter = MIN(counter->min_counter, delta);
		counter->max_counter = MAX(counter->max_counter, delta);
	}
	
	EXPORT int64_t perf_end_atomic_delta(Perf_Counter* counter, int64_t delta, bool detailed)
	{
		ASSERT(counter != NULL && delta >= 0 && "invalid Global_Perf_Counter_Running submitted");
		int64_t runs = platform_atomic_add64(&counter->runs, 1); 
		
		//only save the stats that dont need to be updated on the first run
		if(runs == 0)
		{
			counter->frquency = platform_perf_counter_frequency();
			counter->max_counter = INT64_MIN;
			counter->min_counter = INT64_MAX;
			counter->mean_estimate = delta;
		}
	
		platform_atomic_add64(&counter->counter, delta);

		if(detailed)
		{
			int64_t offset_delta = delta - counter->mean_estimate;
			platform_atomic_add64(&counter->sum_of_squared_offset_counters, offset_delta*offset_delta);

			do {
				if(counter->min_counter <= delta)
					break;
			} while(platform_atomic_compare_and_swap64(&counter->min_counter, counter->min_counter, delta) == false);

			do {
				if(counter->max_counter >= delta)
					break;
			} while(platform_atomic_compare_and_swap64(&counter->max_counter, counter->max_counter, delta) == false);
		}

		return runs;
	}
	
	EXPORT int64_t perf_start()
	{
		return platform_perf_counter();
	}

	EXPORT void perf_end(Perf_Counter* counter, int64_t measure)
	{
		int64_t delta = platform_perf_counter() - measure;
		perf_end_delta(counter, delta);
	}

	EXPORT void perf_end_atomic(Perf_Counter* counter, int64_t measure)
	{
		int64_t delta = platform_perf_counter() - measure;
		perf_end_atomic_delta(counter, delta, true);
	}
	EXPORT Perf_Stats perf_get_stats(Perf_Counter counter, int64_t batch_size)
	{
		if(batch_size <= 0)
			batch_size = 1;

		if(counter.frquency == 0)
			counter.frquency = platform_perf_counter_frequency();

		ASSERT(counter.min_counter * counter.runs <= counter.counter && "min must be smaller than sum");
		ASSERT(counter.max_counter * counter.runs >= counter.counter && "max must be bigger than sum");
        
		//batch_size is in case we 'batch' our tested function: 
		// ie instead of measure the tested function once we run it 100 times
		// this just means that each run is multiplied batch_size times
		int64_t iters = batch_size * (counter.runs);
        
		double batch_deviation_s = 0;
		if(counter.runs > 1)
		{
			double n = (double) counter.runs;
			double sum = (double) counter.counter;
			double sum2 = (double) counter.sum_of_squared_offset_counters;
            
			//Welford's algorithm for calculating varience
			double varience_ns = (sum2 - (sum * sum) / n) / (n - 1.0);

			//deviation = sqrt(varience) and deviation is unit dependent just like mean is
			batch_deviation_s = sqrt(fabs(varience_ns)) / (double) counter.frquency;
		}

		double total_s = 0.0;
		double mean_s = 0.0;
		double min_s = 0.0;
		double max_s = 0.0;

		ASSERT(counter.min_counter * counter.runs <= counter.counter);
		ASSERT(counter.max_counter * counter.runs >= counter.counter);
		if(counter.frquency != 0)
		{
			total_s = (double) counter.counter / (double) counter.frquency;
			min_s = (double) counter.min_counter / (double) (batch_size * counter.frquency);
			max_s = (double) counter.max_counter / (double) (batch_size * counter.frquency);
		}
		if(iters != 0)
			mean_s = total_s / (double) iters;

		ASSERT(mean_s >= 0 && min_s >= 0 && max_s >= 0);

		//We assume that summing all measure times in a batch 
		// (and then dividing by its size = making an average)
		// is equivalent to picking random samples from the original distribution
		// => Central limit theorem applies which states:
		// deviation_sampling = deviation / sqrt(samples)
        
		// We use this to obtain the original deviation
		// => deviation = deviation_sampling * sqrt(samples)
        
		// but since we also need to take the average of each batch
		// to get the deviation of a single element we get:
		// deviation_element = deviation_sampling * sqrt(samples) / samples
		//                   = deviation_sampling / sqrt(samples)

		double sqrt_batch_size = sqrt((double) batch_size);
		Perf_Stats stats = {0};

		//since min and max are also somewhere within the confidence interval
		// keeping the same confidence in them requires us to also apply the same correction
		// to the distance from the mean (this time * sqrt_batch_size because we already 
		// divided by batch_size when calculating min_s)
		stats.min_s = mean_s + (min_s - mean_s) * sqrt_batch_size; 
		stats.max_s = mean_s + (max_s - mean_s) * sqrt_batch_size; 

		//the above correction can push min to be negative 
		// happens mostly with noop and generally is not a problem
		if(stats.min_s < 0.0)
			stats.min_s = 0.0;
		if(stats.max_s < 0.0)
			stats.max_s = 0.0;
	
		stats.total_s = total_s;
		stats.standard_deviation_s = batch_deviation_s / sqrt_batch_size;
		stats.average_s = mean_s; 
		stats.batch_size = batch_size;
		stats.runs = iters;

		if(stats.average_s > 0)
			stats.normalized_standard_deviation_s = stats.standard_deviation_s / stats.average_s;

		//statss must be plausible
		ASSERT(stats.runs >= 0);
		ASSERT(stats.batch_size >= 0);
		ASSERT(stats.total_s >= 0.0);
		ASSERT(stats.average_s >= 0.0);
		ASSERT(stats.min_s >= 0.0);
		ASSERT(stats.max_s >= 0.0);
		ASSERT(stats.standard_deviation_s >= 0.0);
		ASSERT(stats.normalized_standard_deviation_s >= 0.0);

		return stats;
	}
	
	EXPORT void perf_do_not_optimize(const void* ptr) 
	{ 
		static volatile int __perf_always_zero = 0;
		if(__perf_always_zero == 0x7FFFFFFF)
		{
			volatile int* vol_ptr = (volatile int*) (void*) ptr;
			//If we would use the following line the compiler could infer that 
			//we are only really modifying the value at ptr. Thus if we did 
			// perf_do_not_optimize(long_array) it would gurantee no optimize only at the first element.
			//The precise version is also not very predictable. Often the compilers decide to only keep the first element
			// of the array no metter which one we actually request not to optimize. 
			//
			// __perf_always_zero = *vol_ptr;
			__perf_always_zero = vol_ptr[*vol_ptr];
		}
    }
	
	Perf_Stats perf_benchmark(f64 warmup, f64 time, isize batch_size, Benchamrk_Func func, void* context)
	{
		Perf_Counter counter = {0};
		int64_t total_clocks = (int64_t) ((f64) platform_perf_counter_frequency() * time);
		int64_t warmup_clocks = (int64_t) ((f64) platform_perf_counter_frequency() * warmup);

		int64_t start = platform_perf_counter();
		int64_t discard_time = 0;

		for(isize i = 0; ; i++)
		{
			int64_t before = platform_perf_counter();
			int64_t passed_clocks = before - start;
			if(passed_clocks >= total_clocks + discard_time)
				break;

			bool keep = func(i, context);
			int64_t after = platform_perf_counter();
			int64_t delta = after - before;
			
			//If we discarded the result prolongue the test time by the time we have wasted.
			//This is fairly imporant for benchmarks that require discarting for more complex setups.
			//For example to benchmark hash map removal we need to every once in a while repopulate the
			// hash map. We of course discard this setup time but if we didnt prolongue the time we 
			// would often just exit without making a single measurement.
			if(keep == false)
				discard_time += delta;

			if(keep && passed_clocks >= warmup_clocks + discard_time)
				perf_end_delta(&counter, delta);
		}

		return perf_get_stats(counter, batch_size);
	}

#endif