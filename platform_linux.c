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

        void* new_data = realloc(*curr_data, (size_t) alloc_size);
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
        memmove(*curr_data, string.data, (size_t) string.size);
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
            if(getcwd(dir_path, (size_t) dir_path_size) != NULL)
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
            
        void* realloced_to = realloc(dir_path, (size_t) dir_path_size);
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

const char* platform_get_executable_path()
{
    const int64_t path_size = PATH_MAX*4;
    static char* exe_path = NULL;

    if(exe_path == NULL)
    {
        exe_path = (char*) malloc((size_t) path_size);
        if(exe_path == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(path_size);
            assert(false && "allocation of executable failed!");
        }
        else
        {
            ssize_t count = readlink("/proc/self/exe", exe_path, (size_t) path_size);
            if(count < 0)
                count = 0;
            if(count >= path_size)
                count = path_size - 1;

            exe_path[count] = '\0';
        }
    }

    return exe_path ? exe_path : "";
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
    return (int64_t) malloc_usable_size((void*) old_ptr);
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

        return realloc(old_ptr, (size_t) new_size);
    }
    else
    {
        (void) old_size; //@TODO: remove as an argument!
        void* out = NULL;
        if(new_size > 0)
        {
            if(posix_memalign(&out, (size_t) align, (size_t) new_size) != 0)
                out = NULL;

            if(out != NULL && old_ptr != NULL)
            {
                int64_t min_size = (int64_t) malloc_usable_size(old_ptr);
                if(min_size > new_size)
                    min_size = new_size;

                memcpy(out, old_ptr, (size_t) min_size);
            }
        }

        if(old_ptr)
            free(old_ptr);

        return out;
    }
}


//=========================================
// Debug
//=========================================

#define PLATFORM_CALLSTACKS_MAX 256
#define PLATFORM_CALLSTACK_LINE_LEN 64

#define _GNU_SOURCE
#define __USE_GNU
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>

int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count)
{
    static void* stack_ptrs[PLATFORM_CALLSTACKS_MAX] = {0};
    int64_t found_size = backtrace(stack_ptrs, (int) PLATFORM_CALLSTACKS_MAX);
    if(skip_count < 0)
        skip_count = 0;
        
    skip_count += 1; //for this function
    
    int64_t not_skipped_size = found_size - skip_count;
    if(not_skipped_size < 0)
        not_skipped_size = 0;

    memcpy(stack, stack_ptrs + skip_count, (size_t) not_skipped_size*sizeof(void*));
    return not_skipped_size;
}

int64_t platform_shell_output(const char* command, char* buffer, int64_t buffer_size)
{
    int64_t read = 0;
    FILE* pipe = popen(command, "r");
    if(pipe)
    {
        read = (int64_t) fread(buffer, 1, (size_t) buffer_size, pipe);
        pclose(pipe);
    }

    return read;
}

#define _MIN(a, b)   ((a) < (b) ? (a) : (b))
#define _MAX(a, b)   ((a) > (b) ? (a) : (b))
#define _CLAMP(value, low, high) _MAX((low), _MIN((value), (high)))

