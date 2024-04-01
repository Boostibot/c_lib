#ifndef JOT_LOG
#define JOT_LOG

// This file is focused on as-simpel-as-possible structured logging.
// That is we attempt to give logs some structure but not too much (so that it is still convinient).
// 
// We use three primary pieces of information for our logs:
//  1) Log log_module - a simple string indicating from where the log came from. 
//                  The user is free to give this location any meaning (function, file, etc.)
//                  to group things however they please. 
// 
//  2) Log log_type - a number indicating what kind of log this is (info, warn, error, etc.).
//                These numbers can range from 0-63 (some slots are already taken). This
//                enables us to store the filter of allowed types as single 64 bit number
//                which can in turn be used to silence certain logs really easily.
// 
//  3) Indentation - a visual indicator to the hierarchy of messages. 
//                   This can be used to separate function calls. The simple global
//                   implementation also nests (as long as we remeber to call pop 
//                   when we are done). 
// 
// Log log_module in combination with log log_type enables us to extremely easily locate desired properties
// in the resulting log files. We grep for example for just LOG_ERROR from RENDER log_module. we can then 
// reconstruct the call stack from indetation.
// 
// The choice to have Log_Filter which qualifies some basic log categories instead of the usual 
//  severity level approach was chosen because of the following: 
// Severity level has the two primary problems:
//  1) Lack of meaning:
//     The choice between severity level 5 and 6 is largely abitrary. This is because the meaning of 
//     severity is distacted by how rest of the codebase uses it.
// 
//  2) Lack of fine grained control:
//     If we want to dissable all info messages but keep debug emssaegs we simply cannot 
//     (assuming severity of debug is smaller then of info - which is usually the case)


#ifdef _LOG_EXAMPLE

void log_example_nested();

void log_example()
{
    //We have chose to call this 'log_module' EXAMPLES
    //Usually we choose modules based on logical units
    // instead of code units. So for example RENDER, IO, INPUT
    // instead of log_example or My_Class
    LOG_INFO("EXAMPLES", "Starting examples!");
    
    LOG_DEBUG("EXAMPLES", "current time %i", clock());

    //Increases indentation for all subsequent calls untill log_group_pop is called
    //This can be used for example to distinguish nested functions such as now
    log_group(); 


    //Also dissable debug prints
    Log_Filter prev = log_set_filter(log_get_filter() & ~LOG_DEBUG);

        log_example_nested();

    //Restore previous state
    log_set_filter(prev);
    log_ungroup();
    LOG_INFO("EXAMPLES", "example finished");
}

void log_example_nested()
{
    LOG_INFO("EXAMPLES", "Inside nested func");
    LOG_DEBUG("EXAMPLES", "this print will not show because we dissabled debug");

    //All logs whose modules start by ">" are indented +1 more than usual.
    //This is extremely convenient for small groups.
    if(clock() == 0)
        LOG_OKAY(">EXAMPLES", "OK");
    else
    {
        LOG_ERROR(">EXAMPLES", "ERROR OCCURED!");
        // ">" can be used multiple times and even works for custom fucntions
        // (because they will eventually call the standard vlog_message)
        log_callstack(">>EXAMPLES", LOG_TRACE, 0, "printing callstack below:");
    }
}

#endif

#include "defines.h"
#include "platform.h"
#include <string.h>
#include <stdarg.h>

typedef u64 Log_Filter;
typedef enum Log_Type{
    LOG_INFO  = 0, //Used to log general info.
    LOG_OKAY  = 1, //Used to log the opposites of errors
    LOG_WARN  = 2, //Used to log near error conditions
    LOG_ERROR = 3, //Used to log errors
    LOG_FATAL = 4, //Used to log errors just before giving up some important action
    LOG_DEBUG = 5, //Used to log for debug purposes. Is only logged in debug builds
    LOG_TRACE = 6, //Used to log for step debug purposes (prinf("HERE") and such). Is only logged in step debug builds

    //Custom can be defined...
    LOG_TYPE_MAX = 63,
} Log_Type;

