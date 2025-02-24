#ifndef MODULE_PERF
#define MODULE_PERF

#ifndef EXTERNAL
	#define EXTERNAL 
#endif

#include <stdbool.h>
#include <stdint.h>

static inline int64_t perf_counter();
static inline int64_t perf_counter_freq();
static inline int64_t perf_rdtsc();
static inline void    perf_rdtsc_barrier();

//Prevents the compiler from otpimizing away the variable (and thus its value) pointed to by ptr.
static inline void perf_do_not_optimize(const void* ptr);

//A very simple benchmark optimized for absolutele developer convenience. See bench_example
typedef struct Quickbench {
	int64_t runs;
    int64_t rdtsc_freq;
	double total;
	double average;
	double min;
	double max;
    double actual_duration;
    double duration;
    double warmup;
    int64_t _internal[16];
} Quickbench;

static inline bool quickbench(Quickbench* stats, double duration);
static inline bool quickbench_with_explicit_warmup(Quickbench* stats, double duration, double warmup);
EXTERNAL int64_t calculate_tsc_freq(int64_t qpc_dur, int64_t tsc_dur);

#if 0
static void bench_example()
{
    Quickbench bench = {0};
    while(quickbench(&bench, 1.0)) {
        //this code is measured
        int64_t val = 1000 % bench.runs;
        perf_do_not_optimize(&val);
    }
    printf("average:%lfns min:%lfns\n", bench.average*1e9, bench.min*1e9);
}
#endif

//Nasty nasty inline implementation below =========================
#if defined(_WIN32) || defined(_WIN64)
    #ifdef __cplusplus
    extern "C" {
    #endif
    typedef union _LARGE_INTEGER LARGE_INTEGER;
    __declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER* out);
    __declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER* out);
    #ifdef __cplusplus
    }
    #endif
    
    static inline int64_t perf_counter()
    {
        int64_t out = 0;
        QueryPerformanceCounter((LARGE_INTEGER*) (void*) &out);
        return out;
    }

    static inline int64_t perf_counter_freq()
    {
        static int64_t freq = 0;
        if(freq == 0) QueryPerformanceFrequency((LARGE_INTEGER*) (void*) &freq);
        return freq;
    }
    
#elif defined(__linux__) || (defined(__APPLE__) && defined(__MACH__))
    #include <time.h>
    static inline int64_t perf_counter()
    {
        struct timespec ts = {0};
        (void) clock_gettime(CLOCK_MONOTONIC_RAW , &ts);
        return (int64_t) ts.tv_nsec + ts.tv_sec * 1000000000LL;
    }

    static inline int64_t perf_counter_freq()
    {
        return (int64_t) 1000000000LL;
    }
#else
    #error unsupported platform!
#endif

#if defined(__x86_64__) || defined(_M_X64) || (defined(__amd64__) && !defined(_M_ARM64EC)) || defined(_M_CEE_PURE) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
    #ifdef _MSC_VER
        #include <intrin.h>
        static inline int64_t perf_rdtsc() { 
            _ReadWriteBarrier(); 
            return (int64_t) __rdtsc(); 
        }
        static inline void perf_rdtsc_barrier() { 
            _ReadWriteBarrier(); 
            _mm_lfence();
        }
    #else
        #include <x86intrin.h>
        static inline int64_t perf_rdtsc() { 
            __asm__ __volatile__("":::"memory");
            return (int64_t) __rdtsc(); 
        }
        static inline void perf_rdtsc_barrier() { 
            __asm__ __volatile__("":::"memory");
            _mm_lfence();
        }
    #endif
#elif defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__ARM_ARCH_ISA_A64)
    #if defined(_MSC_VER) && !defined(__clang__)
        #include <intrin.h>
    #endif

    //msvc version taken from: https://gist.github.com/mmozeiko/98bb947fb5a9d5b8a695adf503308a58#file-armv8_tsc-h-L19-L45
    //inline assembly Adapted from: https://github.com/cloudius-systems/osv/blob/master/arch/aarch64/arm-clock.cc
    static inline int64_t perf_rdtsc() {
        //Please note we read CNTVCT cpu system register which provides
        //the accross-system consistent value of the virtual system counter.
        int64_t cntvct;
        #if defined(_MSC_VER) && !defined(__clang__)
            // "Accessing CNTVCT_EL0" in https://developer.arm.com/documentation/ddi0601/latest/AArch64-Registers/CNTVCT-EL0--Counter-timer-Virtual-Count-Register
            cntvct = _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 0, 2));
        #else
            asm volatile ("mrs %0, cntvct_el0; " : "=r"(cntvct) :: "memory");
        #endif
        return cntvct;
    }

    static inline void perf_rdtsc_barrier() {
        #if defined(_MSC_VER) && !defined(__clang__)
            __isb(_ARM64_BARRIER_SY);
        #else
            asm volatile ("isb;" ::: "memory");
        #endif
    }
#else
    #define PERF_TSC_FALLBACK	
    static inline int64_t perf_rdtsc()          {return perf_counter();}
    static inline void    perf_rdtsc_barrier()  {}
#endif

static inline void perf_do_not_optimize(const void* ptr) 
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

