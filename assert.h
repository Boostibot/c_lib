#ifndef JOT_ASSERT
#define JOT_ASSERT

    #include <stdarg.h>
    #include <stdio.h>
    #include <stdbool.h>

    #if 0
    #define PANIC_EXPR(type, expr, ...)         //calls the panic handler with given panic type (for example "ASSERT"), expression string and format function
    #define PANIC(...)                          //calls the panic handler
    #define TEST(x, ...)                        //asserts x is true - does NOT get removed in release builds 
    #define ASSERT(x, ...)                      //asserts x is true - gets removed in release builds
    #define ASSERT_SLOW(x, ...)                 //asserts x is true - gets removed in release and optimized debug builds -
    #define ASSERT_BOUNDS(i, to)                //asserts i is in [0, to)
    #define ASSERT_BOUNDS_RANGE(i, from, to)    //asserts i is in [from, to)
    #define STATIC_ASSERT(x)                    //if x is not true compilation fails. x must be constant expression.
    #define TODO(...)                           //declares this code is imcomplete with TEST(FALSE). 
    #define UNREACHABLE(...)                    //asserts this code is unreachable with ASSERT(FALSE). Also adds optimalization hints
    #define DEBUG_BREAK()                       //If code is running under debugger, breaks at the call site as if with an explicit breakpoint
    #define ASSUME_UNREACHABLE(...)             //hints to the compiler this location in code is unreachable. Does not assert.
    #define CAST(T, x)                          //performs checked integer cast ensuring no information is lost.
    #endif

    #if !defined(ASSERT_CUSTOM_SETTINGS)
        #if !defined(NDEBUG)
            #define DO_ASSERTS       // enables assertions
            #define DO_ASSERTS_SLOW  // enables slow assertions - expensive assertions or once that change the time complexity of an algorithm
        #endif
        #define DO_BOUNDS_CHECKS // checks bounds prior to lookup 
    #endif

    //Pre-Processor (PP) utils
    #define _PP_STRINGIFY(X) #X
    #define _PP_CONCAT(a, b)        a ## b
    #define PP_STRINGIFY(X)         _PP_STRINGIFY(X)
    #define PP_CONCAT(a, b)         _PP_CONCAT(a, b)
    #define PP_UNIQ(a)              PP_CONCAT(a, __LINE__)


    #ifdef _MSC_VER
        #define ASSUME_UNREACHABLE()  __assume(0)
        #define ATTRIBUTE_NORETURN    __declspec(noreturn)
        #define ATTRIBUTE_INLINE_NEVER __declspec(noinline)
    #elif defined(__GNUC__) || defined(__clang__)
        #define ASSUME_UNREACHABLE()  __builtin_unreachable() 
        #define ATTRIBUTE_NORETURN    __attribute__((noreturn))
        #define ATTRIBUTE_INLINE_NEVER __attribute__((noinline))
    #else
        #define ASSUME_UNREACHABLE() (*(int*)0 = 0)
        #define ATTRIBUTE_NORETURN
        #define ATTRIBUTE_INLINE_NEVER
    #endif

    #ifndef EXTERNAL
        #define EXTERNAL
    #endif

    ATTRIBUTE_NORETURN ATTRIBUTE_INLINE_NEVER
    EXTERNAL void panic(const char* function, const char* joined, ...);

    ATTRIBUTE_NORETURN ATTRIBUTE_INLINE_NEVER
    EXTERNAL void vpanic(const char* function, const char* joined, va_list arg);
    EXTERNAL void panic_recovered(); //should get called after we recovered from a panic (ie. before longjump to safety)

    typedef struct Panic_Handler {
        void (*panic)(void* context, const char* type, const char* expression, const char* file, const char* function, int line, const char* format, va_list args);
        bool (*break_into_debugger)(void* context);
        void* context;
    } Panic_Handler;

    EXTERNAL Panic_Handler panic_get_handler();
    EXTERNAL Panic_Handler panic_set_handler(Panic_Handler handler);

    EXTERNAL Panic_Handler panic_get_default_handler();
    EXTERNAL void panic_default_handler_func(void* context, const char* type, const char* expression, const char* file, const char* function, int line, const char* format, va_list args);
    EXTERNAL bool panic_default_handler_break_into_debugger(void* context);


    //macro implementation below 
    //===========================================================
    #ifdef _MSC_VER
        #define DEBUG_BREAK() __debugbreak()
    #elif defined(__clang__)
        #define DEBUG_BREAK() __builtin_debugtrap()
    #elif defined(__GNUC__)
        #define DEBUG_BREAK() __builtin_trap()
    #else
        #include <signal.h>
        #define DEBUG_BREAK() raise(SIGTRAP)
    #endif

    #define STATIC_ASSERT(x)                typedef char PP_CONCAT(__static_assertion__, __LINE__)[(x) ? 1 : -1]

    #define PANIC_EXPR(type, expr, ...)     (panic(__func__, type "\0" expr "\0" __FILE__ "\0" PP_STRINGIFY(__LINE__) "\0" __VA_ARGS__), (void) sizeof printf(" " __VA_ARGS__))
    #define PANIC(...)                      PANIC_EXPR("PANIC", "PANIC("#__VA_ARGS__")", ##__VA_ARGS__)

    #define TEST(x, ...)                    (!(x) ? PANIC_EXPR("TEST", "TEST("#x")", ##__VA_ARGS__) : (void) 0)
    #define _DISSABLED_TEST(x, ...)         (void) sizeof(printf(" " __VA_ARGS__), (x))

    #ifdef DO_ASSERTS
        #define ASSERT(x, ...)              (!(x) ? PANIC_EXPR("ASSERT", "ASSERT("#x")", ##__VA_ARGS__) : (void) 0) 
    #else
        #define ASSERT(x, ...)              _DISSABLED_TEST(x, ##__VA_ARGS__)
    #endif

    #ifdef DO_ASSERTS_SLOW
        #define ASSERT_SLOW(x, ...)          (!(x) ? PANIC_EXPR("ASSERT", "ASSERT_SLOW("#x")", ##__VA_ARGS__) : (void) 0)        
    #else
        #define ASSERT_SLOW(x, ...)          _DISSABLED_TEST(x, ##__VA_ARGS__)
    #endif

    #ifdef DO_ASSERTS
        #define UNREACHABLE(...)            (PANIC_EXPR("UNREACHABLE", "UNREACHABLE("#__VA_ARGS__")", ##__VA_ARGS__), ASSUME_UNREACHABLE())
    #else
        #define UNREACHABLE(...)            (_DISSABLED_TEST(0, ##__VA_ARGS__), ASSUME_UNREACHABLE())
    #endif

    #define TODO(...)                       PANIC_EXPR("UNFINISHED", "TODO("#__VA_ARGS__")", ##__VA_ARGS__)

    #ifdef DO_BOUNDS_CHECKS
        #define ASSERT_BOUNDS_RANGE(i, from, to) \
            ((from) <= (i) && (i) < (to) \
                ? (void) 0 \
                : PANIC_EXPR("BOUNDS", "ASSERT_BOUNDS_RANGE("#i", "#from","#to")", \
                    "Bounds check failed! %lli is not from the interval [%lli, %lli)!", \
                    (long long) (i), (long long) (from), (long long) (to)))      
                    
        #define ASSERT_BOUNDS(i, to) \
            ((uint64_t) (i) < (uint64_t) (to) \
                ? (void) 0 \
                : PANIC_EXPR("BOUNDS", "ASSERT_BOUNDS("#i","#to")", \
                    "Bounds check failed! %lli is not from the interval [0, %lli)!", \
                    (long long) (i), (long long) (to)))           
    #else
        #define ASSERT_BOUNDS_RANGE(i, from, to)  ((void) sizeof((from) <= (i) && (i) < (to)))
        #define ASSERT_BOUNDS(i, to)              ((void) sizeof((0) <= (i) && (i) < (to)))
    #endif

    #ifdef DO_ASSERTS
        #define CAST(T, value) \
            (((__typeof__(value)) (T) (value) != (value) || ((value) > 0) != ((T) (value) > 0)) \
                ? PANIC_EXPR("OVERFLOW", "ASSERT_BOUNDS("#T","#value")", \
                    "Cast failed! %lli does not fith into type " #T, (long long) value) \
                : (void) 0, (T) (value))
    #else
        #define CAST(T, value) ((T) (value))
    #endif

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ASSERT_IMPL)) && !defined(JOT_ASSERT_HAS_IMPL)
#define JOT_ASSERT_HAS_IMPL

