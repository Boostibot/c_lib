#ifndef JOT_ASSERT
#define JOT_ASSERT

#include "platform.h"
#include <stdlib.h>

#if !defined(ASSERT_CUSTOM_SETTINGS) && !defined(NDEBUG)
    //Locally enables/disables asserts. If we wish to disable for part of
    // code we simply undefine them then redefine them after.
    #define DO_ASSERTS       /* enables assertions */
    #define DO_ASSERTS_SLOW  /* enables slow assertions - expensive assertions or once that change the time complexity of an algorhitm */
    #define DO_BOUNDS_CHECKS /* checks bounds prior to lookup */
#endif

//If fails does not compile. 
//x must be a valid compile time exception. 
//Is useful for validating if compile time settings are correct
#define STATIC_ASSERT(x) typedef char PP_CONCAT(__static_assertion__, __LINE__)[(x) ? 1 : -1]

//If x evaluates to false executes assertion_report() with optional provided message
#define TEST(x, ...)                            (!(x) ? (assertion_report(#x, __LINE__, __FILE__, __FUNCTION__, "" __VA_ARGS__), abort()) : (void) 0)
#define _DISSABLED_TEST(x, ...)                 (0 ? ((x), assertion_report(#x, __LINE__, __FILE__, __FUNCTION__, "" __VA_ARGS__), abort()) : (void) 0)

//In debug builds do the same as TEST() else do nothing
#ifdef DO_ASSERTS
    #define ASSERT(x, ...)              TEST(x, __VA_ARGS__)          
#else
    #define ASSERT(x, ...)              _DISSABLED_TEST(x, __VA_ARGS__)
#endif

//In slow debug builds do the same as TEST() else do nothing
#ifdef DO_ASSERTS_SLOW
    #define ASSERT_SLOW(x, ...)          TEST(x, __VA_ARGS__)          
#else
    #define ASSERT_SLOW(x, ...)          _DISSABLED_TEST(x, __VA_ARGS__)
#endif

//In debug builds checks wheter the value falls into the valid range
#ifdef DO_BOUNDS_CHECKS
    #define CHECK_RANGE_BOUNDS(i, from, to)  TEST((from) <= (i) && (i) < (to), \
                                                "Bounds check failed! %lli is not from the interval [%lli, %lli)!", \
                                                (long long) (i), (long long) (from), (long long) (to))          
#else
    #define CHECK_RANGE_BOUNDS(i, from, to)  _DISSABLED_TEST((from) <= (i) && (i) < (to))
#endif

#define CHECK_BOUNDS(i, to)         CHECK_RANGE_BOUNDS(i, 0, to)
#define UNREACHABLE(...)            (ASSERT(false, "Unreachable code reached! " __VA_ARGS__), platform_assume_unreachable())

//Gets called when assertion fails. 
//Does not have to terminate process since that is done at call site by the assert macro itself.
//if ASSERT_CUSTOM_REPORT is defined is left unimplemented
EXPORT ATTRIBUTE_FORMAT_FUNC(format, 5) void assertion_report(const char* expression, int line, const char* file, const char* function, ATTRIBUTE_FORMAT_ARG const char* format, ...);

//Pre-Processor (PP) utils
#define _PP_CONCAT(a, b)        a ## b
#define PP_CONCAT(a, b)         _PP_CONCAT(a, b)
#endif