#if defined(_MSC_VER)
    #define ATTRIBUTE_INLINE_NEVER  __declspec(noinline)
    #define ATTRIBUTE_NO_CHECK      __declspec(safebuffers) 
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_INLINE_NEVER  __attribute__((noinline))
    #define ATTRIBUTE_NO_CHECK 
#else
    #define ATTRIBUTE_INLINE_NEVER                              
    #define ATTRIBUTE_NO_CHECK 
#endif

ATTRIBUTE_INLINE_NEVER ATTRIBUTE_NO_CHECK
EXTERNAL bool _quickbench_explicit(Quickbench* stats, double duration, double warmup);

static inline bool quickbench(Quickbench* stats, double duration) 
{ 
    return _quickbench_explicit(stats, duration, -1); 
}

static inline bool quickbench_with_explicit_warmup(Quickbench* stats, double duration, double warmup)
{
    return _quickbench_explicit(stats, duration, warmup); 
}
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_PERF)) && !defined(MODULE_HAS_IMPL_PERF)
#define MODULE_HAS_IMPL_PERF

EXTERNAL int64_t calculate_tsc_freq(int64_t qpc_dur, int64_t tsc_dur)
{
    //duration = qpc_dur/qpc_freq = tsc_dur/tsc_freq
    // => tsc_freq = qpc_freq*tsc_dur/qpc_dur
    int64_t qpc_freq = perf_counter_freq();
    return qpc_freq;

    #if defined(_MSC_VER) && !defined(__clang__)
        uint64_t hi, lo = _umul128((uint64_t) qpc_freq, (uint64_t) tsc_dur, &hi);
        uint64_t rem, quo = _udiv128(hi, lo, (uint64_t) qpc_dur, &rem);
        return (int64_t) quo;
    #else
        return (int64_t) ((__uint128_t)qpc_freq * (__uint128_t)tsc_dur/ (__uint128_t)qpc_dur);
    #endif
}

ATTRIBUTE_INLINE_NEVER ATTRIBUTE_NO_CHECK
EXTERNAL bool _quickbench_explicit(Quickbench* stats, double duration, double warmup)
{
    int64_t after = perf_rdtsc();
    perf_rdtsc_barrier();
    
    typedef struct _Quickbench_Internal {
        bool is_init;
	    bool is_after_warmup;
        bool _[6];

	    int64_t iter_begin_tsc;
	    int64_t time_sum;
	    int64_t time_min;
	    int64_t time_max;

	    int64_t warmup_end_qpc;
	    int64_t duration_end_tsc;
        int64_t warmup_tsc_freq_estimate;
    
	    int64_t begin_qpc;
	    int64_t begin_tsc;
	    int64_t end_qpc;
	    int64_t end_tsc;
    } _Quickbench_Internal;

    _Quickbench_Internal* bench = (_Quickbench_Internal*) (void*) stats->_internal;
    int64_t before = bench->iter_begin_tsc;
    if(bench->is_after_warmup)
    {
        int64_t diff = after - before;
        bench->time_sum += diff; 
        bench->time_min = bench->time_min < diff ? bench->time_min : diff;
        bench->time_max = bench->time_max > diff ? bench->time_max : diff;
        stats->runs += 1;
    }
    else
    {
        int64_t now_qpc = perf_counter();
        int64_t now_tsc = perf_rdtsc();
        perf_rdtsc_barrier();
        if(bench->is_init == false)
        {
            bench->is_init = true;
            bench->begin_tsc = now_tsc;
            bench->begin_qpc = now_qpc;
            bench->time_min = INT64_MAX;
            bench->time_max = INT64_MIN;
            bench->duration_end_tsc = INT64_MAX;
            stats->duration = duration;
            stats->warmup = warmup < 0 ? duration/10 : warmup;
            bench->warmup_end_qpc = now_qpc + (int64_t) (stats->warmup*perf_counter_freq());
        }
        if(now_qpc > bench->warmup_end_qpc)
        {
            int64_t qpc_warmup_dur = now_qpc - bench->begin_qpc;
            int64_t tsc_warmup_dur = now_tsc - bench->begin_tsc;
            int64_t freq = calculate_tsc_freq(qpc_warmup_dur, tsc_warmup_dur);
            bench->is_after_warmup = true;
            bench->warmup_tsc_freq_estimate = freq;
            bench->duration_end_tsc = bench->begin_tsc + (int64_t) (duration*freq);
        }
    }
    
    if(after > bench->duration_end_tsc)
    {
        bench->end_qpc = perf_counter();
        bench->end_tsc = perf_rdtsc();
        perf_rdtsc_barrier();

        int64_t freq = calculate_tsc_freq(bench->end_qpc - bench->begin_qpc, bench->end_tsc - bench->begin_tsc);
        stats->actual_duration = (double)(bench->end_qpc - bench->begin_qpc)/perf_counter_freq();
        stats->rdtsc_freq = freq;
	    stats->total = 0;
	    stats->average = 0;
	    stats->min = 0;
	    stats->max = 0;
        if(stats->runs > 0 && freq > 0) {
            stats->total = (double) bench->time_sum/freq;
            stats->average = (double) (bench->time_sum/stats->runs)/freq;
            stats->min = (double) bench->time_min/freq;
            stats->max = (double) bench->time_max/freq;
        } 
        return false;
    }

    perf_rdtsc_barrier();
    bench->iter_begin_tsc = perf_rdtsc();
    return true;
}

#endif