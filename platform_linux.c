#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <limits.h>

typedef struct Platform_State {
    int64_t startup_perf_counter;
    int64_t startup_epoch_time;
    int64_t startup_local_epoch_time;
} Platform_State;

Platform_State gp_state = {0};

static int64_t untested_stack_overflow_calendar_to_time_t(int64_t sec, int64_t min, int64_t hour, int64_t day, int64_t month, int64_t year);
static int64_t platform_startup_local_epoch_time();

void platform_init()
{
    platform_perf_counter_startup();
    platform_startup_local_epoch_time();
    platform_startup_epoch_time();
}

void platform_deinit()
{
    memset(&gp_state, 0, sizeof gp_state);
}

void platform_set_internal_allocator(Platform_Allocator allocator)
{
    (void) allocator;
}

void platform_abort()
{
    abort();
}

void platform_terminate()
{
    exit(0);
}

//=========================================
// Timings 
//=========================================

#define _SECOND_MILLISECONDS    ((int64_t) 1000LL)
#define _SECOND_MICROSECS       ((int64_t) 1000000LL)
#define _SECOND_NANOSECS        ((int64_t) 1000000000LL)

typedef long long lli;

int64_t platform_perf_counter()
{
    struct timespec ts = {0};
    (void) clock_gettime(CLOCK_MONOTONIC_RAW , &ts);
    return (int64_t) ts.tv_nsec + ts.tv_sec * _SECOND_NANOSECS;
}

int64_t platform_perf_counter_frequency()
{
	return _SECOND_NANOSECS;
}

int64_t platform_perf_counter_startup()
{
    if(gp_state.startup_perf_counter == 0)
        gp_state.startup_perf_counter = platform_perf_counter();

    return gp_state.startup_perf_counter;
}

#if 0
typedef struct Platform_Calendar_Time {
    int32_t year;       // any
    int8_t month;       // [0, 12)
    int8_t day_of_week; // [0, 7) where 0 is sunday
    int8_t day;         // [0, 31] !note the end bracket!
    
    int8_t hour;        // [0, 24)
    int8_t minute;      // [0, 60)
    int8_t second;      // [0, 60)
    
    int16_t millisecond; // [0, 1000)
    int16_t microsecond; // [0, 1000)
    //int16_t day_of_year; // [0, 365]
} Platform_Calendar_Time;
#endif

int64_t platform_epoch_time()
{
    struct timespec ts = {0};
    (void) clock_gettime(CLOCK_REALTIME , &ts);
    return (int64_t) ts.tv_nsec / (_SECOND_NANOSECS / _SECOND_MICROSECS) + ts.tv_sec * _SECOND_MICROSECS;
}

int64_t platform_startup_epoch_time()
{
    if(gp_state.startup_epoch_time == 0)
        gp_state.startup_epoch_time = platform_epoch_time();

    return gp_state.startup_epoch_time;
}

void print_calendar_time(const char* label, Platform_Calendar_Time now)
{
    printf("%s%i/%i/%i %02i:%02i:%02i %03i\n", label, now.year, now.month + 1, now.day + 1, now.hour, now.minute, now.second, now.millisecond);

}

static int64_t platform_startup_local_epoch_time()
{
    if(gp_state.startup_local_epoch_time == 0)
    {
        int64_t startup_microsecond = platform_startup_epoch_time();
        time_t startup_seconds = (time_t) (startup_microsecond / _SECOND_MICROSECS);
        struct tm *converted = localtime(&startup_seconds);
        assert(converted);

        //@TODO: pull out this ugly untested_stack_overflow_calendar_to_time_t so that it gets called only at one place
        int64_t local_startup_seconds = untested_stack_overflow_calendar_to_time_t(converted->tm_sec, converted->tm_min, converted->tm_hour, converted->tm_mday, converted->tm_mon+1, converted->tm_year + 1900);
        gp_state.startup_local_epoch_time = local_startup_seconds*_SECOND_MICROSECS + startup_microsecond%_SECOND_MICROSECS; 

        //@TODO: remove
        if(0)
        {
            printf("converted mon: %lli\n", (lli) converted->tm_mon);
            printf("startup:       %lli\n", (lli) startup_microsecond);
            printf("startup_local: %lli\n", (lli) gp_state.startup_local_epoch_time);
            Platform_Calendar_Time startup = platform_epoch_time_to_calendar_time(startup_microsecond);
            Platform_Calendar_Time startup_local = platform_epoch_time_to_calendar_time(gp_state.startup_local_epoch_time);
            print_calendar_time("startup:       ", startup);
            print_calendar_time("startup_local: ", startup_local);
        }
    }   
}

