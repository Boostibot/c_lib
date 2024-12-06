#ifndef JOT_ASSERT
#define JOT_ASSERT

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

#if !defined(ASSERT_CUSTOM_SETTINGS) && !defined(NDEBUG)
    #define DO_ASSERTS       // enables assertions
    #define DO_ASSERTS_SLOW  // enables slow assertions - expensive assertions or once that change the time complexity of an algorithm
    #define DO_BOUNDS_CHECKS // checks bounds prior to lookup 
#endif

#ifndef INTERNAL 
    #define INTERNAL static
#endif

#include <stdarg.h>
#include <stdio.h>
//gets called on assert/test failure. Does not get implemented unless JOT_ASSERT_PANIC_IMPL is defined
INTERNAL void assert_panic(const char* expression, const char* file, const char* function, int line, const char* format, ...);

//macro implementation below 
//===========================================================
#undef TEST
#undef ASSERT
#undef ASSERT_SLOW
#undef ASSERT_BOUNDS
#undef ASSERT_BOUNDS_RANGE
#undef STATIC_ASSERT
#undef TODO
#undef UNREACHABLE
#undef DEBUG_BREAK
#undef ASSUME_UNREACHABLE

#ifdef _MSC_VER
    #define DEBUG_BREAK() __debugbreak()
#else
    #include <signal.h>
    #define DEBUG_BREAK() raise(SIGTRAP)
#endif

#ifdef _MSC_VER
    #define ASSUME_UNREACHABLE()  __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
    #define ASSUME_UNREACHABLE()  __builtin_unreachable() 
#else
    #define ASSUME_UNREACHABLE() (*(int*)0 = 0)
#endif

#define _ASSERT_PANIC_EXPR(expr, ...)  (assert_panic(expr, __FILE__, __FUNCTION__, __LINE__, "" __VA_ARGS__), (void) sizeof printf(" " __VA_ARGS__))

#define TEST(x, ...)                    (!(x) ? _ASSERT_PANIC_EXPR("TEST("#x")") : (void) 0)
#define _DISSABLED_TEST(x, ...)         (void) sizeof(printf(" " __VA_ARGS__), (x))

#ifdef DO_ASSERTS
    #define ASSERT(x, ...)              (!(x) ? _ASSERT_PANIC_EXPR("ASSERT("#x")") : (void) 0) 
#else
    #define ASSERT(x, ...)              _DISSABLED_TEST(x, ##__VA_ARGS__)
#endif

#ifdef DO_ASSERTS_SLOW
    #define ASSERT_SLOW(x, ...)          (!(x) ? _ASSERT_PANIC_EXPR("ASSERT_SLOW("#x")") : (void) 0)        
#else
    #define ASSERT_SLOW(x, ...)          _DISSABLED_TEST(x, ##__VA_ARGS__)
#endif

#define STATIC_ASSERT(x) typedef char PP_CONCAT(__static_assertion__, __LINE__)[(x) ? 1 : -1]

#ifdef DO_BOUNDS_CHECKS
    #define ASSERT_BOUNDS_RANGE(i, from, to) \
        ((from) <= (i) && (i) < (to) \
            ? (void) 0 \
            : _ASSERT_PANIC_EXPR("ASSERT_BOUNDS_RANGE("#i", "#from","#to")", \
                "Bounds check failed! %lli is not from the interval [%lli, %lli)!", \
                (long long) (i), (long long) (from), (long long) (to)))          
#else
    #define ASSERT_BOUNDS_RANGE(i, from, to)  ((void) sizeof((from) <= (i) && (i) < (to)))
#endif

#define ASSERT_BOUNDS(i, to)        ASSERT_BOUNDS_RANGE(i, 0, to)

#ifdef DO_ASSERTS
    #define UNREACHABLE(...)            (_ASSERT_PANIC_EXPR("UNREACHABLE("#__VA_ARGS__")", ##__VA_ARGS__), ASSUME_UNREACHABLE())
#else
    #define UNREACHABLE(...)            (_DISSABLED_TEST(0, ##__VA_ARGS__), ASSUME_UNREACHABLE())
#endif

#define TODO(...)                   _ASSERT_PANIC_EXPR("TODO("#__VA_ARGS__")", ##__VA_ARGS__)

//Pre-Processor (PP) utils
#define _PP_CONCAT(a, b)        a ## b
#define PP_CONCAT(a, b)         _PP_CONCAT(a, b)
#define PP_UNIQ(a)              PP_CONCAT(a, __LINE__)

#ifdef JOT_ASSERT_PANIC_IMPL
    #include <stdio.h>
    #include <stdlib.h>
    INTERNAL void assert_panic(const char* expression, const char* file, const char* function, int line, const char* format, ...)
    {
        printf("%s in %s %s:%i\n", expression, function, file, line);
        //if user format non empty non empty
        if(format && format[0] != '\0') { 
            printf("user message: ");
            va_list args;               
            va_start(args, format);   
            vprintf(format, args);
            va_end(args);  
        }
        abort();
    }
#endif

#endif
