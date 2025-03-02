#ifndef MODULE_LOG
#define MODULE_LOG

#ifndef EXTERNAL
    #define EXTERNAL
#endif

#ifndef INTERNAL
    #define INTERNAL static
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef enum Log_Type {
    LOG_FLUSH,      //only flushes the log but doesnt log anything
    LOG_TRACE,      //step debug (prinf("HERE") etc.)
    LOG_DEBUG,      //debug messages/bugs
    LOG_INFO,       //general info.
    LOG_OKAY,       //the opposites of errors
    LOG_WARN,       //near error conditions
    LOG_ERROR,      //errors
    LOG_FATAL,      //just before aborting thread/process
} Log_Type;

typedef struct Log_Event {
    const char* name;
    const char* file;
    const char* function;
    Log_Type type;
    int32_t line;
    int64_t indentation;
} Log_Event;

typedef struct Logger {
    void (*log)(struct Logger* self, Log_Event event, const char* format, va_list args);
} Logger;

EXTERNAL Logger* console_logger(); //returns the default logger which only logs to console
EXTERNAL Logger* silent_logger(); //returns a logger that does nothing
EXTERNAL Logger* log_get_logger();
EXTERNAL Logger* log_set_logger(Logger* logger);
EXTERNAL const char* log_type_to_string(Log_Type type);

EXTERNAL void log_fmt(Logger* logger, Log_Type type, const char* module, int32_t line, const char* file, const char* function, const char* format, ...);
EXTERNAL void log_vfmt(Logger* logger, Log_Type type, const char* module, int32_t line, const char* file, const char* function, const char* format, va_list args);
EXTERNAL void log_flush(Logger* logger);

EXTERNAL void log_callstack(Log_Type type, const char* module, int64_t skip);
EXTERNAL void log_captured_callstack(Log_Type type, const char* module, void** callstack, int64_t callstack_size);

#define LOGGER_LOG(logger, log_type, module, format, ...) log_fmt(logger, log_type, module, __LINE__, __FILE__, __func__, format, ##__VA_ARGS__)
#define LOGGER_LOGV(logger, log_type, module, format, ...) log_vfmt(logger, log_type, module, __LINE__, __FILE__, __func__, format, args)
#define LOG(log_type, module, format, ...)   LOGGER_LOG(log_get_logger(), (log_type), (module), (format), ##__VA_ARGS__)
#define LOGV(log_type, module, format, args) LOGGER_LOGV(log_get_logger(), (log_type), (module), (format), (args))
#define LOG_INFO(module, format, ...)  LOG(LOG_INFO,  module, format, ##__VA_ARGS__)
#define LOG_OKAY(module, format, ...)  LOG(LOG_OKAY,  module, format, ##__VA_ARGS__)
#define LOG_WARN(module, format, ...)  LOG(LOG_WARN,  module, format, ##__VA_ARGS__)
#define LOG_ERROR(module, format, ...) LOG(LOG_ERROR, module, format, ##__VA_ARGS__)
#define LOG_FATAL(module, format, ...) LOG(LOG_FATAL, module, format, ##__VA_ARGS__)
#define LOG_DEBUG(module, format, ...) LOG(LOG_DEBUG, module, format, ##__VA_ARGS__)
#define LOG_TRACE(module, format, ...) LOG(LOG_TRACE, module, format, ##__VA_ARGS__)
#define LOG_HERE(...) LOG_TRACE("here", "%s %s:%i", __func__, __FILE__, __LINE__)

//Printing helpers
#define STRING_PRINT(str) (int) (str).count, (str).data

typedef struct String_Buffer_16 {
    char data[16];
} String_Buffer_16;

typedef struct String_Buffer_64 {
    char data[64];
} String_Buffer_64;

EXTERNAL String_Buffer_16 format_ptr(void* ptr); //returns "0x00000ff76344ae64"
EXTERNAL String_Buffer_16 format_bytes(int64_t bytes); //returns "39B" "64KB", "10.3MB", "5.3GB", "7.531TB" etc.
EXTERNAL String_Buffer_16 format_seconds(double seconds); //returns "153ns", "10μs", "6.3ms", "15.2s". But doesnt go to hours, days etc.
EXTERNAL String_Buffer_16 format_nanoseconds(int64_t ns); //returns "153ns", "10μs", "6.3ms", "15.2s". But doesnt go to hours, days etc.

