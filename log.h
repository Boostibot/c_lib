#ifndef JOT_LOG
#define JOT_LOG

// This file is focused on as-simpel-as-possible semi-structured logging.
// That is we attempt to give logs some structure but not too much (so that it is still convinient).
// 
// We use three primary pieces of information for our logs:
//  1) Log module - a simple string indicating from where the log came from. 
//                  The user is free to give this location any meaning (function, file, etc.)
//                  to group things however they please. 
// 
//  2) Log type - a number indicating what kind of log this is (info, warn, error, etc.).
//                These numbers can range from 0-63 (some slots are already taken). This
//                enables us to store the filter of allowed types as single 64 bit number
//                which can in turn be used to silence certain logs really easily.
// 
//  3) Indentation - a visual indicator to the hierarchy of messages. 
//                   This can be used to separate function calls. The simple global
//                   implementation also nests (as long as we remeber to call pop 
//                   when we are done). 
// 
// Log module in combination with log type enables us to extremely easily locate desired properties
// in the resulting log files. We grep for example for just LOG_ERROR from RENDER module. we can then 
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
    //We have chose to call this 'module' EXAMPLES
    //Usually we choose modules based on logical units
    // instead of code units. So for example RENDER, IO, INPUT
    // instead of log_example or My_Class
    LOG_INFO("EXAMPLES", "Starting examples!");
    
    LOG_DEBUG("EXAMPLES", "current time %i", clock());

    //Increases indentation for all subsequent calls untill log_group_pop is called
    //This can be used for example to distinguish nested functions such as now
    log_group(); 
    //Also dissable debug prints
    Log_Filter filter = log_disable(LOG_DEBUG);

        log_example_nested();

    //Restore previous state
    log_set_filter(filter);
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

#include "vformat.h"

#ifndef LOG_CUSTOM_SETTINGS
    #define DO_LOG          /* Disables all log types */   
    #define DO_LOG_INFO
    #define DO_LOG_OKAY
    #define DO_LOG_WARN 
    #define DO_LOG_ERROR
    #define DO_LOG_FATAL

    #ifndef NDEBUG
    #define DO_LOG_DEBUG
    #define DO_LOG_TRACE
    #endif
#endif

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
    LOG_ACTION_LOG      = 0, //Logs the provided log_list
    LOG_ACTION_FLUSH    = 1, //Only Flushes the log
    //Custom can be defined...
} Log_Action;

typedef struct Logger Logger;
typedef struct Log Log;
typedef void (*Log_Func)(Logger* logger, const Log* log_list, i32 group_depth, Log_Action action);

typedef struct Logger {
    Log_Func log;
} Logger;

typedef struct Log {
    //@NOTE: this struct is rather big 104B at the moment.
    //       This might seem scary but its not that much.
    //       In 10MB we are able to store 100,824 logs.

    //@NOTE: Notice that module and subject are const char*
    //       and thus as static strings. This is to make 
    //       our lives easier because it almost always end
    //       yp being static stirngs
    const char* module;
    const char* subject;
    String message;
    
    Log_Type type;
    i64 time;
    Source_Info source;

    struct Log* prev;
    struct Log* next;
    struct Log* first_child;
    struct Log* last_child;
} Log;

EXPORT Logger* log_get_logger(); //Returns the default used logger
EXPORT Logger* log_set_logger(Logger* logger); //Sets the default used logger. Returns a pointer to the previous logger so it can be restored later.

EXPORT Log_Filter log_get_filter(); //Returns the current global filter - For Log_Type type to be printed it must satissfy (filter & ((Log_Filter) 1 << type)) > 0
EXPORT Log_Filter log_set_filter(Log_Filter filter); //Sets the global filter. Returns previous value so it can be restored later.

EXPORT void log_flush();    //Flushes the logger
EXPORT void log_group();    //Increases group depth (indentation) of subsequent log messages
EXPORT void log_ungroup();  //Decreases group depth (indentation) of subsequent log messages
EXPORT i32  log_group_depth(); //Returns the current group depth
EXPORT void log_chain(const Log* log_list);

EXPORT const char* log_defualt_type_to_string(Log_Filter type);

EXPORT ATTRIBUTE_FORMAT_FUNC(format, 5) void log_message(const char* module, const char* subject, Log_Type type, Source_Info source, const Log* child, ATTRIBUTE_FORMAT_ARG const char* format, ...);
EXPORT void vlog_message(const char* module, const char* subject, Log_Type type, Source_Info source, const Log* child, const char* format, va_list args);