//Translates call stack. 
void _platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, const void** stack, int64_t stack_size, int64_t crash_depth)
{
    assert(stack_size >= 0);

    memset(translated, 0, (size_t) stack_size * sizeof *translated);

    char addr2line_buffer[1024] = {0};
    char command[1024] = {0};

    //@NOTE: backtrace_symbols only gives good answers sometimes and more often than not its
    //missing all the relevant info. (File, function, line).
    //Thus we use it as only a starting off point and call addr2line to get rest of the information.
    //If that fails we use just the backtrace_symbols output @TODO
    //We then srtring parse whatever we can.

    //@NOTE: backtrace_symbols is not safe to call from within signal handler but we handle signals
    //by caopying tiny bit of state and siglongjmp'ing back to before the signal handler was encoutnered
    //so we *should* be fine
    char **semi_translated = backtrace_symbols((void *const *) stack, stack_size);
    if (semi_translated != NULL)
    {
        for (int64_t i = 0; i < stack_size; i++)
        {
            Platform_Stack_Trace_Entry* entry = &translated[i];
            void* frame = (void*) stack[i];
            char* message = semi_translated[i];

            Dl_info info = {0};
            struct link_map* link_map = NULL;
            
            //if is dynamically loaded symbol we can use that info.
            bool is_dynamic_loaded = !!dladdr1(frame, &info, (void**) &link_map, RTLD_DL_LINKMAP);
            if(is_dynamic_loaded)
            {
                size_t VMA_addr = (size_t) frame - link_map->l_addr;

                //we need to substract one to get the actual instruction address because the instruction pointer
                //is pointing to the next instruction. The only time when we dont do this is when a crahs occured
                //see: https://stackoverflow.com/questions/11579509/wrong-line-numbers-from-addr2line/63841497#63841497
                if(i!=crash_depth)
                    VMA_addr-=1;    
                snprintf(command, sizeof command - 1, "addr2line -e %s -f -Ci %zx", info.dli_fname,VMA_addr);
            }
            //Else parse the bear minimum
            else
            {
                //find first occurence of '(' or ' ' in message[i] and assume
                //everything before that is the file name. (don't go beyond 0 though
                //(string terminator)
                int p = 0;
                while(message[p] != '(' && message[p] != ' ' && message[p] != 0)
                    p++;

                snprintf(command, sizeof command - 1, "addr2line -e %.*s -f -Ci %zx", p, message, (size_t) frame);
            }

            //Translate the address using address to line
            int64_t addr2line_size = platform_shell_output(command, addr2line_buffer, (int64_t) sizeof addr2line_buffer - 1);
            addr2line_buffer[addr2line_size] = '\0';

            //parse the output. It will look something like the following (my comments (//) not included): 
            //my_func_a             //[function name]
            //path/to/file.c:13     //[path]:[line]

            int64_t function_name_to = addr2line_size;
            for(int64_t i = 0; i < addr2line_size; i++)
            {
                if(addr2line_buffer[i] == '\n')
                {
                    function_name_to = i;
                    break;
                }
            }

            int64_t digit_separator = addr2line_size;
            for(int64_t i = addr2line_size; i-- > 0;)
            {
                if(addr2line_buffer[i] == ':')
                {
                    digit_separator = i;
                    break;
                }
            }

            int64_t file_from = function_name_to + 1;
            int64_t file_size = digit_separator - file_from;

            int64_t line_from = digit_separator + 1;
            int64_t line_size = addr2line_size - line_from;
            
            //Add the module or current executable
            const char* module = "";
            if(is_dynamic_loaded && info.dli_fname != NULL)
                module = info.dli_fname;
            else
                module = platform_get_executable_path();
            
            int64_t module_size = (int64_t) strlen(module);
            int64_t function_size = function_name_to;

            file_size = _CLAMP(file_size, 0, (int64_t) sizeof entry->file - 1);
            line_size = _CLAMP(line_size, 0, (int64_t) sizeof entry->function - 1);
            function_size = _CLAMP(function_size, 0, (int64_t) sizeof entry->function - 1);

            int64_t line = atoi(addr2line_buffer + line_from);

            entry->line = line;
            entry->address = frame;
            memcpy(entry->function, addr2line_buffer, (size_t) function_size);
            memcpy(entry->file, addr2line_buffer + file_from, (size_t) file_size);
            memcpy(entry->module, module, (size_t) module_size);

            //if everything else failed just use the semi translate message...            
            if(strcmp(entry->function, "") == 0 && strcmp(entry->file, "") == 0)
            {
                function_size = (int64_t) strlen(message);
                function_size = _CLAMP(function_size, 0, (int64_t) sizeof entry->function - 1);
                memcpy(entry->function, message, (size_t) function_size);
            }

            //null terminate everything just in case
            entry->module[sizeof entry->module - 1] = '\0';
            entry->file[sizeof entry->file - 1] = '\0';
            entry->function[sizeof entry->function - 1] = '\0';
        }
    }

    free(semi_translated);
}

void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, const void** stack, int64_t stack_size)
{
    _platform_translate_call_stack(translated, stack, stack_size, -1);
}

#include <signal.h>
#include <setjmp.h>

#define PLATFORM_SANDBOXES_MAX 256
#define PLATFORM_SANDBOXE_JUMP_CODE 0x123

