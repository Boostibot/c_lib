#ifndef JOT_LOG
#define JOT_LOG

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "defines.h"

typedef struct Log {
    void (*log)(void* context, int indent, int custom, int is_flush, const char* name, const char* format, va_list args);
    void* context;
    const char* name;
    int indent;
    int custom;
} Log;

typedef struct Log_Set {
    Log trace;
    Log debug;
    Log okay;
    Log info;
    Log warn;
    Log error;
    Log fatal;
    
    int indent;
    int _;
} Log_Set;

EXTERNAL Log_Set* get_log_set();
EXTERNAL Log_Set set_log_set(Log_Set new_set);

EXTERNAL Log log_trace(const char* name);
EXTERNAL Log log_debug(const char* name);
EXTERNAL Log log_okay(const char* name);
EXTERNAL Log log_info(const char* name);
EXTERNAL Log log_warn(const char* name);
EXTERNAL Log log_error(const char* name);
EXTERNAL Log log_fatal(const char* name);
EXTERNAL Log log_none();
EXTERNAL Log log_indented(Log);

EXTERNAL void log_flush(Log log);
EXTERNAL void log_flush_all();
EXTERNAL void log_indent();
EXTERNAL void log_outdent();
EXTERNAL void _log_local_call(Log stream, const char* format, ...);
EXTERNAL void _log_global_call(Log stream, const char* log_module, const char* format, ...);

#define LOG(stream, ...) \
    ((stream).log \
        ? _log_local_call((stream), "" __VA_ARGS__) \
        : (void) sizeof printf("" __VA_ARGS__)) \

#define GLOBAL_LOG(stream, name, ...) \
    ((stream).log \
        ? _log_global_call((stream), (name), "" __VA_ARGS__) \
        : (void) sizeof printf("" __VA_ARGS__)) \

#define LOG_TRACE(name, ...) GLOBAL_LOG(get_log_set()->trace, (name), ##__VA_ARGS__)
#define LOG_DEBUG(name, ...) GLOBAL_LOG(get_log_set()->debug, (name), ##__VA_ARGS__)
#define LOG_OKAY(name, ...)  GLOBAL_LOG(get_log_set()->okay,  (name), ##__VA_ARGS__)
#define LOG_INFO(name, ...)  GLOBAL_LOG(get_log_set()->info,  (name), ##__VA_ARGS__)
#define LOG_WARN(name, ...)  GLOBAL_LOG(get_log_set()->warn,  (name), ##__VA_ARGS__)
#define LOG_ERROR(name, ...) GLOBAL_LOG(get_log_set()->error, (name), ##__VA_ARGS__)
#define LOG_FATAL(name, ...) GLOBAL_LOG(get_log_set()->fatal, (name), ##__VA_ARGS__)

#define LOG_HERE             LOG_TRACE("HERE", "HERE %15s() %25s:%i", __func__, __FILE__, __LINE__);

#define STRING_PRINT(str) (int) (str).len, (str).data

typedef struct String_Buffer_16 {
    char data[16];
} String_Buffer_16;

typedef struct String_Buffer_64 {
    char data[64];
} String_Buffer_64;

EXTERNAL String_Buffer_16 format_ptr(void* ptr); //returns "0x00000ff76344ae64"
EXTERNAL String_Buffer_16 format_bytes(int64_t bytes, int width); //returns "39B" "64KB", "10.3MB", "5.3GB", "7.531TB" etc.
EXTERNAL String_Buffer_16 format_seconds(double seconds, int width); //returns "153ns", "10μs", "6.3ms", "15.2s". But doesnt go to hours, days etc.
EXTERNAL String_Buffer_16 format_nanoseconds(int64_t ns, int width); //returns "153ns", "10μs", "6.3ms", "15.2s". But doesnt go to hours, days etc.