EXPORT ATTRIBUTE_FORMAT_FUNC(format, 4) void log_callstack(const char* log_module, Log_Type log_type, isize skip, ATTRIBUTE_FORMAT_ARG const char* format, ...);
EXPORT void log_just_callstack(const char* log_module, Log_Type log_type, isize depth, isize skip);
EXPORT void log_captured_callstack(const char* log_module, Log_Type log_type, const void* const* callstack, isize callstack_size);
EXPORT void log_translated_callstack(const char* log_module, Log_Type log_type, const Platform_Stack_Trace_Entry* translated, isize callstack_size);

typedef struct Memory_Format {
    const char* unit;
    isize unit_value;
    f64 fraction;

    i32 whole;
    i32 remainder;
} Memory_Format;

EXPORT Memory_Format get_memory_format(isize bytes);
EXPORT Allocator_Stats log_allocator_stats(const char* log_module, Log_Type log_type, Allocator* allocator);
EXPORT void log_allocator_stats_provided(const char* log_module, Log_Type log_type, Allocator_Stats stats);

//Logs a message. Does not get dissabled.
#define LOG(module, log_type, format, ...)                         log_message(module, "", log_type, SOURCE_INFO(), NULL, format, ##__VA_ARGS__)
#define LOG_CHILD(module, subject, log_type, child, format, ...)   log_message(module, subject, log_type, SOURCE_INFO(), child, format, ##__VA_ARGS__)

//Logs a message type into the provided module cstring.
#define LOG_INFO(module, format, ...)  PP_IF(DO_LOG_INFO,  LOG)(module, LOG_INFO,  format, ##__VA_ARGS__)
#define LOG_OKAY(module, format, ...)  PP_IF(DO_LOG_OKAY,  LOG)(module, LOG_OKAY,  format, ##__VA_ARGS__)
#define LOG_WARN(module, format, ...)  PP_IF(DO_LOG_WARN,  LOG)(module, LOG_WARN,  format, ##__VA_ARGS__)
#define LOG_ERROR(module, format, ...) PP_IF(DO_LOG_ERROR, LOG)(module, LOG_ERROR, format, ##__VA_ARGS__)
#define LOG_FATAL(module, format, ...) PP_IF(DO_LOG_FATAL, LOG)(module, LOG_FATAL, format, ##__VA_ARGS__)
#define LOG_DEBUG(module, format, ...) PP_IF(DO_LOG_DEBUG, LOG)(module, LOG_DEBUG, format, ##__VA_ARGS__)
#define LOG_TRACE(module, format, ...) PP_IF(DO_LOG_TRACE, LOG)(module, LOG_TRACE, format, ##__VA_ARGS__)

#define LOG_ERROR_CHILD(module, subject, child, format, ...) PP_IF(DO_LOG_ERROR, LOG_CHILD)(module, subject, LOG_ERROR, child, format, ##__VA_ARGS__)
#define LOG_FATAL_CHILD(module, subject, child, format, ...) PP_IF(DO_LOG_FATAL, LOG_CHILD)(module, subject, LOG_FATAL, child, format, ##__VA_ARGS__)

//Does not do anything (failed condition) but type checks the arguments
#define LOG_NEVER(module, subject, log_type, format, ...)  ((module && false) ? log_message(module, subject, log_type, SOURCE_INFO(), format, ##__VA_ARGS__) : (void) 0)

//Some of the ansi colors that can be used within logs. 
//However their usage is not recommended since these will be written to log files and thus make their parsing more difficult.
#define ANSI_COLOR_NORMAL       "\x1B[0m"
#define ANSI_COLOR_RED          "\x1B[31m"
#define ANSI_COLOR_BRIGHT_RED   "\x1B[91m"
#define ANSI_COLOR_GREEN        "\x1B[32m"
#define ANSI_COLOR_YELLOW       "\x1B[33m"
#define ANSI_COLOR_BLUE         "\x1B[34m"
#define ANSI_COLOR_MAGENTA      "\x1B[35m"
#define ANSI_COLOR_CYAN         "\x1B[36m"
#define ANSI_COLOR_WHITE        "\x1B[37m"
#define ANSI_COLOR_GRAY         "\x1B[90m"

//Gets expanded when the particular type is dissabled.
#define _IF_NOT_DO_LOG(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_INFO(ignore)   LOG_NEVER
#define _IF_NOT_DO_LOG_OKAY(ignore)   LOG_NEVER
#define _IF_NOT_DO_LOG_WARN(ignore)   LOG_NEVER
#define _IF_NOT_DO_LOG_ERROR(ignore)  LOG_NEVER
#define _IF_NOT_DO_LOG_FATAL(ignore)  LOG_NEVER
#define _IF_NOT_DO_LOG_DEBUG(ignore)  LOG_NEVER
#define _IF_NOT_DO_LOG_TRACE(ignore)  LOG_NEVER