//Communicates desired action to logger
typedef enum Log_Action {
    LOG_ACTION_LOG   = 1, //Makes a log out of the arguments
    LOG_ACTION_CHILD = 2, //Logs the provided child 
    LOG_ACTION_FLUSH = 4, //Flushes the log
    //Custom can be defined...
} Log_Action;

typedef struct Logger Logger;
typedef struct Log Log;
typedef void (*Log_Func)(Logger* logger, i32 group_depth, int actions, const char* log_module, const char* subject, Log_Type log_type, Source_Info source, const Log* child, const char* format, va_list args);

typedef struct Logger {
    Log_Func log;
} Logger;

typedef struct Log {
    //@NOTE: this struct is rather big 104B at the moment.
    //       This might seem scary but its not that much.
    //       In 10MB we are able to store 100,824 logs.

    //@NOTE: Notice that log_module and subject are const char*
    //       and thus as static strings. This is to make 
    //       our lives easier because it almost always end
    //       yp being static stirngs
    const char* module;
    const char* subject;
    Platform_String message;
    
    Log_Type type;
    int _padding;
    i64 time;
    Source_Info source;

    struct Log* prev;
    struct Log* next;
    struct Log* first_child;
    struct Log* last_child;
} Log;

EXPORT Logger* log_get_logger(); //Returns the default used logger
EXPORT Logger* log_set_logger(Logger* logger); //Sets the default used logger. Returns a pointer to the previous logger so it can be restored later.

EXPORT Log_Filter log_get_filter(); //Returns the current global filter - For Log_Type log_type to be printed it must satissfy (filter & ((Log_Filter) 1 << log_type)) > 0
EXPORT Log_Filter log_set_filter(Log_Filter filter); //Sets the global filter. Returns previous value so it can be restored later.

EXPORT void log_flush();    //Flushes the logger
EXPORT void log_group();    //Increases group depth (indentation) of subsequent log messages
EXPORT void log_ungroup();  //Decreases group depth (indentation) of subsequent log messages
EXPORT i32* log_group_depth(); //Returns the current group depth
EXPORT void log_captured(const Log* log_list);

EXPORT const char* log_type_to_string(Log_Type log_type);


EXPORT void log_message_no_check(const char* log_module, const char* subject, Log_Type log_type, Source_Info source, const Log* child, const char* format, ...);
EXPORT void vlog_message(const char* log_module, const char* subject, Log_Type log_type, Source_Info source, const Log* child, const char* format, va_list args);

EXPORT void log_callstack_no_check(const char* log_module, Log_Type log_type, isize skip, const char* format, ...);
EXPORT void log_captured_callstack(const char* log_module, Log_Type log_type, const void* const* callstack, isize callstack_size);

//A cute hack to typecheck printf arguments better and more portable than attribute annotations.
//Simply use printf(format, args...) as well but dont actually evaluate it (using sizeof)
#include <stdio.h>
#define log_message(log_module, subject, log_type, source, child, format, ...) (sizeof printf((format), ##__VA_ARGS__), log_message_no_check((log_module), (subject), (log_type), (source), (child), (format), ##__VA_ARGS__))
#define log_callstack(log_module, log_type, skip, format, ...)                 (sizeof printf((format), ##__VA_ARGS__), log_callstack_no_check((log_module), (log_type), (skip), (format), ##__VA_ARGS__))

typedef struct Memory_Format {
    const char* unit;
    isize unit_value;
    f64 fraction;

    i32 whole;
    i32 remainder;
} Memory_Format;

EXPORT Memory_Format get_memory_format(isize bytes);
EXPORT Allocator_Stats log_allocator_stats(const char* log_module, Log_Type log_type, Allocator* allocator);

//Logs a message. Does not get dissabled.
#define LOG(log_module, log_type, format, ...)                         log_message(log_module, "", log_type, SOURCE_INFO(), NULL, format, ##__VA_ARGS__)
#define LOG_CHILD(log_module, subject, log_type, child, format, ...)   log_message(log_module, subject, log_type, SOURCE_INFO(), child, format, ##__VA_ARGS__)

