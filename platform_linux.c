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
} Platform_State;

Platform_State gp_state = {0};

static int64_t untested_stack_overflow_calendar_to_time_t(int64_t sec, int64_t min, int64_t hour, int64_t day, int64_t month, int64_t year);
static int64_t platform_startup_local_epoch_time();

void platform_init()
{
    platform_perf_counter_startup();
    platform_local_epoch_time();
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
#define _MINUTE_SECS            ((int64_t) 60)

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


Platform_Calendar_Time platform_calendar_time_from_tm(const struct tm* converted)
{
    assert(converted && "locatime must never fail on this architecture!");
    Platform_Calendar_Time out = {0};
    out.year = converted->tm_year + 1900;
    out.month = converted->tm_mon;
    out.day_of_week = converted->tm_wday;
    out.day = converted->tm_mday - 1;
    out.hour = converted->tm_hour;
    out.minute = converted->tm_min;
    out.second = converted->tm_sec;

    return out;
}

int64_t platform_local_epoch_time()
{
    int64_t now = platform_epoch_time();

    //We abuse localtime to give us the time zone dependendedn conversion and convert its
    //output back to microseconds. We refresh this every cache_invalidate_every to prevent
    //drifting for long programs that would just happen to cross daylight saving time boundary but still
    //remain very fast on rapid calls. (this function is used for logging and such)

    //Having it invalidate every second should only produce minimal mistakes since the internal
    //localtime is only second accuracy. Because of this we dont even make them a members of 
    //Platform_State and let this function be "pure" (from the point of view of platform_init())
    const  int64_t cache_invalidate_every = _SECOND_MICROSECS;
    static int64_t cached_epoch_time_local = 0;
    static int64_t cached_epoch_time = 0;

    if(now - cached_epoch_time >= cache_invalidate_every)
    {
        time_t now_sec = (time_t) (now / _SECOND_MICROSECS);
        Platform_Calendar_Time calendar = platform_calendar_time_from_tm(localtime(&now_sec));

        int64_t now_local = platform_calendar_time_to_epoch_time(calendar);
        now_local += now % _SECOND_MICROSECS; 
        
        cached_epoch_time = now;
        cached_epoch_time_local = now_local;
    }
    
    int64_t offset_micro = now - cached_epoch_time;
    int64_t local_epoch_time = cached_epoch_time_local + offset_micro;

    return local_epoch_time;
} 

Platform_Calendar_Time platform_epoch_time_to_calendar_time(int64_t epoch_time_usec)
{
    time_t epoch_seconds = (time_t) (epoch_time_usec / _SECOND_MICROSECS);
    Platform_Calendar_Time calendar = platform_calendar_time_from_tm(gmtime(&epoch_seconds));
    calendar.microsecond = epoch_time_usec % _SECOND_MICROSECS;
    calendar.millisecond = epoch_time_usec % _SECOND_MILLISECONDS;
    return calendar;
}

//Converts calendar time to the precise epoch time (micro second time since unix epoch)
int64_t platform_calendar_time_to_epoch_time(Platform_Calendar_Time calendar_time)
{
    Platform_Calendar_Time c = calendar_time;
    int64_t seconds = untested_stack_overflow_calendar_to_time_t(
        c.second, c.minute, c.hour, c.day + 1, c.month + 1, c.year);

    int64_t microseconds = seconds * _SECOND_MICROSECS;
    microseconds += calendar_time.microsecond;

    return microseconds;
}

//see: https://stackoverflow.com/a/57744744
//Note that both day and month is one based
static int64_t untested_stack_overflow_calendar_to_time_t(int64_t sec, int64_t min, int64_t hour, int64_t day, int64_t month, int64_t year)
{
  assert(day >= 1);
  assert(month >= 1);

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
          + mdays[month - 1]
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
    //@TODO: addr2line
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

//=========================================
// Filesystem
//=========================================

//@TODO: free allocated!
#define PLATFORM_PRINT_OUT_OF_MEMORY(ammount) \
    fprintf(stderr, "platform allocation failed while calling function %s! not enough memory! requested: %lli", (__FUNCTION__), (lli) (ammount))

const char* platform_null_terminate(Platform_String string)
{
    if(string.data == NULL || string.size <= 0)
        return "";

    //We use a trick to not pointlessly copy and null terminate strings that are null terminated.
    //For this of course we need to check if they contain null one-past-the-end-of-the-buffer.
    //This is illegal by the standard but we can exploit the fact that errors (or any other type
    //of side effect from reading memory location) can only occur on individual pages.
    //
    //This means if the null termination is on the same page as any part of the string we are free
    //to check it. This makes it so we only have probability of 1/PAGE_SIZE that we will needlessly
    //copy and null terminate string that was already null terminated

    enum {
        PAGE_BOUNDARY = 1024,         //Assume very small for safety
        DO_CONDTIONAL_NULL_TERMINATION = true, //set to true for maximum compatibility 
        MAX_COPIED_SIMULATENOUS = 4, //The number of copied strings that are able to coexist in the system. 
        MAX_COPIED_SIZE = 1024,
        MIN_COPIED_SIZE = 64,
    };

    if(DO_CONDTIONAL_NULL_TERMINATION)
    {
        const char* potential_null_termination = string.data + string.size;
        bool is_null_termianted = false;

        //if the potential_null_termination is on the same page as the rest of the string...
        if((int64_t) potential_null_termination % PAGE_BOUNDARY != 0)
        {
            //Do illegal read past the end of the buffer to check if it is null terminated
            is_null_termianted = *potential_null_termination == '\0';
        }

        if(is_null_termianted)
            return string.data;
    }

    static char* strings[MAX_COPIED_SIMULATENOUS] = {0};
    static int64_t string_sizes[MAX_COPIED_SIMULATENOUS] = {0};
    static int64_t string_slot = 0;

    const char* out_string = "";
    char** curr_data = &strings[string_slot];
    int64_t* curr_size = &string_sizes[string_slot];
    string_slot = (string_slot + 1) % MAX_COPIED_SIMULATENOUS;

    bool had_error = false;
    //If we need a bigger buffer OR the previous allocation was too big and the new one isnt
    if(*curr_size <= string.size || (*curr_size > MAX_COPIED_SIZE && string.size <= MAX_COPIED_SIZE))
    {
        int64_t alloc_size = string.size + 1;
        if(alloc_size < MIN_COPIED_SIZE)
            alloc_size = MIN_COPIED_SIZE;

        void* new_data = realloc(*curr_data, alloc_size);
        if(new_data == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(alloc_size);
            had_error = true;
            *curr_size = 0;
            *curr_data = NULL;
        }
        else
        {
            *curr_size = alloc_size;
            *curr_data = new_data;
        }
    }

    if(had_error == false)
    {
        memmove(*curr_data, string.data, string.size);
        (*curr_data)[string.size] = '\0';
        out_string = *curr_data;
    }

    return out_string;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
Platform_Error platform_error_code(bool state)
{
    if(state)
        return PLATFORM_ERROR_OK;
    else
        return (Platform_Error) (errno);
}

int64_t platform_epoch_time_from_time_t(time_t time)
{
    return (int64_t) time * _SECOND_MICROSECS;
}


Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null)
{
    struct stat buf = {0};
    bool state = stat(platform_null_terminate(file_path), &buf) == 0;
    if(state && info_or_null != NULL)
    {
        memset(info_or_null, 0, sizeof *info_or_null);
        info_or_null->size = buf.st_size;
        info_or_null->created_epoch_time = platform_epoch_time_from_time_t(buf.st_ctime);
        info_or_null->last_write_epoch_time = platform_epoch_time_from_time_t(buf.st_mtime);
        info_or_null->last_access_epoch_time = platform_epoch_time_from_time_t(buf.st_atime);

        if(S_ISREG(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_FILE;
        else if(S_ISDIR(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_DIRECTORY;
        else if(S_ISCHR(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_CHARACTER_DEVICE;
        else if(S_ISFIFO(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_PIPE;
        else if(S_ISSOCK(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_SOCKET;
        else
            info_or_null->type = PLATFORM_FILE_TYPE_OTHER;

        if(S_ISLNK(buf.st_mode))
            info_or_null->link_type = PLATFORM_LINK_TYPE_SYM;
        else
            info_or_null->link_type = PLATFORM_LINK_TYPE_NOT_LINK;
    }

    return platform_error_code(state);
}

Platform_Error platform_file_create(Platform_String file_path, bool* was_just_created)
{   
    int fd = open(platform_null_terminate(file_path), O_RDWR | O_CREAT | O_EXCL , S_IRUSR | S_IRGRP | S_IROTH);
    bool was_just_created_ = true;
    bool state = fd != -1;

    if(state == false)
    {
        //if the failiure was because the file already exists it was no failiure at all!
        //Only it must not have been created by this call...
        if(errno == EEXIST)
        {
            state = true;
            was_just_created_ = false;
        }
    }
    else
        close(fd);


    if(was_just_created)
        *was_just_created = was_just_created_ && state;

    return platform_error_code(state);
}

Platform_Error platform_file_remove(Platform_String file_path, bool* was_just_deleted)
{
    bool state = unlink(platform_null_terminate(file_path)) == 0;
    bool was_just_deleted_ = true;
    if(state == false)
    {
        //if the failiure was because the file doesnt exist its sucess
        //Only it must not have been deleted by this call...
        if(errno == ENOENT)
        {
            state = true;
            was_just_deleted_ = false;
        }
    }

    if(was_just_deleted)
        *was_just_deleted = was_just_deleted_ && state;

    return platform_error_code(state);
}

//@TODO:
//Moves or renames a file. If the file cannot be found or renamed to file that already exists, fails.
Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path);
//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path);
//Resizes a file. The file must exist.
Platform_Error platform_file_resize(Platform_String file_path, int64_t size);

Platform_Error platform_directory_create(Platform_String dir_path)
{
    bool state = mkdir(platform_null_terminate(dir_path), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
    return platform_error_code(state);
}
Platform_Error platform_directory_remove(Platform_String dir_path)
{
    bool state = rmdir(platform_null_terminate(dir_path)) == 0;
    return platform_error_code(state);
}

Platform_Error platform_directory_set_current_working(Platform_String new_working_dir)
{
    bool state = chdir(platform_null_terminate(new_working_dir)) == 0;
    return platform_error_code(state);
}
const char* platform_directory_get_current_working()
{
    static char* dir_path = NULL;
    static int64_t dir_path_size = 0;

    for(int i = 0; i < 24; i++)
    {
        printf("[%lli]: dir_path_size: %lli\n", (lli)i, (lli)dir_path_size);
        if(dir_path_size > 0 && dir_path != NULL)
        {
            //if success
            if(getcwd(dir_path, dir_path_size) != NULL)
                break;
            //if error was caused by something else then small buffer
            //returne error
            if(errno != ERANGE)
                dir_path[0] = '\0';
            //Else continue reallocating
            else
            {}
        }

        dir_path_size *= 2;
        if(dir_path_size < 256)
            dir_path_size = 256;
            
        void* realloced_to = realloc(dir_path, dir_path_size);
        if(realloced_to == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(dir_path_size);
            dir_path_size = 0;
            break;
        }

        dir_path = realloced_to;
    }

    printf("dir_path: %s\n", dir_path);

    if(dir_path)
        return dir_path;
    else    
        return "";
}    

//@TODO
//Retrieves the absolute path of the executable / dll
const char* platform_get_executable_path()
{
    // assert(false && "TODO");
    return "";
    // return platform_directory_get_current_working();
}

//Memory maps the file pointed to by file_path and saves the adress and size of the mapped block into mapping. 
//If the desired_size_or_zero == 0 maps the entire file. 
//  if the file doesnt exist the function fails.
//If the desired_size_or_zero > 0 maps only up to desired_size_or_zero bytes from the file.
//  The file is resized so that it is exactly desired_size_or_zero bytes (filling empty space with 0)
//  if the file doesnt exist the function creates a new file.
//If the desired_size_or_zero < 0 maps additional desired_size_or_zero bytes from the file 
//    (for appending) extending it by that ammount and filling the space with 0.
//  if the file doesnt exist the function creates a new file.
Platform_Error platform_file_memory_map(Platform_String file_path, int64_t desired_size_or_zero, Platform_Memory_Mapping* mapping);
//Unmpas the previously mapped file. If mapping is a result of failed platform_file_memory_map does nothing.
void platform_file_memory_unmap(Platform_Memory_Mapping* mapping);

const char* platform_translate_error(Platform_Error error)
{
    return strerror((int) error);
}

#include <malloc.h>
int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align)
{
    (void) align;
    return malloc_usable_size((void*) old_ptr);
}

void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
{
    //Note this can maybe be 16 
    if(align <= 8)
    {
        if(new_size <= 0)
        {
            free(old_ptr);
            return NULL;
        }

        return realloc(old_ptr, new_size);
    }
    else
    {
        (void) old_size; //@TODO: remove as an argument!
        void* out = NULL;
        if(new_size > 0)
        {
            if(posix_memalign(&out, align, new_size) != 0)
                out = NULL;

            if(out != NULL && old_ptr != NULL)
            {
                int64_t min_size = malloc_usable_size(old_ptr);
                if(min_size > new_size)
                    min_size = new_size;

                memcpy(out, old_ptr, min_size);
            }
        }

        if(old_ptr)
            free(old_ptr);

        return out;
    }
}


Platform_Sandox_Error platform_exception_sandbox(
    void (*sandboxed_func)(void* context),   
    void* sandbox_context,
    void (*error_func)(void* context, Platform_Sandox_Error error_code),   
    void* error_context
)
{
    (void) error_func;
    (void) error_context;
    sandboxed_func(sandbox_context);
    return PLATFORM_EXCEPTION_NONE;
}

const char* platform_sandbox_error_to_string(Platform_Sandox_Error error)
{
    switch(error)
    {
        case PLATFORM_EXCEPTION_NONE: return "PLATFORM_EXCEPTION_NONE";
        case PLATFORM_EXCEPTION_ACCESS_VIOLATION: return "PLATFORM_EXCEPTION_ACCESS_VIOLATION";
        case PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT: return "PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT";
        case PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND: return "PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND";
        case PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO: return "PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO";
        case PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT: return "PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT";
        case PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION: return "PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION";
        case PLATFORM_EXCEPTION_FLOAT_OVERFLOW: return "PLATFORM_EXCEPTION_FLOAT_OVERFLOW";
        case PLATFORM_EXCEPTION_FLOAT_UNDERFLOW: return "PLATFORM_EXCEPTION_FLOAT_UNDERFLOW";
        case PLATFORM_EXCEPTION_FLOAT_OTHER: return "PLATFORM_EXCEPTION_FLOAT_OTHER";
        case PLATFORM_EXCEPTION_PAGE_ERROR: return "PLATFORM_EXCEPTION_PAGE_ERROR";
        case PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO: return "PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO";
        case PLATFORM_EXCEPTION_INT_OVERFLOW: return "PLATFORM_EXCEPTION_INT_OVERFLOW";
        case PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION: return "PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION";
        case PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION: return "PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION";
        case PLATFORM_EXCEPTION_BREAKPOINT: return "PLATFORM_EXCEPTION_BREAKPOINT";
        case PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP: return "PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP";
        case PLATFORM_EXCEPTION_STACK_OVERFLOW: return "PLATFORM_EXCEPTION_STACK_OVERFLOW";
        case PLATFORM_EXCEPTION_ABORT: return "PLATFORM_EXCEPTION_ABORT";
        case PLATFORM_EXCEPTION_TERMINATE: return "PLATFORM_EXCEPTION_TERMINATE";
        default:
        case PLATFORM_EXCEPTION_OTHER: return "PLATFORM_EXCEPTION_OTHER";
    }
}