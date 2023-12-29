#ifndef JOT_LOG
#define JOT_LOG

// This file is focused on as-simpel-as-possible semi-structured logging.
// That is we attempt to give logs some structure but not too much (so that it is still convinient).
// 
// The primary design choice is thus how much and what to keep structured. We select three primary
// independent pieces:
//  1) log_module - a simple string indicating from where the log came from. 
//                  The user is free to give this location any meaning (function, file, etc.)
//                  to group things however they please.
// 
//  2) log_type - a number indicating what kind of log this is (info, warn, error, etc.). See below. 
// 
//  3) indendtation/groups 
// 
// The choice to have Log_Type which qualifies some basic log categories instead of the usual 
//  severity level approach was chosen because of the following: 
// Severity level has the two primary problems:
//  1) Lack of meaning:
//     The choice between severity level 5 and 6 is largely abitrary. This is because the meaning of 
//     severity is distacted by how rest of the codebase uses it.
//  2) Lack of fine grained control:
//     If we want to dissable all info messages but keep debug emssaegs we simply cannot 
//     (assuming severity of debug is smaller then of info - which is usually the case)
//

#include "defines.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef LOG_CUSTOM_SETTINGS
    #define DO_LOG          /* Disables all log types */   
    #define DO_LOG_INFO     
    #define DO_LOG_WARN 
    #define DO_LOG_ERROR
    #define DO_LOG_FATAL

    #ifndef NDEBUG
    #define DO_LOG_DEBUG
    #define DO_LOG_TRACE
    #endif
#endif

typedef enum Log_Type {
    LOG_TYPE_FLUSH = 0, //only flushes the log but doesnt print anything
    LOG_TYPE_INFO,  //Used to print general info.
    LOG_TYPE_WARN,  //Used to print near error conditions
    LOG_TYPE_ERROR, //Used to print errors
    LOG_TYPE_FATAL, //Used to print errors just before giving up some important action
    LOG_TYPE_DEBUG, //Used to print log for debug purposes. Is only logged in debug builds
    LOG_TYPE_TRACE, //Used to print log for step debug purposes (prinf("HERE") and such). Is only logged in step debug builds

    LOG_TYPE_ENUM_MAX = 63, //This is the maximum value log types are allowed to have without being ignored.
} Log_Type;

typedef struct Source_Info {
    int64_t line;
    const char* file;
    const char* function;
} Source_Info;

typedef struct Logger Logger;
typedef void (*Vlog_Func)(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args);

typedef struct Logger {
    Vlog_Func log;
} Logger;

//Returns the default used logger
EXPORT Logger* log_system_get_logger();
//Sets the default used logger. Returns a pointer to the previous logger so it can be restored later.
EXPORT Logger* log_system_set_logger(Logger* logger);

//
EXPORT u64  log_get_mask();
EXPORT u64  log_set_mask(u64 mask);
EXPORT u64  log_disable(isize log_type);
EXPORT u64  log_enable(isize log_type);
EXPORT bool log_is_enabled(isize log_type);

EXPORT void  log_group_push();   //Increases indentation of subsequent log messages
EXPORT void  log_group_pop();    //Decreases indentation of subsequent log messages
EXPORT isize log_group_depth(); //Returns the current indentation of messages

EXPORT MODIFIER_FORMAT_FUNC(format, 4) void log_message(const char* module, Log_Type type, Source_Info source, MODIFIER_FORMAT_ARG const char* format, ...);
EXPORT void vlog_message(const char* module, Log_Type type, Source_Info source, const char* format, va_list args);
EXPORT void log_flush();

EXPORT MODIFIER_FORMAT_FUNC(format, 4) void log_callstack(const char* log_module, Log_Type log_type, isize skip, MODIFIER_FORMAT_ARG const char* format, ...);
EXPORT void log_just_callstack(const char* log_module, Log_Type log_type, isize depth, isize skip);
EXPORT void log_captured_callstack(const char* log_module, Log_Type log_type, const void* const* callstack, isize callstack_size);
EXPORT void log_translated_callstack(const char* log_module, Log_Type log_type, const Platform_Stack_Trace_Entry* translated, isize callstack_size);

EXPORT const char* log_type_to_string(Log_Type type);
EXPORT Logger def_logger_make();
EXPORT void def_logger_func(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args);