//Logs a message log_type into the provided log_module cstring.
#define LOG_INFO(log_module, format, ...)  LOG(log_module, LOG_INFO,  format, ##__VA_ARGS__)
#define LOG_OKAY(log_module, format, ...)  LOG(log_module, LOG_OKAY,  format, ##__VA_ARGS__)
#define LOG_WARN(log_module, format, ...)  LOG(log_module, LOG_WARN,  format, ##__VA_ARGS__)
#define LOG_ERROR(log_module, format, ...) LOG(log_module, LOG_ERROR, format, ##__VA_ARGS__)
#define LOG_FATAL(log_module, format, ...) LOG(log_module, LOG_FATAL, format, ##__VA_ARGS__)
#define LOG_DEBUG(log_module, format, ...) LOG(log_module, LOG_DEBUG, format, ##__VA_ARGS__)
#define LOG_TRACE(log_module, format, ...) LOG(log_module, LOG_TRACE, format, ##__VA_ARGS__)

#define LOG_ERROR_CHILD(log_module, subject, child, format, ...)        LOG_CHILD(log_module, subject, LOG_ERROR, child, format, ##__VA_ARGS__)
#define LOG_FATAL_CHILD(log_module, subject, child, format, ...)        LOG_CHILD(log_module, subject, LOG_FATAL, child, format, ##__VA_ARGS__)

#define STRING_FMT "%.*s"
#define STRING_PRINT(string) (int) (string).size, (string).data

#define MEMORY_FMT "%.2lf%s"
#define MEMORY_PRINT(bytes) get_memory_format((bytes)).fraction, get_memory_format((bytes)).unit //@NOTE We call the fucntion twice. Its not optimal however I dont think its going to be used in perf critical situations

#define PTR_FMT "0x%08llx"
#define PTR_PRINT(ptr) (lli) ptr

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOG_IMPL)) && !defined(JOT_LOG_HAS_IMPL)
#define JOT_LOG_HAS_IMPL

typedef struct Global_Log_State {
    Log_Filter filter;
    Logger* logger;
    i32 group_depth;
    i32 _padding;
} Global_Log_State;

static ATTRIBUTE_THREAD_LOCAL Global_Log_State _global_log_state = {~(Log_Filter) 0}; //All channels on!

EXPORT Logger* log_get_logger()
{
    return _global_log_state.logger;
}

EXPORT Logger* log_set_logger(Logger* logger)
{
    Logger* before = _global_log_state.logger;
    _global_log_state.logger = logger;
    return before;
}

EXPORT void log_captured(const Log* log_list)
{
    Global_Log_State* state = &_global_log_state;
    if(state->logger && (state->filter & ((Log_Filter) 1 << log_list->type)))
    {
        va_list args = {0};
        Logger* logger = state->logger;
        state->logger = NULL;
        logger->log(logger, state->group_depth, LOG_ACTION_CHILD, "", "", (Log_Type) 0, SOURCE_INFO(), log_list, "", args);
        state->logger = logger;
    }
}
EXPORT void log_flush()
{   
    Global_Log_State* state = &_global_log_state;
    if(state->logger)
    {
        va_list args = {0};
        state->logger->log(state->logger, 0, LOG_ACTION_FLUSH, "", "", (Log_Type) 0, SOURCE_INFO(), NULL, "", args);
    }
}
EXPORT void log_group()
{
    _global_log_state.group_depth += 1;
}
EXPORT void log_ungroup()
{
    ASSERT(_global_log_state.group_depth > 0);
    _global_log_state.group_depth = MAX(_global_log_state.group_depth - 1, 0);
}
EXPORT i32* log_group_depth()
{
    return &_global_log_state.group_depth;
}

EXPORT Log_Filter log_get_filter()
{
    return _global_log_state.filter;
}

EXPORT Log_Filter log_set_filter(Log_Filter filter)
{
    Log_Filter* gloabl_filter = &_global_log_state.filter;
    Log_Filter prev = *gloabl_filter; 
    *gloabl_filter = filter;
    return prev;
}

EXPORT void log_message_no_check(const char* log_module, const char* subject, Log_Type log_type, Source_Info source, const Log* child, const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    vlog_message(log_module, subject, log_type, source, child, format, args);                    
    va_end(args);            
}

