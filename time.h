#ifndef MODULE_TIME
#define MODULE_TIME

#include <stdint.h>
#ifndef EXTERNAL
	#define EXTERNAL 
#endif

#ifndef TIMEAPI
    #define TIMEAPI static inline
    #define TIMEAPI_INLINE_IMPL
#endif

#define SECOND_MILISECONDS      1000ll
#define SECOND_MIRCOSECONDS     1000000ll
#define SECOND_NANOSECONDS      1000000000ll
#define SECOND_PICOSECONDS      1000000000000ll
#define MILISECOND_NANOSECONDS  1000000000ll

#define MINUTE_SECONDS          60ll
#define HOUR_SECONDS            3600ll
#define DAY_SECONDS             86400ll
#define WEEK_SECONDS            604800ll
#define YEAR_SECONDS            31556952ll

TIMEAPI int64_t epoch_time();       //returns time since the epoch in microseconds
TIMEAPI int64_t clock_ticks();      //returns precise as possible yet long term stable time since unspecified point in time (last boot) 
TIMEAPI int64_t clock_ticks_freq(); //returns the frequency of clock_ticks
TIMEAPI int64_t clock_ns();         //returns time in nanoseconds since since unspecified point in time (last boot) 
EXTERNAL double clock_sec();        //returns time in seconds since last call to clock_sec_set(x) plus x. 
EXTERNAL float  clock_secf();       //returns time in seconds since last call to clock_sec_set(x) plus x as float
EXTERNAL double clock_sec_set(double to_time); //sets the base for clock_sec and return the previous time 

//@NOTE: 
//For clock_sec we might be scared that the int64_t to double conversion will cost us precision for sufficiently large 
// performance counter values. In practice this extremely hard to achieve. On windows performance counter has
// frequency of 10Mhz = period of 1e-7 seconds. Double is able to represent numbers up to 2^53 without loosing 
// any precision, that is around 9e15. Thus it is able to represent numbers up to ~1e9 with precision of 1e-7 seconds.
// This means that we will start to lose precision after 1e9 seconds = 31 years. If you dont run your program for 31 
// years you should be fine.   

#endif // !MODULE_TIME

//INLINE IMPLEMENTATION
#if (defined(MODULE_IMPL_ALL) || defined(TIMEAPI_INLINE_IMPL)) && !defined(MODULE_HAS_INLINE_IMPL_FILE)
#define MODULE_HAS_INLINE_IMPL_FILE
    extern int     g_clock_sec_init;
    extern int64_t g_clock_sec_offset;
    extern double  g_clock_sec_period;
    extern int64_t g_clock_sec_freq;

    #if defined(_WIN32) || defined(_WIN64)

        #ifdef __cplusplus
        extern "C" {
        #endif
        typedef union _LARGE_INTEGER LARGE_INTEGER;
        typedef struct _FILETIME FILETIME;
        __declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER* out);
        __declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER* out);
        __declspec(dllimport) void __stdcall GetSystemTimeAsFileTime(FILETIME* lpSystemTimeAsFileTime);
        #ifdef __cplusplus
        }
        #endif

        TIMEAPI int64_t clock_ticks()
        {
            int64_t out = 0;
            QueryPerformanceCounter((LARGE_INTEGER*) (void*) &out);
            return out;
        }

        TIMEAPI int64_t clock_ticks_freq()
        {
            if(g_clock_sec_freq == 0)
                QueryPerformanceFrequency((LARGE_INTEGER*) (void*) &g_clock_sec_freq);
            return g_clock_sec_freq;
        }

        TIMEAPI int64_t epoch_time()
        {
            uint64_t filetime = 0;
            GetSystemTimeAsFileTime((FILETIME*) (void*) &filetime);
            return filetime / 10 - 11644473600000000LL;
        }
        
        #if defined(_MSC_VER) && !defined(__clang__)
            #include <intrin.h>
            __declspec(noinline) static int64_t _clock_ns_unusual()
            {
                uint64_t counter = (uint64_t) clock_ticks();
                uint64_t freq = (uint64_t) clock_ticks_freq(); 
                uint64_t hi, lo = _umul128(counter, 1000000000ull, &hi);
                uint64_t rem, quo = _udiv128(hi, lo, freq, &rem);
                return (int64_t) quo;
            }
        #else
            __atribute__((noinline)) static int64_t _clock_ns_unusual()
            {
                uint64_t counter = (uint64_t) clock_ticks();
                uint64_t freq = (uint64_t) clock_ticks_freq(); 
                return (int64_t) ((__uint128_t) counter*1000000000ull/freq);
            }
        #endif

        TIMEAPI int64_t clock_ns()
        {
            //QPC is rn hardcoded to return 10Mhz so we exploit that
            if(g_clock_sec_freq != 10000000) 
                return _clock_ns_unusual();

            return clock_ticks()*100;
        }

    #elif defined(__linux__) || (defined(__APPLE__) && defined(__MACH__))
        #include <time.h>
        TIMEAPI int64_t clock_ticks()
        {
            struct timespec ts = {0};
            (void) clock_gettime(CLOCK_MONOTONIC_RAW , &ts);
            return (int64_t) ts.tv_nsec + ts.tv_sec * 1000000000LL;
        }
        TIMEAPI int64_t clock_ticks_freq() { return (int64_t) 1000000000LL; }
        TIMEAPI int64_t clock_ns()         { return clock_ticks(); }
        TIMEAPI int64_t epoch_time()       
        { 
            struct timespec ts = {0};
            (void) clock_gettime(CLOCK_REALTIME , &ts);
            return (int64_t) ts.tv_nsec/1000 + ts.tv_sec*1000000LL;
        }
    #endif
    
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_TIME)) && !defined(MODULE_HAS_IMPL_TIME)
#define MODULE_HAS_IMPL_TIME
    int     g_clock_sec_init = 0;
    int64_t g_clock_sec_offset = 0;
    double  g_clock_sec_period = 0;
    int64_t g_clock_sec_freq = 0;
    EXTERNAL double clock_sec_set(double to_time)
    {
        int64_t counter = clock_ticks();
        double prev_now = (double) (counter - g_clock_sec_offset)*g_clock_sec_period;
        int64_t freq = clock_ticks_freq();
        g_clock_sec_offset = counter + (int64_t) (to_time*freq + 0.5);
        g_clock_sec_period = 1.0 / freq;
        g_clock_sec_freq = freq;
        g_clock_sec_init = 1;
        return prev_now;
    }
    
    EXTERNAL double clock_sec()
    {
        if(g_clock_sec_init == 0)
            clock_sec_set(0);
        return (double) (clock_ticks() - g_clock_sec_offset)*g_clock_sec_period;
    }

    EXTERNAL float clock_secf()
    {
        return (float) clock_sec();
    }
#endif

