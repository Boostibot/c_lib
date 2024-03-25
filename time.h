#ifndef JOT_TIME
#define JOT_TIME

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

#include <time.h>
typedef struct tm Posix_Date;
EXPORT int64_t epoch_time_from_global_date(Posix_Date date);
EXPORT int64_t epoch_time_from_local_date(Posix_Date date);
EXPORT Posix_Date global_date_from_epoch_time(int64_t epoch_time);
EXPORT Posix_Date local_date_from_epoch_time(int64_t epoch_time);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_TIME_IMPL)) && !defined(JOT_TIME_HAS_IMPL)
#define JOT_TIME_HAS_IMPL

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
    i64 startup = platform_epoch_time_startup();
    i64 delta = epoch_time - startup;
    return (f64) delta / (f64) SECOND_MIRCOSECONDS;
}

EXPORT i64 clock_time_to_epoch_time(f64 time)
{
    i64 startup = platform_epoch_time_startup();
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
//@NOTE:
//We might be rightfully scared that after some ammount of time the clock_s() will get sufficiently large and
// we will start loosing pression. This is however not a problem. If we assume the perf_counter_frequency is equal to 
// 10Mhz = 1e7 (which is very common) then we would like the clock_s to have enough precision to represent
// 1 / 10Mhz = 1e-7. This precission is held up untill 1e9 secons have passed which is roughly 31 years. 
// => Please dont run your program for more than 31 years or you will loose precision
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

EXPORT int64_t epoch_time_from_global_date(Posix_Date date)
{
    return (int64_t) mktime(&date) * 1000 * 1000;
}

EXPORT int64_t epoch_time_from_local_date(Posix_Date date)
{
    int64_t now = platform_epoch_time();

    //Only recalculate time delta every second.
    static int64_t last_now = 0;
    static int64_t diff = 0;
    if(now - last_now > SECOND_MIRCOSECONDS)
    {
        int64_t local_now = epoch_time_from_global_date(local_date_from_epoch_time(now));
        diff = now - local_now;
    }

    int64_t time = epoch_time_from_global_date(date);
    int64_t local_time = time + diff;
    return local_time;
}

EXPORT Posix_Date global_date_from_epoch_time(int64_t epoch_time)
{
    time_t copy = (time_t) (epoch_time / 1000 / 1000);
    return *gmtime(&copy);
}

EXPORT Posix_Date local_date_from_epoch_time(int64_t epoch_time)
{
    time_t copy = (time_t) (epoch_time / 1000 / 1000);
    return *localtime(&copy);
}

#endif // !JOT_TIME