//File logger - logs to console and/or file. Is safe to be used across multiple threads
enum {
    FILE_LOGGER_FILE_PATH = 1,
    FILE_LOGGER_FILE_APPEND = 2,
    FILE_LOGGER_NO_CONSOLE_PRINT = 4,
    FILE_LOGGER_NO_CONSOLE_COLORS = 8,
    FILE_LOGGER_USE = 16,
};
typedef struct File_Logger {
    Logger logger;
    FILE* file;
    char* path;
    uint32_t flags;
    uint32_t _;
    Logger* prev_logger;
} File_Logger;

EXTERNAL void file_logger_log(Logger* self, Log_Event event, const char* format, va_list args);
EXTERNAL bool file_logger_init(File_Logger* logger, const char* path, uint32_t flags);
EXTERNAL void file_logger_deinit(File_Logger* logger);

EXTERNAL void silent_logger_log(Logger* self, Log_Event event, const char* format, va_list args);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_LOG)) && !defined(MODULE_HAS_IMPL_LOG)
#define MODULE_HAS_IMPL_LOG

static File_Logger _console_logger = {file_logger_log};
static Logger _silent_logger = {silent_logger_log};
static _Thread_local Logger* _thread_logger = &_console_logger.logger;

EXTERNAL Logger* log_get_logger()
{
    return _thread_logger;
}

EXTERNAL Logger* log_set_logger(Logger* logger)
{
    Logger* before = _thread_logger;
    _thread_logger = logger;
    return before;
}
EXTERNAL void log_vfmt(Logger* logger, Log_Type type, const char* module, int32_t line, const char* file, const char* function, const char* format, va_list args)
{
    if(logger)
    {
        size_t extra_indentation = 0;
        for(; module[extra_indentation] == '>'; extra_indentation++);

        Log_Event event = {0};
        event.file = file;
        event.function = function;
        event.line = line;
        event.type = type;
        event.name = module + extra_indentation;
        event.indentation = extra_indentation;
        logger->log(logger, event, format, args);
    }
}
EXTERNAL void log_fmt(Logger* logger, Log_Type type, const char* module, int32_t line, const char* file, const char* function, const char* format, ...)
{
    va_list args;               
    va_start(args, format);     
    log_vfmt(logger, type, module, line, file, function, format, args);                    
    va_end(args);      
}

EXTERNAL void log_flush(Logger* logger)
{
    log_fmt(logger, LOG_FLUSH, "", __LINE__, __FILE__, __FUNCTION__, " ");
}

EXTERNAL const char* log_type_to_string(Log_Type type)
{
    switch(type)
    {
        case LOG_FLUSH: return "FLUSH"; break;
        case LOG_INFO: return "INFO"; break;
        case LOG_OKAY: return "OKAY"; break;
        case LOG_WARN: return "WARN"; break;
        case LOG_ERROR: return "ERROR"; break;
        case LOG_FATAL: return "FATAL"; break;
        case LOG_DEBUG: return "DEBUG"; break;
        case LOG_TRACE: return "TRACE"; break;
        default: return "";
    }
}

//Format helpers
EXTERNAL String_Buffer_16 format_ptr(void* ptr)
{
    String_Buffer_16 out = {0};
    snprintf(out.data, sizeof out.data, "0x%08llx", (long long) ptr);
    return out;
}

EXTERNAL String_Buffer_16 format_bytes(int64_t bytes)
{
    int64_t abs = bytes > 0 ? bytes : -bytes;
    String_Buffer_16 out = {0};
    int64_t TB_ = (int64_t) 1024*1024*1024*1024;
    int64_t GB_ = (int64_t) 1024*1024*1024;
    int64_t MB_ = (int64_t) 1024*1024;
    int64_t KB_ = (int64_t) 1024;

    if(abs >= TB_)
        snprintf(out.data, sizeof out.data, "%.3lfTB", (double) bytes / (double) TB_);
    else if(abs >= GB_)
        snprintf(out.data, sizeof out.data, "%.2lfGB", (double) bytes / (double) GB_);
    else if(abs >= MB_)
        snprintf(out.data, sizeof out.data, "%.2lfMB", (double) bytes / (double) MB_);
    else if(abs >= KB_)
        snprintf(out.data, sizeof out.data, "%.1lfKB", (double) bytes / (double) KB_);
    else
        snprintf(out.data, sizeof out.data, "%lliB", (long long) bytes);

    return out;
}