EXPORT void vlog_message(const char* log_module, const char* subject, Log_Type log_type, Source_Info source, const Log* first_child, const char* format, va_list args)
{   
    Global_Log_State* state = &_global_log_state;
    if(state->logger && (state->filter & ((Log_Filter) 1 << log_type)))
    {
        i32 extra_indentation = 0;
        for(; log_module[extra_indentation] == '>'; extra_indentation++);
        
        int action = LOG_ACTION_LOG;
        if(first_child)
            action |= LOG_ACTION_CHILD;

        //We temporarily dissable loggers while we are logging. This prevents log infinite recursion which occurs for example
        // when the logger fails to acquire a resource (memory) and that failiure logs 
        Logger* logger = state->logger;
        state->logger = NULL;
        logger->log(logger, state->group_depth + extra_indentation, action, log_module + extra_indentation, subject, log_type, source, first_child, format, args);
        state->logger = logger;
    }
}

EXPORT const char* log_type_to_string(Log_Type log_type)
{
    switch(log_type)
    {
        case LOG_INFO: return "INFO"; break;
        case LOG_OKAY: return "SUCC"; break;
        case LOG_WARN: return "WARN"; break;
        case LOG_ERROR: return "ERROR"; break;
        case LOG_FATAL: return "FATAL"; break;
        case LOG_DEBUG: return "DEBUG"; break;
        case LOG_TRACE: return "TRACE"; break;
        case LOG_TYPE_MAX:
        default: return "";
    }
}

EXPORT void log_callstack_no_check(const char* log_module, Log_Type log_type, isize skip, const char* format, ...)
{
    bool has_msg = format != NULL && strlen(format) != 0;
    if(has_msg)
    {
        va_list args;               
        va_start(args, format);     
        vlog_message(log_module, "", log_type, SOURCE_INFO(), NULL, format, args);                    
        va_end(args);   

        log_group();
    }
    
    void* stack[256] = {0};
    isize size = platform_capture_call_stack(stack, 256, skip + 1);
    log_captured_callstack(log_module, log_type, (const void**) stack, size);

    if(has_msg)
        log_ungroup();
}

EXPORT void log_captured_callstack(const char* log_module, Log_Type log_type, const void* const* callstack, isize callstack_size)
{
    if(callstack_size < 0 || callstack == NULL)
        callstack_size = 0;
    
    enum {TRANSLATE_AT_ONCE = 8};
    for(isize i = 0; i < callstack_size; i += TRANSLATE_AT_ONCE)
    {
        isize remaining = callstack_size - i;
        assert(remaining > 0);

        if(remaining > TRANSLATE_AT_ONCE)
            remaining = TRANSLATE_AT_ONCE;

        Platform_Stack_Trace_Entry translated[TRANSLATE_AT_ONCE] = {0};
        platform_translate_call_stack(translated, callstack + i, remaining);
        
        for(isize j = 0; j < remaining; j++)
        {
            const Platform_Stack_Trace_Entry* entry = &translated[j];
            log_message(log_module, "", log_type, SOURCE_INFO(), NULL, "%-30s %s : %i", entry->function , entry->file, (int) entry->line);
            if(strcmp(entry->function, "main") == 0) //if reaches main stops (we dont care about OS stuff)
            {
                i = callstack_size;
                break;
            }
        }
    }
}

#ifndef ASSERT_CUSTOM_REPORT
    EXPORT void assertion_report(const char* expression, int line, const char* file, const char* function, const char* format, ...)
    {
        Source_Info source = {line, file, function};
        log_message("assert", "", LOG_FATAL, source, NULL, "TEST(%s) TEST/ASSERT failed! %s:%i", expression, file, line);
        if(format != NULL && strlen(format) != 0)
        {
            va_list args;               
            va_start(args, format);     
            vlog_message(">assert", "", LOG_FATAL, source, NULL, format + 1, args); //+1 to get around the annoying zero length printf warning in gcc
            va_end(args);  
        }

        log_callstack(">assert", LOG_TRACE, -1, "callstack:");
    }
#endif
    
