#ifndef JOT_PERF
#define JOT_PERF

#include <stdbool.h>
#include <math.h>
#include <stdint.h>

#ifndef ASSERT
	#include <assert.h>
	#define ASSERT(x) assert(x)
#endif

#ifndef EXTERNAL
	#define EXTERNAL 
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
EXTERNAL int64_t		perf_now();
EXTERNAL int64_t		perf_freq(); //returns the frequency of the perf counter
EXTERNAL int64_t		perf_submit(Perf_Counter* counter, int64_t measured);
EXTERNAL int64_t		perf_submit_no_init(Perf_Counter* counter, int64_t measured);
EXTERNAL Perf_Stats	perf_get_stats(Perf_Counter counter, int64_t batch_size);
EXTERNAL Perf_Counter perf_counter_merge(Perf_Counter a, Perf_Counter b, bool* could_combine_everything_or_null);
EXTERNAL Perf_Counter	perf_counter_init(int64_t mean_estimate);

//Maintains a benchmark. See example below for how to use this.
EXTERNAL bool perf_benchmark(Perf_Benchmark* bench, double time);

//Maintains a benchmark requiring manual measurement. Allows to submit more settings. 
// Measurements need to be added using perf_benchmark_submit() to register!
EXTERNAL bool perf_benchmark_custom(Perf_Benchmark* bench, Perf_Stats* stats_or_null, double warmup, double time, int64_t batch_size);
//Submits the measured time in nanoseconds to the benchmark. The measurement is discareded if warmup is still in progress. 
EXTERNAL void perf_benchmark_submit(Perf_Benchmark* bench, int64_t measured);

//Needs implementation:
int64_t platform_perf_counter();
int64_t platform_perf_counter_frequency();

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

	EXTERNAL Perf_Counter perf_counter_init(int64_t mean_estimate)
	{
		Perf_Counter out = {0};
		out.frquency = platform_perf_counter_frequency();
		out.max_counter = INT64_MIN;
		out.min_counter = INT64_MAX;
		out.mean_estimate = mean_estimate;
		return out;
	}
	
	EXTERNAL int64_t perf_submit_no_init(Perf_Counter* counter, int64_t delta)
	{
		int64_t offset_delta = delta - counter->mean_estimate;
		counter->counter += delta;
		counter->sum_of_squared_offset_counters += offset_delta*offset_delta;
		counter->min_counter = MIN(counter->min_counter, delta);
		counter->max_counter = MAX(counter->max_counter, delta);
		counter->runs += 1; 
		return counter->runs - 1;
	}

	EXTERNAL int64_t perf_submit(Perf_Counter* counter, int64_t delta)
	{
		ASSERT(counter != NULL && delta >= 0 && "invalid submit");
		if(counter->frquency == 0)
			*counter = perf_counter_init(delta);
	
		return perf_submit_no_init(counter, delta);
	}
	
	//@TODO: RDTSC!
	EXTERNAL int64_t perf_now()
	{
		return platform_perf_counter();
	}

	EXTERNAL int64_t perf_freq()
	{
		return platform_perf_counter_frequency();
	}

	EXTERNAL Perf_Counter perf_counter_merge(Perf_Counter a, Perf_Counter b, bool* could_combine_everything_or_null)
	{
		Perf_Counter out = {0};
		out.max_counter = a.max_counter > b.max_counter ? b.max_counter : a.max_counter;
		out.min_counter = a.min_counter < b.min_counter ? b.min_counter : a.min_counter;
		out.frquency = a.frquency > b.frquency ? b.frquency : a.frquency;
		out.runs = a.runs + b.runs;
		out.counter = a.counter + b.counter;
		if(a.mean_estimate == b.mean_estimate)
		{
			out.mean_estimate = a.mean_estimate;
			out.sum_of_squared_offset_counters = a.sum_of_squared_offset_counters + b.sum_of_squared_offset_counters;
			if(could_combine_everything_or_null)
				*could_combine_everything_or_null = true;
		}
		else
		{
			if(could_combine_everything_or_null)
				*could_combine_everything_or_null = false;
		}

		return out;
	}

	EXTERNAL Perf_Stats perf_get_stats(Perf_Counter counter, int64_t batch_size)
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

	EXTERNAL bool perf_benchmark_custom(Perf_Benchmark* bench, Perf_Stats* stats_or_null, double warmup, double time, int64_t batch_size)
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

	EXTERNAL bool perf_benchmark(Perf_Benchmark* bench, double time)
	{
		int64_t last = bench->iter_begin_time;
		bool out = perf_benchmark_custom(bench, NULL, time/8, time, 1);

		if(last > 0)
			perf_benchmark_submit(bench, bench->iter_begin_time-last);

		//One more perf_now() so that we best isolate the actual timed code.
		bench->iter_begin_time = perf_now();
		return out;
	}

	EXTERNAL void perf_benchmark_submit(Perf_Benchmark* bench, int64_t measurement)
	{
		if(bench->iter_begin_time - bench->start > bench->warmup)
			perf_submit(&bench->counter, measurement);
	}

#endif