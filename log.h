#ifndef LIB_LOG
#define LIB_LOG

#include "assert.h"
#include <stdarg.h>

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
    LOG_TYPE_INFO    = 1, //Used to print general info.
    LOG_TYPE_WARN,        //Used to print near error conditions
    LOG_TYPE_ERROR,       //Used to print errors
    LOG_TYPE_FATAL,       //Used to print errors just before giving up some important action
    LOG_TYPE_DEBUG,       //Used to print log for debug purposes. Is only logged in debug builds
    LOG_TYPE_TRACE,       //Used to print log for step debug purposes (prinf("HERE") and such). Is only logged in step debug builds

    LOG_TYPE_ENUM_MAX = 63, //This is the maximum value log types are allowed to have without being ignored.
} Log_Type;

typedef struct Logger Logger;
typedef void (*Vlog_Func)(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args);

typedef struct Logger {
    Vlog_Func log;
} Logger;

//Returns the default used logger
EXPORT Logger* log_system_get_logger();
//Sets the default used logger. Returns a pointer to the previous logger so it can be restored later.
EXPORT Logger* log_system_set_logger(Logger* logger);

EXPORT bool log_is_enabled();
EXPORT void log_disable();
EXPORT void log_enable();

EXPORT void log_group_push();   //Increases indentation of subsequent log messages
EXPORT void log_group_pop();    //Decreases indentation of subsequent log messages
EXPORT isize log_group_depth(); //Returns the current indentation of messages

EXPORT void log_message(const char* module, Log_Type type, Source_Info source, const char* format, ...);
EXPORT void vlog_message(const char* module, Log_Type type, Source_Info source, const char* format, va_list args);

//Default logging facility. Logs a message into the provided module cstring with log_type type (info, warn, error...)
#define LOG(module, log_type, format, ...)      PP_IF(DO_LOG, LOG_ALWAYS)(module, log_type, format, __VA_ARGS__)

//Logs a message type into the provided module cstring.
#define LOG_INFO(module, format, ...)           PP_IF(DO_LOG_INFO, LOG)(module, LOG_TYPE_INFO, format, __VA_ARGS__)
#define LOG_WARN(module, format, ...)           PP_IF(DO_LOG_WARN, LOG)(module, LOG_TYPE_WARN, format, __VA_ARGS__)
#define LOG_ERROR(module, format, ...)          PP_IF(DO_LOG_ERROR, LOG)(module, LOG_TYPE_ERROR, format, __VA_ARGS__)
#define LOG_FATAL(module, format, ...)          PP_IF(DO_LOG_FATAL, LOG)(module, LOG_TYPE_FATAL, format, __VA_ARGS__)
#define LOG_DEBUG(module, format, ...)          PP_IF(DO_LOG_DEBUG, LOG)(module, LOG_TYPE_DEBUG, format, __VA_ARGS__)
#define LOG_TRACE(module, format, ...)          PP_IF(DO_LOG_TRACE, LOG)(module, LOG_TYPE_TRACE, format, __VA_ARGS__)

//Logs a message. Does not get dissabled.
#define LOG_ALWAYS(module, log_type, format, ...) log_message(module, log_type, SOURCE_INFO(), format, __VA_ARGS__)
//Does not do anything (failed condition) but type checks the arguments
#define LOG_NEVER(module, log_type, format, ...)  ((module && false) ? log_message(module, log_type, SOURCE_INFO(),format, __VA_ARGS__) : (void) 0)

//Gets expanded when the particular type is dissabled.
#define _IF_NOT_DO_LOG(ignore)              LOG_NEVER
#define _IF_NOT_DO_LOG_INFO(ignore)         LOG_NEVER
#define _IF_NOT_DO_LOG_WARN(ignore)         LOG_NEVER
#define _IF_NOT_DO_LOG_ERROR(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_FATAL(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_DEBUG(ignore)        LOG_NEVER
#define _IF_NOT_DO_LOG_TRACE(ignore)        LOG_NEVER

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_LOG_IMPL)) && !defined(LIB_LOG_HAS_IMPL)
#define LIB_LOG_HAS_IMPL

void log_flush_all() {}
EXPORT void log_callstack(const char* log_module, Log_Type log_type, isize depth, isize skip);
EXPORT void log_captured_callstack(const char* log_module, Log_Type log_type, const void** callstack, isize callstack_size);

static Logger* _global_logger = NULL;
static isize _global_log_group_depth = 0;
static bool _global_log_enabled = true;

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

EXPORT bool log_is_enabled()
{
    return _global_log_enabled;
}
EXPORT void log_disable()
{
    _global_log_enabled = false;
}
EXPORT void log_enable()
{
    _global_log_enabled = true;
}

EXPORT void vlog_message(const char* module, Log_Type type, Source_Info source, const char* format, va_list args)
{
    if(_global_logger && _global_log_enabled)
        _global_logger->log(_global_logger, module, type, _global_log_group_depth, source, format, args);
}

EXPORT void log_message(const char* module, Log_Type type, Source_Info source, const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    vlog_message(module, type, source, format, args);                    
    va_end(args);                
}

#endif