EXTERNAL String_Buffer_16 format_nanoseconds(int64_t ns)
{
    int64_t sec = (int64_t) 1000*1000*1000;
    int64_t milli = (int64_t) 1000*1000;
    int64_t micro = (int64_t) 1000;

    int64_t abs = ns > 0 ? ns : -ns;
    String_Buffer_16 out = {0};
    if(abs >= sec)
        snprintf(out.data, sizeof out.data, "%.2lfs", (double) ns / (double) sec);
    else if(abs >= milli)
        snprintf(out.data, sizeof out.data, "%.2lfms", (double) ns / (double) milli);
    else if(abs >= micro)
        snprintf(out.data, sizeof out.data, "%.2lfμs", (double) ns / (double) micro);
    else
        snprintf(out.data, sizeof out.data, "%llins", (long long) ns);

    return out;
}

EXTERNAL String_Buffer_16 format_seconds(double seconds)
{
    return format_nanoseconds((int64_t) (seconds * 1000*1000*1000));
}

// File logger
typedef struct _Log_Builder {
    char* data;
    int64_t capacity;
    bool is_backed;
    bool _[7];
    int64_t size;
} _Log_Builder;


INTERNAL char* _log_builder_append_vfmt(_Log_Builder* builder_or_null, const char* fmt, va_list args);
INTERNAL char* _log_builder_append_fmt(_Log_Builder* builder_or_null, const char* fmt, ...);
INTERNAL void _log_builder_deinit(_Log_Builder* builder);
INTERNAL const char* _log_thread_name();

#include <ctype.h>
// #include <string.h>
EXTERNAL void file_logger_log(Logger* self, Log_Event event, const char* format, va_list args)
{
    const char* CONSOLE_COLOR_NORMAL =       "\x1B[0m"; (void) CONSOLE_COLOR_NORMAL;
    const char* CONSOLE_COLOR_RED =          "\x1B[31m"; (void) CONSOLE_COLOR_RED;
    const char* CONSOLE_COLOR_BRIGHT_RED =   "\x1B[91m"; (void) CONSOLE_COLOR_BRIGHT_RED;
    const char* CONSOLE_COLOR_GREEN =        "\x1B[32m"; (void) CONSOLE_COLOR_GREEN;
    const char* CONSOLE_COLOR_YELLOW =       "\x1B[33m"; (void) CONSOLE_COLOR_YELLOW;
    const char* CONSOLE_COLOR_BLUE =         "\x1B[34m"; (void) CONSOLE_COLOR_BLUE;
    const char* CONSOLE_COLOR_MAGENTA =      "\x1B[35m"; (void) CONSOLE_COLOR_MAGENTA;
    const char* CONSOLE_COLOR_CYAN =         "\x1B[36m"; (void) CONSOLE_COLOR_CYAN;
    const char* CONSOLE_COLOR_WHITE =        "\x1B[37m"; (void) CONSOLE_COLOR_WHITE;
    const char* CONSOLE_COLOR_GRAY =         "\x1B[90m"; (void) CONSOLE_COLOR_GRAY;

    File_Logger* logger = (File_Logger*) (void*) self;
    if(event.type == LOG_FLUSH)
    {
        if(logger->file)
            fflush(logger->file);
    }
    else
    {
        //Make log line prefix
        struct timespec ts = {0};
        (void) timespec_get(&ts, TIME_UTC);
        struct tm* now = gmtime(&ts.tv_sec);

        const char* thread_name = _log_thread_name();

        size_t prefix_len = 0; (void) prefix_len;
        char prefix_backing[128]; (void) prefix_backing;
        prefix_len = snprintf(prefix_backing, sizeof prefix_backing, 
            "%02i:%02i:%02i %-6.30s %-5.5s %.20s", 
            now->tm_hour, now->tm_min, now->tm_sec, thread_name, log_type_to_string(event.type), event.name);

        //Format user
        char user_backing[512]; (void) user_backing;
        _Log_Builder user_builder = {user_backing, sizeof user_backing, true};
        _log_builder_append_vfmt(&user_builder, format, args);

        //trim trailing whitespace
        for(; user_builder.size > 0; user_builder.size--)
            if(!isspace(user_builder.data[user_builder.size - 1]))
                break;

        //prefix each line of user message with log prefix
        char complete_backing[512]; (void) complete_backing;
        _Log_Builder complete_builder = {complete_backing, sizeof complete_backing, true};
        
        int64_t line_from = 0;
        for(int64_t i = 0; i <= user_builder.size; i++)
        {
            if(i == user_builder.size || user_builder.data[i] == '\n')
            {
                const char* line = user_builder.data + line_from;
                int line_len = (int)(i - line_from);
                _log_builder_append_fmt(&complete_builder, "%s: %*.s%.*s\n", prefix_backing, event.indentation*2, "", line_len, line);
                line_from = i + 1;
            }
        }

        //print into file and or console
        if((logger->flags & FILE_LOGGER_NO_CONSOLE_PRINT) == 0)
        {
            if(logger->flags & FILE_LOGGER_NO_CONSOLE_COLORS)
                puts(complete_builder.data);
            else
            {
                const char* line_begin = CONSOLE_COLOR_NORMAL;
                const char* line_end = CONSOLE_COLOR_NORMAL;
                if(event.type == LOG_ERROR || event.type == LOG_FATAL)
                    line_begin = CONSOLE_COLOR_BRIGHT_RED;
                else if(event.type == LOG_WARN)
                    line_begin = CONSOLE_COLOR_YELLOW;
                else if(event.type == LOG_OKAY)
                    line_begin = CONSOLE_COLOR_GREEN;
                else if(event.type == LOG_TRACE || event.type == LOG_DEBUG)
                    line_begin = CONSOLE_COLOR_GRAY;

                printf("%s%s%s", line_begin, complete_builder.data, line_end);
            }
        }
        
        if(logger->file)
            fputs(complete_builder.data, logger->file);

        _log_builder_deinit(&user_builder);
        _log_builder_deinit(&complete_builder);
    }
}

