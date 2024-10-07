#ifndef JOT_ASSERT
#define JOT_ASSERT

//Declaration of convenient and easily debugable asserts. This file is self contained.
//Provides TEST(x) simply checks always, ASSERT and ASSERT_SLOW which get expanded only in debug builds.
// All of which can be used as regular 
//      ASSERT(val > 0) 
// but also support 
//      ASSERT(val > 0, "Val: %i needs to be greater than 5", val) 
// All arguments are fully typechecked. 

#include <stdlib.h>
#include <stdio.h>
#if !defined(JOT_INLINE_ASSERT) && !defined(JOT_DEFINES) && !defined(JOT_COUPLED)
    #define JOT_INLINE_ASSERT
    #include <string.h>
    #include <stdarg.h>

    #ifndef ASSUME_UNREACHABLE
        #define ASSUME_UNREACHABLE() (*(int*)0 = 0)
    #endif

    #ifndef EXTERNAL 
        #define EXTERNAL
    #endif
    EXTERNAL void assertion_report(const char* expression, int line, const char* file, const char* function, const char* format, ...)
    {
        printf("TEST(%s) or ASSERT failed in %s %s:%i\n", expression, function, file, line);
        if(strlen(format) > 1)
        {
            va_list args;               
            va_start(args, format);   
            vprintf(format, args);
            va_end(args);  
        }
    }
#else
    #include "defines.h"
#endif


#if !defined(ASSERT_CUSTOM_SETTINGS) && !defined(NDEBUG)
    //Locally enables/disables asserts. If we wish to disable for part of
    // code we simply undefine them then redefine them after.
    #define DO_ASSERTS       /* enables assertions */
    #define DO_ASSERTS_SLOW  /* enables slow assertions - expensive assertions or once that change the time complexity of an algorithm */
    #define DO_BOUNDS_CHECKS /* checks bounds prior to lookup */
#endif

//#undef DO_ASSERTS
//#undef DO_ASSERTS_SLOW

//If fails does not compile. 
//x must be a valid compile time exception. 
//Is useful for validating if compile time settings are correct
#define STATIC_ASSERT(x) typedef char PP_CONCAT(__static_assertion__, __LINE__)[(x) ? 1 : -1]

//If x evaluates to false executes assertion_report() with optional provided message
#define TEST(x, ...)                            (!(x) ? (assertion_report(#x, __LINE__, __FILE__, __FUNCTION__, " " __VA_ARGS__), sizeof printf(" " __VA_ARGS__), abort(), 0) : 1)
#define _DISSABLED_TEST(x, ...)                 sizeof(printf(" " __VA_ARGS__), (x))

//In debug builds do the same as TEST() else do nothing
#ifdef DO_ASSERTS
    #define ASSERT(x, ...)              TEST(x, __VA_ARGS__)          
#else
    #define ASSERT(x, ...)              _DISSABLED_TEST(x, ##__VA_ARGS__)
#endif

//In slow debug builds do the same as TEST() else do nothing
#ifdef DO_ASSERTS_SLOW
    #define ASSERT_SLOW(x, ...)          TEST(x, __VA_ARGS__)          
#else
    #define ASSERT_SLOW(x, ...)          _DISSABLED_TEST(x, ##__VA_ARGS__)
#endif

//In debug builds checks whether the value falls into the valid range
#ifdef DO_BOUNDS_CHECKS
    #define CHECK_RANGE_BOUNDS(i, from, to)  TEST((from) <= (i) && (i) < (to), \
                                                "Bounds check failed! %lli is not from the interval [%lli, %lli)!", \
                                                (long long) (i), (long long) (from), (long long) (to))          
#else
    #define CHECK_RANGE_BOUNDS(i, from, to)  _DISSABLED_TEST((from) <= (i) && (i) < (to))
#endif

#define CHECK_BOUNDS(i, to)         CHECK_RANGE_BOUNDS(i, 0, to)
#define UNREACHABLE(...)            (ASSERT(false, "Unreachable code reached! " __VA_ARGS__), ASSUME_UNREACHABLE())
#define TODO(...)                   TEST(false, "TODO: " __VA_ARGS__)

//Gets called when assertion fails. 
//Does not have to terminate process since that is done at call site by the assert macro itself.
//if ASSERT_CUSTOM_REPORT is defined is left unimplemented
EXTERNAL void assertion_report(const char* expression, int line, const char* file, const char* function, const char* format, ...);

//Pre-Processor (PP) utils
#define _PP_CONCAT(a, b)        a ## b
#define PP_CONCAT(a, b)         _PP_CONCAT(a, b)
#define PP_UNIQ(a)              PP_CONCAT(a, __LINE__)
#endif