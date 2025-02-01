#ifndef MODULE_PERF
#define MODULE_PERF

//TODO MAKE SIMPLER

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

typedef struct Benchmark_Stats {
	int64_t runs;
	int64_t batch_size;

	double total_s;
	double average_s;
	double min_s;
	double max_s;
} Benchmark_Stats;

typedef struct Benchmark {
	int64_t start;
	int64_t duration_end;
	int64_t warmup_end;
	int64_t frequency;

	//Chnages on every iteration!
	int64_t iter;
	int64_t iter_begin_time;
	int64_t time_sum;
	int64_t time_min;
	int64_t time_max;
} Benchmark;

//Maintains a benchmark. See example below for how to use this.
EXTERNAL bool benchmark_with_custom_warmup(Benchmark* bench, double warmup, double time);
EXTERNAL bool benchmark(Benchmark* bench, double time);
EXTERNAL Benchmark_Stats benchmark_get_stats(Benchmark bench, int64_t batch_size);

//Prevents the compiler from otpimizing away the variable (and thus its value) pointed to by ptr.
static void benchmark_do_not_optimize(const void* ptr);

static void perf_do_not_optimize(const void* ptr) 
{ 
	#if defined(__GNUC__) || defined(__clang__)
		__asm__ __volatile__("" : "+r"(ptr));
	#else
		static volatile int __perf_always_zero = 0;
		if(__perf_always_zero != 0)
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

	


#endif

#define MODULE_IMPL_ALL

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_PERF)) && !defined(MODULE_HAS_IMPL_PERF)
#define MODULE_HAS_IMPL_PERF




	#endif
#endif