EXTERNAL void file_logger_deinit(File_Logger* logger)
{
    //restore logger
    if(logger->flags & FILE_LOGGER_USE)
        log_set_logger(logger->prev_logger);
    if(logger->file)
        fclose(logger->file);
    free(logger->path);
    memset(logger, 0, sizeof *logger);
}

EXTERNAL bool file_logger_init(File_Logger* logger, const char* path, uint32_t flags)
{
    file_logger_deinit(logger);

    const char* open_mode = flags & FILE_LOGGER_FILE_APPEND ? "ab" : "wb"; 
    char* filename = NULL;
    if((flags & FILE_LOGGER_FILE_PATH) && path)
        filename = _log_builder_append_fmt(NULL, "%s", path);
    else
    {
        struct timespec ts = {0};
        (void) timespec_get(&ts, TIME_UTC);
        struct tm* now = localtime(&ts.tv_sec);

        filename = _log_builder_append_fmt(NULL, 
            "%s/%02i-%02i-%02i__%02i-%02i-%02i.log", 
            path ? path : "logs", now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);
    }

    FILE* file = fopen(filename, open_mode);
    logger->file = file;
    logger->path = filename;
    logger->flags = flags;
    logger->logger.log = file_logger_log;

    if(flags & FILE_LOGGER_USE)
        logger->prev_logger = log_set_logger(&logger->logger);

    return file != NULL;
}

EXTERNAL void silent_logger_log(Logger* self, Log_Event event, const char* format, va_list args)
{
    (void) self;
    (void) event;
    (void) format;
    (void) args;
}

EXTERNAL Logger* console_logger()
{
    return &_console_logger.logger;
}

EXTERNAL Logger* silent_logger()
{
    return &_silent_logger;
}

INTERNAL void _log_builder_deinit(_Log_Builder* builder)
{
    if(builder->data && builder->is_backed == false) 
        free(builder->data);
    memset(builder, 0, sizeof *builder);
}

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...)              assert(x)
#endif  