//An X macro collection of all signals with some commented out that we are not handling. 
//If you want to enable/disable comment/uncomment additional lines.
//Taken from: https://man7.org/linux/man-pages/man7/signal.7.html
//For info on X macros: https://en.wikipedia.org/wiki/X_macro 
#define SIGNAL_ACTION_X \
    X(SIGABRT, PLATFORM_EXCEPTION_ABORT)                /*P1990      Core    Abort signal from abort(3) */ \
    /*X(SIGALRM, PLATFORM_EXCEPTION_OTHER)                P1990      Term    Timer signal from alarm(2) */ \
    X(SIGBUS, PLATFORM_EXCEPTION_ACCESS_VIOLATION)      /*P2001      Core    Bus error (bad memory access) */ \
    /*X(SIGCHLD, PLATFORM_EXCEPTION_OTHER)                P1990      Ign     Child stopped or terminated  */ \
    /*X(SIGCLD, PLATFORM_EXCEPTION_OTHER)                   -        Ign     A synonym for SIGCHLD  */ \
    /*X(SIGCONT, PLATFORM_EXCEPTION_OTHER)                P1990      Cont    Continue if stopped  */ \
    /*X(SIGEMT, PLATFORM_EXCEPTION_OTHER)                   -        Term    Emulator trap  */ \
    X(SIGFPE, PLATFORM_EXCEPTION_FLOAT_OTHER)           /*P1990      Core    Floating-point exception */ \
    X(SIGHUP, PLATFORM_EXCEPTION_OTHER)                 /*P1990      Term    Hangup detected on controlling terminal or death of controlling process */ \
    X(SIGILL, PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION)   /*P1990      Core    Illegal Instruction */ \
    /*X(SIGINFO, PLATFORM_EXCEPTION_OTHER)                  -                A synonym for SIGPWR */ \
    /*X(SIGINT, PLATFORM_EXCEPTION_OTHER)                 P1990      Term    Interrupt from keyboard */ \
    /*X(SIGIO, PLATFORM_EXCEPTION_OTHER)                    -        Term    I/O now possible (4.2BSD) */ \
    X(SIGIOT, PLATFORM_EXCEPTION_ABORT)                 /*  -        Core    IOT trap. A synonym for SIGABRT */ \
    /*X(SIGKILL, PLATFORM_EXCEPTION_OTHER)                P1990      Term    Kill signal */ \
    /*X(SIGLOST, PLATFORM_EXCEPTION_OTHER)                  -        Term    File lock lost (unused) */ \
    /*X(SIGPIPE, PLATFORM_EXCEPTION_OTHER)                P1990      Term    Broken pipe: write to pipe with no readers; see pipe(7) */ \
    /*X(SIGPOLL, PLATFORM_EXCEPTION_OTHER)                P2001      Term    Pollable event (Sys V); synonym for SIGIO */ \
    /*X(SIGPROF, PLATFORM_EXCEPTION_OTHER)                P2001      Term    Profiling timer expired */ \
    X(SIGPWR, PLATFORM_EXCEPTION_OTHER)                 /*  -        Term    Power failure (System V) */ \
    /*X(SIGQUIT, PLATFORM_EXCEPTION_OTHER)               P1990      Core    Quit from keyboard */ \
    X(SIGSEGV, PLATFORM_EXCEPTION_ACCESS_VIOLATION)     /*P1990      Core    Invalid memory reference */ \
    X(SIGSTKFLT, PLATFORM_EXCEPTION_ACCESS_VIOLATION)   /*  -        Term    Stack fault on coprocessor (unused) */ \
    /*X(SIGSTOP, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Stop process */ \
    /*X(SIGTSTP, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Stop typed at terminal */ \
    X(SIGSYS, PLATFORM_EXCEPTION_OTHER)                 /*P2001      Core    Bad system call (SVr4); see also seccomp(2) */ \
    X(SIGTERM, PLATFORM_EXCEPTION_TERMINATE)            /*P1990      Term    Termination signal */ \
    X(SIGTRAP, PLATFORM_EXCEPTION_BREAKPOINT)           /*P2001      Core    Trace/breakpoint trap */ \
    /*X(SIGTTIN, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Terminal input for background process */ \
    /*X(SIGTTOU, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Terminal output for background process */ \
    /*X(SIGUNUSED, PLATFORM_EXCEPTION_OTHER)                -        Core    Synonymous with SIGSYS */ \
    /*X(SIGURG, PLATFORM_EXCEPTION_OTHER)                 P2001      Ign     Urgent condition on socket (4.2BSD) */ \
    /*X(SIGUSR1, PLATFORM_EXCEPTION_OTHER)                P1990      Term    User-defined signal 1 */ \
    /*X(SIGUSR2, PLATFORM_EXCEPTION_OTHER)                P1990      Term    User-defined signal 2 */ \
    /*X(SIGVTALRM, PLATFORM_EXCEPTION_OTHER)              P2001      Term    Virtual alarm clock (4.2BSD) */ \
    /*X(SIGXCPU, PLATFORM_EXCEPTION_OTHER)                P2001      Core    CPU time limit exceeded (4.2BSD); see setrlimit(2) */ \
    /*X(SIGXFSZ, PLATFORM_EXCEPTION_OTHER)                P2001      Core    File size limit exceeded (4.2BSD); see setrlimit(2) */ \
    /*X(SIGWINCH, PLATFORM_EXCEPTION_OTHER)                 -        Ign     Window resize signal (4.3BSD, Sun) */ \

typedef struct Signal_Handler_State {
    sigjmp_buf jump_buffer;
    int signal;

    int32_t stack_size;
    void* stack[PLATFORM_CALLSTACKS_MAX];

    int64_t perf_counter;
    int64_t epoch_time;
} Signal_Handler_State;

__thread Signal_Handler_State* platform_signal_handler_queue = NULL;
__thread int64_t platform_signal_handler_i1 = 0;