//Default logging facility. Logs a message into the provided module cstring with log_type type (info, warn, error...)
#define LOG(module, log_type, format, ...)      PP_IF(DO_LOG, LOG_ALWAYS)(module, log_type, format, ##__VA_ARGS__)
#define VLOG(module, log_type, format, args)    PP_IF(DO_LOG, LOG_ALWAYS)(module, log_type, format, args)

//Logs a message type into the provided module cstring.
#define LOG_INFO(module, format, ...)           PP_IF(DO_LOG_INFO, LOG)(module, LOG_TYPE_INFO, format, ##__VA_ARGS__)
#define LOG_WARN(module, format, ...)           PP_IF(DO_LOG_WARN, LOG)(module, LOG_TYPE_WARN, format, ##__VA_ARGS__)
#define LOG_ERROR(module, format, ...)          PP_IF(DO_LOG_ERROR, LOG)(module, LOG_TYPE_ERROR, format, ##__VA_ARGS__)
#define LOG_FATAL(module, format, ...)          PP_IF(DO_LOG_FATAL, LOG)(module, LOG_TYPE_FATAL, format, ##__VA_ARGS__)
#define LOG_DEBUG(module, format, ...)          PP_IF(DO_LOG_DEBUG, LOG)(module, LOG_TYPE_DEBUG, format, ##__VA_ARGS__)
#define LOG_TRACE(module, format, ...)          PP_IF(DO_LOG_TRACE, LOG)(module, LOG_TYPE_TRACE, format, ##__VA_ARGS__)

//Logs a message. Does not get dissabled.
#define LOG_ALWAYS(module, log_type, format, ...)   log_message(module, log_type, SOURCE_INFO(), format, ##__VA_ARGS__)
#define VLOG_ALWAYS(module, log_type, format, args) vlog_message(module, log_type, SOURCE_INFO(), format, args)
//Does not do anything (failed condition) but type checks the arguments
#define LOG_NEVER(module, log_type, format, ...)  ((module && false) ? log_message(module, log_type, SOURCE_INFO(),format, ##__VA_ARGS__) : (void) 0)

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
#define _IF_NOT_DO_LOG(ignore)              LOG_NEVER
#define _IF_NOT_DO_LOG_INFO(ignore)         LOG_NEVER
#define _IF_NOT_DO_LOG_WARN(ignore)         LOG_NEVER
#define _IF_NOT_DO_LOG_ERROR(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_FATAL(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_DEBUG(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_TRACE(ignore)        LOG_NEVER

//Pre-Processor (PP) utils
#define PP_STRINGIFY_(x)        #x
#define PP_CONCAT2(a, b)        a ## b
#define PP_CONCAT3(a, b, c)     PP_CONCAT2(PP_CONCAT2(a, b), c)
#define PP_CONCAT4(a, b, c, d)  PP_CONCAT2(PP_CONCAT3(a, b, c), d)
#define PP_CONCAT(a, b)         PP_CONCAT2(a, b)
#define PP_STRINGIFY(x)         PP_STRINGIFY_(x)
#define PP_ID(x)                x

//if CONDITION_DEFINE is defined: expands to x, 
//else: expands to _IF_NOT_##CONDITION_DEFINE(x). See above how to use this.
//The reason for its use is that simply all other things I have tried either didnt
// work or failed to compose for obscure reasons
#define PP_IF(CONDITION_DEFINE, x)         PP_CONCAT(_IF_NOT_, CONDITION_DEFINE)(x)
#define _IF_NOT_(x) x

#ifdef __cplusplus
    #define SOURCE_INFO() Source_Info{__LINE__, __FILE__, __FUNCTION__}
#else
    #define SOURCE_INFO() (Source_Info){__LINE__, __FILE__, __FUNCTION__}
#endif 

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOG_IMPL)) && !defined(JOT_LOG_HAS_IMPL)
#define JOT_LOG_HAS_IMPL

//Is stateless so it can not be MODIFIER_THREAD_LOCAL
static Logger _global_def_logger = {def_logger_func};
static MODIFIER_THREAD_LOCAL Logger* _global_logger = &_global_def_logger;
static MODIFIER_THREAD_LOCAL isize _global_log_group_depth = 0;
static MODIFIER_THREAD_LOCAL u64 _global_log_mask = ~(u64) 0; //All channels on!

EXPORT Logger* log_system_get_logger()
{
    return _global_logger;
}

EXPORT Logger* log_system_set_logger(Logger* logger)
{
    Logger* before = _global_logger;
    _global_logger = logger;
    return before;
}