#define TIME_FMT "%02i:%02i:%02i %03i"
#define TIME_PRINT(c) (int)(c).hour, (int)(c).minute, (int)(c).second, (int)(c).millisecond

#define STRING_FMT "%.*s"
#define STRING_PRINT(string) (int) (string).size, (string).data

#define MEMORY_FMT "%.2lf%s"
#define MEMORY_PRINT(bytes) get_memory_format((bytes)).fraction, get_memory_format((bytes)).unit
//@NOTE We call the fucntion twice. Its not optimal however I dont think its gonna be used in perf critical situations

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOG_IMPL)) && !defined(JOT_LOG_HAS_IMPL)
#define JOT_LOG_HAS_IMPL

typedef struct Global_Log_State {
    Log_Filter filter;
    Logger* logger;
    i32 group_depth;
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

enum 
{
    #ifdef DO_LOG
    _STATIC_LOG_ENABLED = 1
    #else
    _STATIC_LOG_ENABLED = 0
    #endif
};

EXPORT void logger_action(const Log* log_list, Log_Action action)
{   
    Global_Log_State* state = &_global_log_state;
    if(_STATIC_LOG_ENABLED && state->logger && (state->filter & ((Log_Filter) 1 << log_list->type)))
    {
        Logger* logger = state->logger;
        state->logger = NULL;
        logger->log(logger, log_list, state->group_depth, action);
        state->logger = logger;
    }
}

EXPORT void log_chain(const Log* log_list)
{
    logger_action(log_list, LOG_ACTION_LOG);
}
EXPORT void log_flush()
{   
    Global_Log_State* state = &_global_log_state;
    if(_STATIC_LOG_ENABLED && state->logger)
        state->logger->log(state->logger, NULL, 0, LOG_ACTION_FLUSH);
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
EXPORT i32 log_group_depth()
{
    return _global_log_state.group_depth;
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

EXPORT ATTRIBUTE_FORMAT_FUNC(format, 4) void log_message(const char* module, const char* subject, Log_Type type, Source_Info source, const Log* child, ATTRIBUTE_FORMAT_ARG const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    vlog_message(module, subject, type, source, child, format, args);                    
    va_end(args);            
}
EXPORT void vlog_message(const char* module, const char* subject, Log_Type type, Source_Info source, const Log* first_child, const char* format, va_list args)
{   
    Global_Log_State* state = &_global_log_state;
    if(_STATIC_LOG_ENABLED && state->logger && (state->filter & ((Log_Filter) 1 << type)))
    {
        enum {RESET_EVERY = 32, KEPT_SIZE = 512};
        typedef struct {
            String_Builder formatted;
            isize index;
            isize indentation;
        } Local_Cache;

        static ATTRIBUTE_THREAD_LOCAL Local_Cache _cache = {0};
        Local_Cache* cache = &_cache;

        //Reset cahce every once in a while if too big.
        if(cache->index % RESET_EVERY == 0)
        {
            if(cache->formatted.capacity == 0 || cache->formatted.capacity > KEPT_SIZE)
                builder_init_with_capacity(&cache->formatted, allocator_get_static(), KEPT_SIZE);
        }
        cache->index += 1;
        vformat_into(&cache->formatted, format, args);

        i32 extra_indentation = 0;
        for(; module[extra_indentation] == '>'; extra_indentation++);

        Log logged = {0};
        logged.module = module + extra_indentation;
        logged.subject = subject;
        logged.message = cache->formatted.string;
        logged.type = type;
        logged.source = source;
        logged.time = platform_epoch_time();
        logged.first_child = (Log*) first_child;
        
        //We temporarily dissable loggers while we are logging. This prevents log infinite recursion which occurs for example
        // when the logger fails to acquire a resource (memory) and that failiure logs 
        Logger* logger = state->logger;
        state->logger = NULL;
        logger->log(logger, &logged, state->group_depth + extra_indentation, LOG_ACTION_LOG);
        state->logger = logger;
    }
}

EXPORT const char* log_type_to_string(Log_Type type)
{
    switch(type)
    {
        case LOG_INFO: return "INFO"; break;
        case LOG_OKAY: return "SUCC"; break;
        case LOG_WARN: return "WARN"; break;
        case LOG_ERROR: return "ERROR"; break;
        case LOG_FATAL: return "FATAL"; break;
        case LOG_DEBUG: return "DEBUG"; break;
        case LOG_TRACE: return "TRACE"; break;
        default: return "";
    }
}

EXPORT ATTRIBUTE_FORMAT_FUNC(format, 4) void log_callstack(const char* log_module, Log_Type log_type, isize skip, ATTRIBUTE_FORMAT_ARG const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    vlog_message(log_module, "", log_type, SOURCE_INFO(), NULL, format, args);                    
    va_end(args);   
    
    log_group();
    log_just_callstack(log_module, log_type, -1, skip + 1);
    log_ungroup();
}

EXPORT void log_just_callstack(const char* log_module, Log_Type log_type, isize depth, isize skip)
{
    void* stack[256] = {0};
    if(depth < 0 || depth > 256)
        depth = 256;
    isize size = platform_capture_call_stack(stack, depth, skip + 1);
    log_captured_callstack(log_module, log_type, (const void**) stack, size);
}

INTERNAL bool _log_translated_callstack_and_check_main(const char* log_module, Log_Type log_type, const Platform_Stack_Trace_Entry* translated, isize callstack_size)
{
    for(isize j = 0; j < callstack_size; j++)
    {
        const Platform_Stack_Trace_Entry* entry = &translated[j];
        log_message(log_module, "", log_type, SOURCE_INFO(), NULL, "%-30s %s : %i", entry->function , entry->file, (int) entry->line);
        if(strcmp(entry->function, "main") == 0) //if reaches main stops (we dont care about OS stuff)
            return true;
    }

    return false;
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
        
        bool found_main = _log_translated_callstack_and_check_main(log_module, log_type, translated, remaining);
        if(found_main)
            break;
    }
}

EXPORT void log_translated_callstack(const char* log_module, Log_Type log_type, const Platform_Stack_Trace_Entry* translated, isize callstack_size)
{
    _log_translated_callstack_and_check_main(log_module, log_type, translated, callstack_size);
}

#ifndef ASSERT_CUSTOM_REPORT
    EXPORT ATTRIBUTE_FORMAT_FUNC(format, 5) void assertion_report(const char* expression, int line, const char* file, const char* function, ATTRIBUTE_FORMAT_ARG const char* format, ...)
    {
        Source_Info source = {line, file, function};
        log_message("assert", "", LOG_FATAL, source, NULL, "TEST(%s) TEST/ASSERT failed! (%s : %lli) ", expression, source.file, source.line);
        if(format != NULL && strlen(format) != 0)
        {
            log_message(">assert", "", LOG_FATAL, source, NULL, "message:");

            va_list args;               
            va_start(args, format);     
            vlog_message(">>assert", "", LOG_FATAL, source, NULL, format, args);
            va_end(args);  
        }

        log_callstack(">assert", LOG_TRACE, -1, "callstack:");
    }
#endif

EXPORT void log_allocator_stats_provided(const char* log_module, Log_Type log_type, Allocator_Stats stats)
{
    if(stats.type_name == NULL)
        stats.type_name = "<no type name>";

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
    
EXPORT Allocator_Stats log_allocator_stats(const char* log_module, Log_Type log_type, Allocator* allocator)
{
    Allocator_Stats stats = {0};
    if(allocator != NULL && allocator->get_stats != NULL)
    {
        stats = allocator_get_stats(allocator);
        log_allocator_stats_provided(log_module, log_type, stats);
    }
    else
        LOG(log_module, log_type, "Allocator NULL or missing get_stats callback.");

    return stats;
}

#ifndef ALLOCATOR_CUSTOM_OUT_OF_MEMORY
    EXPORT void allocator_out_of_memory(
        Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align, const char* format_string, ...)
    {
        Allocator_Stats stats = {0};
        if(allocator != NULL && allocator->get_stats != NULL)
            stats = allocator_get_stats(allocator);
        
        if(stats.type_name == NULL)
            stats.type_name = "<no type name>";

        if(stats.name == NULL)
            stats.name = "<no name>";

        LOG_FATAL("memory", "Allocator %s %s reported out of memory! (%s : %lli)", stats.type_name, stats.name);

            LOG_INFO(">memory", "new_size:    %lli B", new_size);
            if(old_ptr != NULL)
                LOG_INFO(">memory", "old_ptr:     0x%08llx", (lli) old_ptr);
            else
                LOG_INFO(">memory", "old_ptr:     NULL");
            LOG_INFO(">memory", "old_size:    %lli B", old_size);
            LOG_INFO(">memory", "align:       %lli B", align);
        
            if(format_string != NULL && strlen(format_string) > 0)
            {
                va_list args;               
                va_start(args, format_string);     
                vlog_message(">>memory", "", LOG_FATAL, SOURCE_INFO(), NULL, format_string, args);
                va_end(args);  
            }

            LOG_INFO(">memory", "Allocator_Stats:");
                log_allocator_stats_provided(">>memory", LOG_INFO, stats);
    
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
