#ifndef JOT_ASSERT
#define JOT_ASSERT

#include "log.h"
#include <stdlib.h>

#undef TEST
#undef TEST_MSG
#undef ASSERT
#undef ASSERT_MSG

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


//If x evaluates to false executes assertion_report() without any message. 
#define TEST(x)                 TEST_MSG(x, "")              /* executes always (even in release) */
#define ASSERT(x)               ASSERT_MSG(x, "")            /* is enabled by DO_ASSERTS */
#define ASSERT_SLOW(x)          ASSERT_SLOW_MSG(x, "")       /* is enabled by DO_ASSERTS_SLOW */
#define CHECK_BOUNDS(i, to)     CHECK_RANGE_BOUNDS(i, 0, to) /* if i is not within [0, to) panics. is enabled by DO_BOUNDS_CHECKS*/
#define UNREACHABLE()           platform_assume_unreachable(), ASSERT_MSG(false, "unreachable code reached!")

//If x evaluates to false executes assertion_report() with the specified message. 
#define TEST_MSG(x, msg, ...)               (!(x) ? (assertion_report(#x, SOURCE_INFO(), (msg), ##__VA_ARGS__), (platform_debug_break()), abort()) : (void) 0)
#define ASSERT_MSG(x, msg, ...)             PP_IF(DO_ASSERTS,       TEST_MSG)(x, msg, ##__VA_ARGS__)
#define ASSERT_SLOW_MSG(x, msg, ...)        PP_IF(DO_ASSERTS_SLOW,  TEST_MSG)(x, msg, ##__VA_ARGS__)
#define CHECK_RANGE_BOUNDS(i, from, to)     PP_IF(DO_BOUNDS_CHECKS, TEST_MSG)((from) <= (i) && (i) < (to), \
                                                "Bounds check failed! %lli is not from the interval [%lli, %lli)!", \
                                                (long long) (i), (long long) (from), (long long) (to))

//Gets called when assertion fails. 
//Does not have to terminate process since that is done at call site by the assert macro itself.
//if ASSERT_CUSTOM_REPORT is defined is left unimplemented
void assertion_report(const char* expression, Source_Info source, const char* message, ...);

void default_assertion_report(const char* expression, Source_Info source, const char* message, va_list args);

//==================== IMPLEMENTATION =======================

    //Doesnt do anything (failed branch) but still properly expands x and msg so it can be type checked.
    //Dissabled asserts expand to this.
    #define DISSABLED_TEST_MSG(x, msg, ...)           (0 ? ((void) (x), assertion_report("", SOURCE_INFO(), (msg), ##__VA_ARGS__)) : (void) 0)

    //If dissabled expand to this
    #define _IF_NOT_DO_ASSERTS(ignore)         DISSABLED_TEST_MSG
    #define _IF_NOT_DO_ASSERTS_SLOW(ignore)    DISSABLED_TEST_MSG
    #define _IF_NOT_DO_BOUNDS_CHECKS(ignore)   DISSABLED_TEST_MSG


#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ASSERT_IMPL)) && !defined(JOT_ASSERT_HAS_IMPL)
#define JOT_ASSERT_HAS_IMPL

    #ifndef ASSERT_CUSTOM_REPORT
        void assertion_report(const char* expression, Source_Info source, const char* message, ...)
        {
            va_list args;               
            va_start(args, message);     
            default_assertion_report(expression, source, message, args);                    
            va_end(args);  
        }
    #endif

    void default_assertion_report(const char* expression, Source_Info source, const char* message, va_list args)
    {
        log_message("assert", LOG_FATAL, source, "TEST(%s) TEST/ASSERT failed! (%s : %lli) ", expression, source.file, source.line);
        if(message != NULL && strlen(message) != 0)
        {
            log_message(">assert", LOG_FATAL, source, "message:");
                vlog_message(">>assert", LOG_FATAL, source, message, args);
        }

        log_callstack(">assert", LOG_TRACE, -1, "callstack:");
    }

#endif