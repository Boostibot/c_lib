#ifndef JOT_PERF
#define JOT_PERF

#include <stdbool.h>
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

typedef struct Perf_Benchmark {
	Perf_Stats inline_stats;
	Perf_Counter counter;

	//Set only once. To reuse a benchmark struct (unusual)
	// for multiple benchmarks these must be set to 0!
	Perf_Stats* stats;
	int64_t start;
	int64_t time;
	int64_t warmup;
	int64_t batch_size;

	//Chnages on every iteration!
	int64_t iter;
	int64_t iter_begin_time;
} Perf_Benchmark;

//Returns the current time in nanoseconds. The time is relative to an arbitrary point in time, thus only difference of two perf_now() makes sense.
EXPORT int64_t		perf_now();
EXPORT int64_t		perf_freq(); //returns the frequency of the perf counter
EXPORT void			perf_submit(Perf_Counter* counter, int64_t measured);
EXPORT int64_t		perf_submit_atomic(Perf_Counter* counter, int64_t measured, bool detailed);
EXPORT Perf_Stats	perf_get_stats(Perf_Counter counter, int64_t batch_size);

//Maintains a benchmark. See example below for how to use this.
EXPORT bool perf_benchmark(Perf_Benchmark* bench, double time);

//Maintains a benchmark requiring manual measurement. Allows to submit more settings. 
// Measurements need to be added using perf_benchmark_submit() to register!
EXPORT bool perf_benchmark_custom(Perf_Benchmark* bench, Perf_Stats* stats_or_null, double warmup, double time, int64_t batch_size);
//Submits the measured time in nanoseconds to the benchmark. The measurement is discareded if warmup is still in progress. 
EXPORT void perf_benchmark_submit(Perf_Benchmark* bench, int64_t measured);
//#define PERF_AUTO_WARMUP -1.0

//Prevents the compiler from otpimizing away the variable (and thus its value) pointed to by ptr.
EXPORT void perf_do_not_optimize(const void* ptr) ;

//Needs implementation:
int64_t platform_perf_counter();
int64_t platform_perf_counter_frequency();
inline static bool    platform_atomic_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value);
inline static int32_t platform_atomic_add32(volatile int32_t* target, int32_t value);
inline static int64_t platform_atomic_add64(volatile int64_t* target, int64_t value);
inline static int32_t platform_atomic_sub32(volatile int32_t* target, int32_t value);
inline static int64_t platform_atomic_sub64(volatile int64_t* target, int64_t value);

#include <stdlib.h>
static void perf_benchmark_example() 
{	
	//For 3 seconds time the conctents of the loop and capture the resulting stats.
	Perf_Benchmark bench1 = {0};
	while(perf_benchmark(&bench1, 3)) {
		//`bench1.iter` is index of the current every iteration
		//`bench1.iter_begin_time` is the value of perf_now() at the start time of the current iteration 
		volatile double result = sqrt((double) bench1.iter); (void) result; //make sure the result is not optimized away
	}

	//do something with stats ...
	bench1.stats->average_s += 10; 

	//Sometimes it is necessary to do contiguous setup in order to
	// have data to benchmark with. In such a case every itration where the setup
	// occurs will be havily influenced by it. We can discard this iteration by
	// setting it.discard = true;

	//We benchmark the free function. In order to have something to free we need
	// to call malloc. But we dont care about malloc in this test => malloc 100
	// items and then free each. We simply dont submit the malloc timings.
	Perf_Stats stats = {0};
	void* ptrs[100] = {0};
	int count = 0;

	//Alternative way of doing benchmark loops, helpful to keep the `bench` variable scoped but the stats not 
	// (which is useful for organization when doing several different benchmarks within a single function). 
	//The `&stats` paramater is optional and when not specified the stats are stored inside the bench variable
	for(Perf_Benchmark bench = {0}; perf_benchmark_custom(&bench, &stats, 0.5, 3.5, 1); ) {
		if(count > 0)
		{
			int64_t before = perf_now();
			free(ptrs[--count]);
			perf_benchmark_submit(&bench, perf_now() - before);
		}
		else
		{
			count = 100;
			for(int i = 0; i < 100; i++)
				ptrs[i] = malloc(256);
		}
	};
}
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PERF_IMPL)) && !defined(JOT_PERF_HAS_IMPL)
#define JOT_PERF_HAS_IMPL

	
	EXPORT void perf_submit(Perf_Counter* counter, int64_t delta)
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
	
	EXPORT int64_t perf_submit_atomic(Perf_Counter* counter, int64_t delta, bool detailed)
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
	
	EXPORT int64_t perf_now()
	{
		return platform_perf_counter();
	}

	EXPORT int64_t perf_freq()
	{
		return platform_perf_counter_frequency();
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
		#if defined(__GNUC__) || defined(__clang__)
			__asm__ __volatile__("" : "+r"(ptr))
		#else
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
		#endif
    }

	EXPORT bool perf_benchmark_custom(Perf_Benchmark* bench, Perf_Stats* stats_or_null, double warmup, double time, int64_t batch_size)
	{
		int64_t now = perf_now();
		if(bench->start == 0)
		{
			bench->counter.frquency = perf_freq();
			bench->warmup = (int64_t) (warmup*bench->counter.frquency);
			bench->time = (int64_t) (time*bench->counter.frquency);
			bench->start = now;
			bench->batch_size = batch_size;
			bench->stats = stats_or_null;
			if(bench->stats == NULL)
				bench->stats = &bench->inline_stats;

			//so that after += 1 is 0
			bench->iter = -1;
		}

		bench->iter += 1;
		bench->iter_begin_time = now;
		int64_t ellpased = now - bench->start;
		if(ellpased <= bench->time)
			return true;
		else
		{
			*bench->stats = perf_get_stats(bench->counter, bench->batch_size);
			return false;
		}
	}

	EXPORT bool perf_benchmark(Perf_Benchmark* bench, double time)
	{
		int64_t last = bench->iter_begin_time;
		bool out = perf_benchmark_custom(bench, NULL, time/8, time, 1);

		if(last > 0)
			perf_benchmark_submit(bench, bench->iter_begin_time-last);

		//One more perf_now() so that we best isolate the actual timed code.
		bench->iter_begin_time = perf_now();
		return out;
	}

	EXPORT void perf_benchmark_submit(Perf_Benchmark* bench, int64_t measurement)
	{
		if(bench->iter_begin_time - bench->start > bench->warmup)
			perf_submit(&bench->counter, measurement);
	}

#endif