EXTERNAL void log_captured_callstack(Log stream, void** callstack, isize callstack_size);
EXTERNAL void log_callstack_no_check(Log stream, isize skip, const char* format, ...);
#define log_callstack(stream, skip, format, ...) (sizeof printf((format), ##__VA_ARGS__), log_callstack_no_check((stream), (skip), (format), ##__VA_ARGS__))
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOG_IMPL)) && !defined(JOT_LOG_HAS_IMPL)
#define JOT_LOG_HAS_IMPL
    static ATTRIBUTE_THREAD_LOCAL Log_Set global_log_set = {0};

    EXTERNAL Log_Set* get_log_set() 
    {
        return &global_log_set; 
    }
    EXTERNAL Log_Set set_log_set(Log_Set new_set)
    {
        Log_Set old_set = global_log_set;
        global_log_set = new_set;
        return old_set; 
    }
    
    EXTERNAL void log_flush(Log log)
    {
        if(log.log)
        {
            va_list list = {0};
            log.log(log.context, log.indent, log.custom, 1, log.name, "", list);
        }
    }

    EXTERNAL void log_flush_all()
    {
        log_flush(get_log_set()->trace);
        log_flush(get_log_set()->debug);
        log_flush(get_log_set()->okay);
        log_flush(get_log_set()->info);
        log_flush(get_log_set()->warn);
        log_flush(get_log_set()->error);
        log_flush(get_log_set()->fatal);
    }

    EXTERNAL void vlog_local_call(Log stream, const char* format, va_list args)
    {
        if(stream.log == NULL)
            return;

        stream.log(stream.context, stream.indent, stream.custom, 0, stream.name, format, args);
    }

    EXTERNAL void _log_local_call(Log stream, const char* format, ...)
    {
        if(stream.log == NULL)
            return;

        va_list args;               
        va_start(args, format);    
        stream.log(stream.context, stream.indent, stream.custom, 0, stream.name, format, args);
        va_end(args);  
    }

    EXTERNAL void _log_global_call(Log stream, const char* name, const char* format, ...)
    {
        if(stream.log == NULL)
            return;

        int indent = 0;
        for(; name[indent] == '>'; indent++);

        va_list args;               
        va_start(args, format);    
        stream.log(stream.context, get_log_set()->indent + indent, stream.custom, 0, name + indent, format, args);
        va_end(args);  
    }
    
    EXTERNAL Log _log_from_log_set(Log model_after, const char* name)
    {
        int indent = 0;
        for(; name[indent] == '>'; indent++);

        Log out = model_after;
        out.indent = get_log_set()->indent + indent;
        out.name = name + indent;
        return out;
    }

    EXTERNAL Log log_trace(const char* name) { return _log_from_log_set(global_log_set.trace, name); }
    EXTERNAL Log log_debug(const char* name) { return _log_from_log_set(global_log_set.debug, name); }
    EXTERNAL Log log_okay(const char* name)  { return _log_from_log_set(global_log_set.okay, name); }
    EXTERNAL Log log_info(const char* name)  { return _log_from_log_set(global_log_set.info, name); }
    EXTERNAL Log log_warn(const char* name)  { return _log_from_log_set(global_log_set.warn, name); }
    EXTERNAL Log log_error(const char* name) { return _log_from_log_set(global_log_set.error, name); }
    EXTERNAL Log log_fatal(const char* name) { return _log_from_log_set(global_log_set.fatal, name); }
    
    EXTERNAL Log log_none()
    {
        Log out = {0};
        return out;
    }

    EXTERNAL Log log_indented(Log log)
    {
        log.indent += 1;
        return log;
    }

    EXTERNAL void log_indent()
    {
        get_log_set()->indent += 1;
    }

    EXTERNAL void log_outdent()
    {
        get_log_set()->indent -= 1;
    }
    
    EXTERNAL String_Buffer_16 format_ptr(void* ptr)
    {
        String_Buffer_16 out = {0};
        snprintf(out.data, sizeof out.data, "0x%08llx", (long long) ptr);
        return out;
    }

    EXTERNAL String_Buffer_16 format_bytes(int64_t bytes, int width)
    {
        int64_t abs = bytes > 0 ? bytes : -bytes;
        String_Buffer_16 out = {0};
        if(abs >= TB)
            snprintf(out.data, sizeof out.data, "%*.3lfTB", width, (double) bytes / (double) TB);
        else if(abs >= GB)
            snprintf(out.data, sizeof out.data, "%*.2lfGB", width, (double) bytes / (double) GB);
        else if(abs >= MB)
            snprintf(out.data, sizeof out.data, "%*.2lfMB", width, (double) bytes / (double) MB);
        else if(abs >= KB)
            snprintf(out.data, sizeof out.data, "%*.1lfKB", width, (double) bytes / (double) KB);
        else
            snprintf(out.data, sizeof out.data, "%*lliB", width+1, (long long) bytes);

        return out;
    }

    EXTERNAL String_Buffer_16 format_nanoseconds(int64_t ns, int width)
    {
        int64_t sec = (int64_t) 1000*1000*1000;
        int64_t milli = (int64_t) 1000*1000;
        int64_t micro = (int64_t) 1000;

        int64_t abs = ns > 0 ? ns : -ns;
        String_Buffer_16 out = {0};
        if(abs >= sec)
            snprintf(out.data, sizeof out.data, "%*.2lfs", width+1, (double) ns / (double) sec);
        else if(abs >= milli)
            snprintf(out.data, sizeof out.data, "%*.2lfms", width, (double) ns / (double) milli);
        else if(abs >= micro)
            snprintf(out.data, sizeof out.data, "%*lliμs", width, (long long) (ns / micro));
        else
            snprintf(out.data, sizeof out.data, "%*llins", width, (long long) ns);

        return out;
    }

    EXTERNAL String_Buffer_16 format_seconds(double seconds, int width)
    {
        return format_nanoseconds((int64_t) (seconds * 1000*1000*1000), width);
    }
    
    EXTERNAL void log_callstack_no_check(Log stream, isize skip, const char* format, ...)
    {
        bool has_msg = format != NULL && strlen(format) != 0;
        Log inner = stream;
        if(has_msg)
        {
            inner = log_indented(stream);
            
            va_list args;               
            va_start(args, format);     
            vlog_local_call(stream, format, args);
            va_end(args);   
        }
    
        void* stack[256] = {0};
        isize size = platform_capture_call_stack(stack, 256, skip + 1);
        log_captured_callstack(inner, stack, size);
    }

    EXTERNAL void log_captured_callstack(Log stream, void** callstack, isize callstack_size)
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
                LOG(stream, "%-30s %s:%i", entry->function, entry->file, (int) entry->line);
                if(strcmp(entry->function, "main") == 0) 
                {
                    i = callstack_size;
                    break;
                }
            }
        }
    }
    
    #ifndef ASSERT_CUSTOM_REPORT
        EXTERNAL void assertion_report(const char* expression, int line, const char* file, const char* function, const char* format, ...)
        {
            if(0)
            {
                LOG_FATAL("assert", "TEST(%s) TEST/ASSERT failed! %s() %s:%i", function, expression, file, line);
                if(format != NULL && strlen(format) > 1)
                {
                    va_list args;               
                    va_start(args, format);     
                    vlog_local_call(log_fatal(">assert"), format + 1, args);
                    va_end(args);  
                }

                log_callstack(log_trace(">assert"), -1, "");
                log_flush_all();
            }
        }
    #endif
    
    EXTERNAL Allocator_Stats log_allocator_stats(Log log, Allocator* allocator)
    {
        Allocator_Stats stats = {0};
        if(allocator != NULL && allocator->func != NULL)
        {
            stats = allocator_get_stats(allocator);
            if(stats.type_name == NULL)
                stats.type_name = "<no log_type name>";

            if(stats.name == NULL)
                stats.name = "<no name>";

            LOG(log, "type_name:           %s", stats.type_name);
            LOG(log, "name:                %s", stats.name);

            LOG(log, "bytes_allocated:     %s", format_bytes(stats.bytes_allocated, 0).data);
            LOG(log, "max_bytes_allocated: %s", format_bytes(stats.max_bytes_allocated, 0).data);

            LOG(log, "allocation_count:    %lli", stats.allocation_count);
            LOG(log, "deallocation_count:  %lli", stats.deallocation_count);
            LOG(log, "reallocation_count:  %lli", stats.reallocation_count);
        }
        else
            LOG(log, "Allocator NULL or missing get_stats callback.");

        return stats;
    }

    #ifndef ALLOCATOR_CUSTOM_OUT_OF_MEMORY
        EXTERNAL void allocator_panic(Allocator_Error error)
        {
            if(0)
            {
                Allocator_Stats stats = {0};
                if(error.alloc != NULL && error.alloc->func != NULL)
                    stats = allocator_get_stats(error.alloc);
        
                if(stats.type_name == NULL)
                    stats.type_name = "<no type name>";

                if(stats.name == NULL)
                    stats.name = "<no name>";

                LOG_FATAL("memory", "Allocator %s of type %s reported out of memory! Message: '%s'", stats.type_name, stats.name, error.message);

                LOG_INFO(">memory", "new_size:    %s", format_bytes(error.new_size, 0).data);
                LOG_INFO(">memory", "old_size:    %s", format_bytes(error.old_size, 0).data);
                LOG_INFO(">memory", "old_ptr:     %s", format_ptr(error.old_ptr).data);
                LOG_INFO(">memory", "align:       %lli", (lli) error.align);

                LOG_INFO(">memory", "Allocator_Stats:");
                LOG_INFO(">>memory", "bytes_allocated:     %s", format_bytes(stats.bytes_allocated, 0).data);
                LOG_INFO(">>memory", "max_bytes_allocated: %s", format_bytes(stats.max_bytes_allocated, 0).data);

                LOG_INFO(">>memory", "allocation_count:    %lli", (lli) stats.allocation_count);
                LOG_INFO(">>memory", "deallocation_count:  %lli", (lli) stats.deallocation_count);
                LOG_INFO(">>memory", "reallocation_count:  %lli", (lli) stats.reallocation_count);
    
                log_callstack(log_info(">memory"), 1, "callstack:");

                log_flush_all();
            }
            abort();
        }
    #endif

#endif