EXPORT Allocator_Stats log_allocator_stats(const char* log_module, Log_Type log_type, Allocator* allocator)
{
    Allocator_Stats stats = {0};
    if(allocator != NULL && allocator->get_stats != NULL)
    {
        stats = allocator_get_stats(allocator);
        if(stats.type_name == NULL)
            stats.type_name = "<no log_type name>";

        if(stats.name == NULL)
            stats.name = "<no name>";

        LOG(log_module, log_type, "type_name:           %s", stats.type_name);
        LOG(log_module, log_type, "name:                %s", stats.name);

        LOG(log_module, log_type, "bytes_allocated:     " MEMORY_FMT, MEMORY_PRINT(stats.bytes_allocated));
        LOG(log_module, log_type, "max_bytes_allocated: " MEMORY_FMT, MEMORY_PRINT(stats.max_bytes_allocated));

        LOG(log_module, log_type, "allocation_count:    %lli", stats.allocation_count);
        LOG(log_module, log_type, "deallocation_count:  %lli", stats.deallocation_count);
        LOG(log_module, log_type, "reallocation_count:  %lli", stats.reallocation_count);
    }
    else
        LOG(log_module, log_type, "Allocator NULL or missing get_stats callback.");

    return stats;
}

#ifndef ALLOCATOR_CUSTOM_OUT_OF_MEMORY
    EXPORT void allocator_out_of_memory(Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        Allocator_Stats stats = {0};
        if(allocator != NULL && allocator->get_stats != NULL)
            stats = allocator_get_stats(allocator);
        
        if(stats.type_name == NULL)
            stats.type_name = "<no log_type name>";

        if(stats.name == NULL)
            stats.name = "<no name>";

        LOG_FATAL("memory", "Allocator %s %s reported out of memory!", stats.type_name, stats.name);

            LOG_INFO(">memory", "new_size:    " MEMORY_FMT, MEMORY_PRINT(new_size));
            LOG_INFO(">memory", "old_size:    " MEMORY_FMT, MEMORY_PRINT(old_size));
            LOG_INFO(">memory", "old_ptr:     " PTR_FMT,    PTR_PRINT(old_ptr));
            LOG_INFO(">memory", "align:       %lli", (lli) align);

            LOG_INFO(">memory", "Allocator_Stats:");
            LOG_INFO(">>memory", "bytes_allocated:     " MEMORY_FMT, MEMORY_PRINT(stats.bytes_allocated));
            LOG_INFO(">>memory", "max_bytes_allocated: " MEMORY_FMT, MEMORY_PRINT(stats.max_bytes_allocated));

            LOG_INFO(">>memory", "allocation_count:    %lli", (lli) stats.allocation_count);
            LOG_INFO(">>memory", "deallocation_count:  %lli", (lli) stats.deallocation_count);
            LOG_INFO(">>memory", "reallocation_count:  %lli", (lli) stats.reallocation_count);
    
            log_callstack(">memory", LOG_TRACE, 1, "callstack:");

        log_flush();
        platform_debug_break(); 
        abort();
    }
#endif
    
    EXPORT Memory_Format get_memory_format(isize bytes)
    {
        isize TB = TEBI_BYTE;
        isize GB = GIBI_BYTE;
        isize MB = MEBI_BYTE;
        isize KB = KIBI_BYTE;
        isize B = (isize) 1;

        Memory_Format out = {0};
        out.unit = "";
        out.unit_value = 1;
        if(bytes >= TB)
        {
            out.unit = "TB";
            out.unit_value = TB;
        }
        else if(bytes >= GB)
        {
            out.unit = "GB";
            out.unit_value = GB;
        }
        else if(bytes >= MB)
        {
            out.unit = "MB";
            out.unit_value = MB;
        }
        else if(bytes >= KB)
        {
            out.unit = "KB";
            out.unit_value = KB;
        }
        else
        {
            out.unit = "B";
            out.unit_value = B;
        }

        out.fraction = (f64) bytes / (f64) out.unit_value;
        out.whole = (i32) (bytes / out.unit_value);
        out.remainder = (i32) (bytes / out.unit_value);

        return out;
    }
#endif
