#ifndef JOT_PANIC
#define JOT_PANIC

    #include "platform.h"
    #include "log.h"
    #include "assert.h"

    //invokes the panic handler with potential format parameter: PANIC() or PANIC("%i", 10)
    #define PANIC(...)             PANIC_EXPR("PANIC("#__VA_ARGS__")", ##__VA_ARGS__)
    #define PANIC_EXPR(expr, ...)  (panic(expr, __FILE__, __FUNCTION__, __LINE__, "" __VA_ARGS__), (void) sizeof printf(" " __VA_ARGS__))

    EXTERNAL void panic(const char* expression, const char* file, const char* function, int line, const char* format, ...);
    EXTERNAL void vpanic(const char* expression, const char* file, const char* function, int line, const char* format, va_list args);
    INTERNAL void assert_panic(const char* expression, const char* file, const char* function, int line, const char* format, ...);
    EXTERNAL void panic_recovered(); //should get called after we recovered from a panic (ie. before longjump to safety)

    EXTERNAL void log_callstack(Log_Type type, const char* module, int64_t skip);
    EXTERNAL void log_captured_callstack(Log_Type type, const char* module, void** callstack, int64_t callstack_size);

    typedef struct Panic_Handler {
        void (*panic)(struct Panic_Handler* self, const char* expression, const char* file, const char* function, int line, const char* format, va_list args);
    } Panic_Handler;

    EXTERNAL Panic_Handler* panic_get_handler();
    EXTERNAL Panic_Handler* panic_set_handler(Panic_Handler* handler);

    EXTERNAL Panic_Handler* panic_get_default_handler();
    EXTERNAL void panic_default_handler_func(Panic_Handler* self, const char* expression, const char* file, const char* function, int line, const char* format, va_list args);
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PANIC_IMPL)) && !defined(JOT_PANIC_HAS_IMPL)
#define JOT_PANIC_HAS_IMPL

static Panic_Handler _panic_default_handler = {panic_default_handler_func};
static _Thread_local Panic_Handler* _thread_panic_handler = &_panic_default_handler;
static _Thread_local int32_t _thread_panic_depth = 0;

EXTERNAL Panic_Handler* panic_get_default_handler()
{
    return &_panic_default_handler;
}

EXTERNAL Panic_Handler* panic_get_handler()
{
    return _thread_panic_handler;
}

EXTERNAL Panic_Handler* panic_set_handler(Panic_Handler* handler)
{
    Panic_Handler* before = _thread_panic_handler;
    _thread_panic_handler = handler;
    return before;
}

INTERNAL void assert_panic(const char* expression, const char* file, const char* function, int line, const char* format, ...)
{
    va_list args;               
    va_start(args, format);   
    vpanic(expression, file, function, line, format, args);
    va_end(args);  
}

EXTERNAL void panic(const char* expression, const char* file, const char* function, int line, const char* format, ...)
{
    va_list args;               
    va_start(args, format);   
    vpanic(expression, file, function, line, format, args);
    va_end(args);  
}

EXTERNAL void vpanic(const char* expression, const char* file, const char* function, int line, const char* format, va_list args)
{
    if(_thread_panic_depth > 10) {
        printf("%i unrecovered panics pending, aborting...", _thread_panic_depth);
        abort();
    }
    else {
        _thread_panic_depth += 1;
        Panic_Handler* handler = panic_get_handler();
        handler->panic(handler, expression, file, function, line, format, args);
    }
}

EXTERNAL void panic_recovered()
{
    _thread_panic_depth -= 1;
}

EXTERNAL void panic_default_handler_func(Panic_Handler* self, const char* expression, const char* file, const char* function, int line, const char* format, va_list args)
{
    (void) self;
    LOG_FATAL("panic", "%s in %s %s:%i\n", expression, function, file, line);
    if(format && format[0] != '\0') 
        LOGV(LOG_FATAL, ">panic", format, args);
    
    LOG_TRACE("panic", "printing execution callstack:");
    log_callstack(LOG_TRACE, ">panic", 2);
    log_flush(log_get_logger());
    abort();
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
        ASSERT(remaining > 0);

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

#endif