INTERNAL char* _log_builder_append_vfmt(_Log_Builder* builder_or_null, const char* fmt, va_list args)
{
    _Log_Builder empty = {0};
    _Log_Builder* builder = builder_or_null ? builder_or_null : &empty;

    va_list copy;
    va_copy(copy, args);
    ASSERT(builder->capacity >= builder->size);
    int count = vsnprintf(builder->data + builder->size, (size_t) (builder->capacity - builder->size), fmt, copy);
    if(builder->size + count >= builder->capacity) {
        void* old_data = builder->data;
        isize new_capacity = builder->capacity*2 + 8;
        if(new_capacity < builder->size + count)
            new_capacity = builder->size + count;

        builder->data = (char*) malloc((size_t) (new_capacity + 1));
        memcpy(builder->data, old_data, (size_t) builder->size);
        if(builder->is_backed == false)
            free(old_data);

        builder->is_backed = false;
        builder->capacity = new_capacity;
        int count2 = vsnprintf(builder->data + builder->size, (size_t) (builder->capacity - builder->size), fmt, args);
        ASSERT(count == count2);
    }

    builder->size += count; 
    ASSERT(builder->capacity >= builder->size);
    return builder->data;
}

INTERNAL char* _log_builder_append_fmt(_Log_Builder* builder_or_null, const char* fmt, ...)
{
    va_list args;               
    va_start(args, fmt);    
    char* out = _log_builder_append_vfmt(builder_or_null, fmt, args);
    va_end(args);
    return out;  
}

#if defined(MODULE_PLATFORM) || defined(MODULE_ALL_COUPLED)
    #include "platform.h"
    INTERNAL const char* _log_thread_name()
    {
        return platform_thread_get_current_name();
    }

    EXTERNAL void log_callstack(Log_Type type, const char* module, int64_t skip)
    {
        void* stack[256] = {0};
        int64_t size = platform_capture_call_stack(stack, 256, skip + 1);
        log_captured_callstack(type, module, stack, size);
    }

    #include <string.h>
    EXTERNAL void log_captured_callstack(Log_Type type, const char* module, void** callstack, int64_t callstack_size)
    {
        if(callstack_size < 0 || callstack == NULL)
            callstack_size = 0;

        enum {TRANSLATE_AT_ONCE = 8};
        for(int64_t i = 0; i < callstack_size; i += TRANSLATE_AT_ONCE)
        {
            int64_t remaining = callstack_size - i;
            if(remaining > TRANSLATE_AT_ONCE)
                remaining = TRANSLATE_AT_ONCE;

            Platform_Stack_Trace_Entry translated[TRANSLATE_AT_ONCE] = {0};
            platform_translate_call_stack(translated, callstack + i, remaining);
        
            for(int64_t j = 0; j < remaining; j++)
            {
                const Platform_Stack_Trace_Entry* entry = &translated[j];
                LOG(type, module, "%-30s %s:%i", entry->function, entry->file, (int) entry->line);

                if(strcmp(entry->function, "main") == 0) {
                    i = callstack_size;
                    break;
                }
            }
        }
    }

    EXTERNAL bool panic_default_handler_break_into_debugger(void* context)
    {
        (void) context;
        return platform_is_debugger_attached() == 1;
    }
#else
    INTERNAL const char* _log_thread_name()
    {
        //poor mans thread id - adress of a local variable
        static _Thread_local char thread_name[16] = {0};
        if(thread_name[0] == 0)
            snprintf(thread_name, sizeof thread_name, "<%p>", thread_name);

        return thread_name;
    }

    EXTERNAL void log_callstack(Log_Type type, const char* module, int64_t skip)
    {
        (void) type;
        (void) module;
        (void) skip;
    }
    EXTERNAL void log_captured_callstack(Log_Type type, const char* module, void** callstack, int64_t callstack_size)
    {
        (void) type;
        (void) module;
        (void) callstack;
        (void) callstack_size;
    }
    
    EXTERNAL bool panic_default_handler_break_into_debugger(void* context)
    {
        (void) context;
        return false;        
    }
#endif

EXTERNAL void panic_default_handler_func(void* context, const char* type, const char* expression, const char* file, const char* function, int line, const char* format, va_list args)
{
    (void) type;
    (void) context;
    LOG_FATAL("panic", "%s in %s %s:%i\n", expression, function, file, line);
    if(format && format[0] != '\0') 
        LOGV(LOG_FATAL, ">panic", format, args);
    
    LOG_TRACE("panic", "printing execution callstack:");
    log_callstack(LOG_TRACE, ">panic", 2);
    log_flush(log_get_logger());
    abort();
}

#endif