int64_t platform_local_epoch_time()
{
    int64_t offset_micro = platform_epoch_time() - platform_startup_epoch_time();
    int64_t local_epoch_time = platform_startup_local_epoch_time() + offset_micro;

    return local_epoch_time;
} 


Platform_Calendar_Time platform_epoch_time_to_calendar_time(int64_t epoch_time_usec)
{
    time_t epoch_seconds = (time_t) (epoch_time_usec / _SECOND_MICROSECS);
    struct tm *converted = gmtime(&epoch_seconds);
    assert(converted);

    Platform_Calendar_Time out = {0};
    out.year = converted->tm_year + 1900;
    out.month = converted->tm_mon;
    out.day_of_week = converted->tm_wday;
    out.day = converted->tm_mday - 1;
    out.hour = converted->tm_hour;
    out.minute = converted->tm_min;
    out.second = converted->tm_sec;
    out.millisecond = epoch_time_usec % _SECOND_MILLISECONDS;
    out.microsecond = epoch_time_usec % _SECOND_MICROSECS;
    return out;
}

//see: https://stackoverflow.com/a/57744744
//Note that both day and month is one based
static int64_t untested_stack_overflow_calendar_to_time_t(int64_t sec, int64_t min, int64_t hour, int64_t day, int64_t month, int64_t year)
{
  // Cumulative days for each previous month of the year
  const int64_t mdays[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
  // Year is to be relative to the epoch start
  year -= 1970;
  // Compensation of the non-leap years
  int64_t minusYear = 0;
  // Detect potential lead day (February 29th) in this year?
  if ( month >= 3 )
  {
    // Then add this year into "sum of leap days" computation
    year++;
    // Compute one year less in the non-leap years sum
    minusYear = 1;
  }

  #define _MOD(x, N) (((x) % (N) + (N)) % (N))
  int64_t prev_month =_MOD(month - 1, 12);  

  return (int64_t)(
    // + Seconds from computed minutes
    60LL * (
      // + Minutes from computed hours
      60LL * (
        // + Hours from computed days
        24LL * (
          // + Day (zero index)
          day - 1
          // + days in previous months (leap day not included)
          + mdays[prev_month]
          // + days for each year divisible by 4 (starting from 1973)
          + ( ( year + 1LL ) / 4LL )
          // - days for each year divisible by 100 (starting from 2001)
          - ( ( year + 69LL ) / 100LL )
          // + days for each year divisible by 400 (starting from 2001)
          + ( ( year + 369LL ) / 100LL / 4LL )
          // + days for each year (as all are non-leap years) from 1970 (minus this year if potential leap day taken into account)
          + ( 5 * 73 /*=365*/ ) * ( year - minusYear )
          // + Hours
        ) + hour
        // + Minutes
      ) + min 
      // + Seconds
    ) + sec
  );
}


//Converts calendar time to the precise epoch time (micro second time since unix epoch)
int64_t platform_calendar_time_to_epoch_time(Platform_Calendar_Time calendar_time)
{
    Platform_Calendar_Time c = calendar_time;
    int64_t seconds = untested_stack_overflow_calendar_to_time_t(
        c.second, c.minute, c.hour, c.day + 1, c.month, c.year);

    int64_t microseconds = seconds * _SECOND_MICROSECS;
    microseconds += calendar_time.microsecond;

    return microseconds;
}

//=========================================
// Debug
//=========================================

#include <execinfo.h>
int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count)
{
    //@TODO: preallocate on heap!
    #define MAX_STACK 256
    static void* stack_ptrs[MAX_STACK] = {0};
    int64_t found_size = backtrace(stack_ptrs, stack_size);
    int64_t not_skipped_size = found_size - skip_count - 1;
    if(not_skipped_size < 0)
        not_skipped_size = 0;

    memcpy(stack, stack_ptrs + skip_count, (long long) not_skipped_size*sizeof(void*));
    return not_skipped_size;
}

void platform_translate_call_stack(Platform_Stack_Trace_Entry* tanslated, const void** stack, int64_t stack_size)
{
    //@TODO: make exception safe!
    char **function_names = backtrace_symbols((void *const *) stack, stack_size);
    if (function_names != NULL)
    {
        for (int64_t i = 0; i < stack_size; i++)
        {
            Platform_Stack_Trace_Entry* entry = &tanslated[i];
            char* func_name = function_names[i];
            int64_t func_name_size = strlen(func_name);
            if(func_name_size > sizeof(entry->function) - 1)
                func_name_size = sizeof(entry->function) - 1;

            memset(entry, 0, sizeof *entry); 
            memcpy(entry->function, func_name, func_name_size);

            assert(entry->function[sizeof(entry->function) - 1] == 0 && "must be null terminated!");
        }
    }

    free (function_names);
}