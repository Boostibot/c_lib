#ifndef LIB_TIME
#define LIB_TIME

#include "platform.h"
#include "defines.h"

#define SECOND_MILISECONDS  ((int64_t) 1000)
#define SECOND_MIRCOSECONDS ((int64_t) 1000000)
#define SECOND_NANOSECONDS  ((int64_t) 1000000000)
#define SECOND_PICOSECONDS  ((int64_t) 1000000000000)
#define MILISECOND_NANOSECONDS (SECOND_NANOSECONDS / SECOND_MILISECONDS)

#define MINUTE_SECONDS ((int64_t) 60)
#define HOUR_SECONDS ((int64_t) 60 * MINUTE_SECONDS)
#define DAY_SECONDS ((int64_t) 24 * MINUTE_SECONDS)
#define WEEK_SECONDS ((int64_t) 7 * DAY_SECONDS)
#define YEAR_SECONDS (int64_t) 31556952

EXPORT f64 platform_perf_counter_frequency_f64();
EXPORT f32 platform_perf_counter_frequency_f32();

EXPORT f64 epoch_time_to_clock_time(i64 epoch_time);
EXPORT i64 clock_time_to_epoch_time(f64 time);

EXPORT int64_t clock_ns();
EXPORT f64 clock_s();
EXPORT f32 clock_s32();

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_TIME_IMPL)) && !defined(LIB_TIME_HAS_IMPL)
#define LIB_TIME_HAS_IMPL

EXPORT f64 platform_perf_counter_frequency_f64()
{
    static f64 freq = 0;
    if(freq == 0)
        freq = (f64) platform_perf_counter_frequency(); 
    return freq;
}

EXPORT f32 platform_perf_counter_frequency_f32()
{
    static f32 freq = 0;
    if(freq == 0)
        freq = (f32) platform_perf_counter_frequency(); 
    return freq;
}

EXPORT f64 epoch_time_to_clock_time(i64 epoch_time)
{
    i64 startup = platform_startup_epoch_time();
    i64 delta = epoch_time - startup;
    return (f64) delta / (f64) SECOND_MIRCOSECONDS;
}

EXPORT i64 clock_time_to_epoch_time(f64 time)
{
    i64 startup = platform_startup_epoch_time();
    i64 delta = (i64) (time * SECOND_MIRCOSECONDS);
    return startup + delta;
}

//Returns the time from the startup time in nanoseconds
EXPORT int64_t clock_ns()
{
    int64_t freq = platform_perf_counter_frequency();
    int64_t counter = platform_perf_counter() - platform_perf_counter_startup();

    int64_t sec_to_nanosec = 1000000000;
    //We assume _perf_counter_base is set to some reasonable thing so this will not overflow
    // (with this we are only able to represent 1e12 secons (A LOT) without overflowing)
    return counter * sec_to_nanosec / freq;
}

//Returns the time from the startup time in seconds
EXPORT f64 clock_s()
{
    f64 freq = platform_perf_counter_frequency_f64();
    f64 counter = (f64) (platform_perf_counter() - platform_perf_counter_startup());
    return counter / freq;
}

EXPORT f32 clock_s32()
{
    f32 freq = platform_perf_counter_frequency_f32();
    f32 counter = (f32) (platform_perf_counter() - platform_perf_counter_startup());
    return counter / freq;
}

//@NOTE:
//We might be rightfully scared that after some ammount of time the clock_s() will get sufficiently large and
// we will start loosing pression. This is however not a problem. If we assume the perf_counter_frequency is equal to 
// 10Mhz = 1e7 (which is very common) then we would like the clock_s to have enough precision to represent
// 1 / 10Mhz = 1e-7. This precission is held up untill 1e9 secons have passed which is roughly 31 years. 
// => Please dont run your program for more than 31 years or you will loose precision

#endif // !LIB_TIME