void platform_sighandler(int sig, struct sigcontext ctx) 
{
    (void) ctx;

    int32_t my_index_i1 = platform_signal_handler_i1;
    if(my_index_i1 >= 1)
    {
        //@TODO: add more specific flag testing!
        Signal_Handler_State* handler = &platform_signal_handler_queue[my_index_i1-1];
        handler->perf_counter = platform_perf_counter();
        handler->perf_counter = platform_epoch_time();
        handler->stack_size = platform_capture_call_stack(handler->stack, PLATFORM_CALLSTACKS_MAX, 1);
        handler->signal = sig;
        siglongjmp(handler->jump_buffer, PLATFORM_SANDBOXE_JUMP_CODE);
    }
}

Platform_Exception platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context,
    void (*error_func)(void* error_context, Platform_Sandbox_Error error),
    void* error_context)
{
    typedef struct {
        int signal;
        Platform_Exception platform_error;
        struct sigaction action;
        struct sigaction prev_action;
    } Signal_Error;

    #undef X
    #define X(SIGNAL_NAME, PLATFORM_ERROR) \
         {(SIGNAL_NAME), (PLATFORM_ERROR), {0}, {0}},

    Signal_Error error_handlers[] = {
        SIGNAL_ACTION_X
    };

    const int64_t handler_count = (int64_t) sizeof(error_handlers) / (int64_t) sizeof(Signal_Error);
    for(int64_t i = 0; i < handler_count; i++)
    {
        Signal_Error* sig_error = &error_handlers[i];
        sig_error->action.sa_handler = (void *)platform_sighandler;
        sigemptyset(&sig_error->action.sa_mask);
        // sigaddset(&sig_error->action.sa_mask, (int) SA_RESETHAND);
        sigaddset(&sig_error->action.sa_mask, (int) SA_NOCLDSTOP);

        bool state = sigaction(sig_error->signal, &sig_error->action, &sig_error->prev_action) == 0;
        assert(state && "bad signal specifier!");
    }

    Platform_Exception had_exception = PLATFORM_EXCEPTION_NONE;
    if(platform_signal_handler_queue == NULL)
    {
        size_t needed_size = PLATFORM_SANDBOXES_MAX * sizeof(Signal_Handler_State);
        platform_signal_handler_queue = (Signal_Handler_State*) malloc(needed_size);
        if(platform_signal_handler_queue == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(needed_size);
            assert("out of memory! @TODO: possible exception?");
            had_exception = PLATFORM_EXCEPTION_OTHER;
        }
        else
        {
            memset(platform_signal_handler_queue, 0, needed_size);
        }
    }
    
    if(platform_signal_handler_queue != NULL)
    {
        platform_signal_handler_i1 = _CLAMP(platform_signal_handler_i1 + 1, 1, PLATFORM_SANDBOXES_MAX);
        Signal_Handler_State* handler = &platform_signal_handler_queue[platform_signal_handler_i1 - 1];
        memset(handler, 0, sizeof *handler);

        switch(sigsetjmp(handler->jump_buffer, 0))
        {
            case 0: {
                sandboxed_func(sandbox_context);
                break;
            }
            case PLATFORM_SANDBOXE_JUMP_CODE: {
                had_exception = PLATFORM_EXCEPTION_OTHER;
                
                for(int64_t i = 0; i < handler_count; i++)
                {
                    if(error_handlers[i].signal == handler->signal)
                    {
                        had_exception = error_handlers[i].platform_error;
                        break;
                    }
                }
                
                Platform_Stack_Trace_Entry stack_trace[PLATFORM_CALLSTACKS_MAX] = {0}; 
                platform_translate_call_stack(stack_trace, (const void**) handler->stack, handler->stack_size);

                Platform_Sandbox_Error sanbox_error = {0};
                sanbox_error.exception = had_exception;
                sanbox_error.stack_trace = stack_trace;
                sanbox_error.stack_trace_size = handler->stack_size;
                sanbox_error.epoch_time = handler->epoch_time;
                
                //@TODO
                sanbox_error.nanosec_offset = 0;
                sanbox_error.execution_context = NULL;
                sanbox_error.execution_context_size = 0;

                error_func(error_context, sanbox_error);
                break;
            }
            default: {
                assert(false && "unexpected jump occured!");
                break;
            }
        }

        platform_signal_handler_i1 = _CLAMP(platform_signal_handler_i1 - 1, 0, PLATFORM_SANDBOXES_MAX);

    }
    for(int64_t i = 0; i < handler_count; i++)
    {
        Signal_Error* sig_error = &error_handlers[i];
        bool state = sigaction(sig_error->signal, &sig_error->prev_action, NULL) == 0;
        assert(state && "bad signal specifier");
    }

    return had_exception;
}

const char* platform_exception_to_string(Platform_Exception error)
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