EXPORT void log_group_push()
{
    _global_log_group_depth ++;
}
EXPORT void log_group_pop()
{
    _global_log_group_depth --;
}
EXPORT isize log_group_depth()
{
    return _global_log_group_depth;
}

EXPORT u64 log_get_mask()
{
    return _global_log_mask;
}
EXPORT u64 log_set_mask(u64 mask)
{
    u64* gloabl_mask = &_global_log_mask;
    u64 prev = *gloabl_mask; 
    *gloabl_mask = mask;
    return prev;
}
EXPORT bool log_is_enabled(isize log_type)
{
    u64 log_bit = (u64) 1 << log_type;
    bool enabled = (log_bit & _global_log_mask) > 0;
    return enabled;
}

EXPORT u64 log_disable(isize log_type)
{
    u64* mask = &_global_log_mask;
    u64 prev = *mask; 
    u64 log_bit = (u64) 1 << log_type;
    *mask = *mask & ~log_bit;
    return prev;
}
EXPORT u64 log_enable(isize log_type)
{
    u64* mask = &_global_log_mask;
    u64 prev = *mask; 
    u64 log_bit = (u64) 1 << log_type;
    *mask = *mask | log_bit;
    return prev;
}

EXPORT void vlog_message(const char* module, Log_Type type, Source_Info source, const char* format, va_list args)
{
    bool static_enabled = false;
    #ifdef DO_LOG
        static_enabled = true;
    #endif
    Logger* global_logger = _global_logger;
    if(static_enabled && global_logger && log_is_enabled(type))
        global_logger->log(global_logger, module, type, _global_log_group_depth, source, format, args);
}

EXPORT MODIFIER_FORMAT_FUNC(format, 4) void log_message(const char* module, Log_Type type, Source_Info source, MODIFIER_FORMAT_ARG const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    vlog_message(module, type, source, format, args);                    
    va_end(args);                
}

EXPORT void log_flush()
{
    log_message("", LOG_TYPE_FLUSH, SOURCE_INFO(), " ");
}

EXPORT const char* log_type_to_string(Log_Type type)
{
    switch(type)
    {
        case LOG_TYPE_FLUSH: return "FLUSH"; break;
        case LOG_TYPE_INFO: return "INFO"; break;
        case LOG_TYPE_WARN: return "WARN"; break;
        case LOG_TYPE_ERROR: return "ERROR"; break;
        case LOG_TYPE_FATAL: return "FATAL"; break;
        case LOG_TYPE_DEBUG: return "DEBUG"; break;
        case LOG_TYPE_TRACE: return "TRACE"; break;
        case LOG_TYPE_ENUM_MAX:
        default: return "";
    }
}

EXPORT Logger def_logger_make()
{
    Logger out = {def_logger_func};
    return out;
}

EXPORT void def_logger_func(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args)
{
    (void) logger;
    (void) source;
    if(type == LOG_TYPE_FLUSH)
        return;

    Platform_Calendar_Time now = platform_local_calendar_time_from_epoch_time(platform_epoch_time());
    const char* color_mode = ANSI_COLOR_NORMAL;
    if(type == LOG_TYPE_ERROR || type == LOG_TYPE_FATAL)
        color_mode = ANSI_COLOR_BRIGHT_RED;
    else if(type == LOG_TYPE_WARN)
        color_mode = ANSI_COLOR_YELLOW;
    else if(type == LOG_TYPE_TRACE || type == LOG_TYPE_DEBUG)
        color_mode = ANSI_COLOR_GRAY;

    printf("%s%02i:%02i:%02i %03i %5s %6s: ", color_mode, now.hour, now.minute, now.second, now.millisecond, log_type_to_string(type), module);
    for(isize i = 0; i < indentation; i++)
        printf("   ");
    vprintf(format, args);
    printf(ANSI_COLOR_NORMAL"\n");
}


EXPORT MODIFIER_FORMAT_FUNC(format, 4) void log_callstack(const char* log_module, Log_Type log_type, isize skip, MODIFIER_FORMAT_ARG const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    VLOG(log_module, log_type, format, args);                    
    va_end(args);   
    
    log_group_push();
    log_just_callstack(log_module, log_type, -1, skip + 1);
    log_group_pop();
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
        log_message(log_module, log_type, SOURCE_INFO(), "%-30s %s : %i", entry->function , entry->file, (int) entry->line);
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

#endif