static _Thread_local Panic_Handler _thread_panic_handler = {panic_default_handler_func, panic_default_handler_break_into_debugger};
static _Thread_local int _thread_panic_depth = 0;

EXTERNAL Panic_Handler panic_get_default_handler()
{
    Panic_Handler out = {panic_default_handler_func, panic_default_handler_break_into_debugger};
    return out;
}

EXTERNAL Panic_Handler panic_get_handler()
{
    return _thread_panic_handler;
}

EXTERNAL Panic_Handler panic_set_handler(Panic_Handler handler)
{
    Panic_Handler before = _thread_panic_handler;
    _thread_panic_handler = handler;
    return before;
}

ATTRIBUTE_NORETURN ATTRIBUTE_INLINE_NEVER
EXTERNAL void panic(const char* function, const char* joined, ...)
{
    Panic_Handler handler = panic_get_handler();
    if(handler.break_into_debugger(handler.context))
        DEBUG_BREAK();

    va_list args;               
    va_start(args, joined);   
    vpanic(function, joined, args);
    va_end(args);  
}

#include <stdlib.h>
#include <string.h>
ATTRIBUTE_NORETURN ATTRIBUTE_INLINE_NEVER
EXTERNAL void vpanic(const char* function, const char* joined, va_list args)
{
    const char* type = joined;
    const char* expr = type + strlen(type) + 1;
    const char* file = expr + strlen(expr) + 1;
    const char* line_str = file + strlen(file) + 1;
    const char* format = line_str + strlen(line_str) + 1;
    int line = atoi(line_str);

    Panic_Handler handler = panic_get_handler();
    if(_thread_panic_depth > 10) {
        printf("%i unrecovered panics pending, aborting...", _thread_panic_depth);
    }
    else {
        _thread_panic_depth += 1;
        handler.panic(handler.context, type, expr, file, function, line, format, args);
    }
    abort();
}

EXTERNAL void panic_recovered()
{
    _thread_panic_depth -= 1;
}

#endif
