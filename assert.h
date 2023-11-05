#ifndef LIB_ASSERT
#define LIB_ASSERT

#include "platform.h"
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
#define STATIC_ASSERT(x) typedef char PP_CONCAT(__static_assertion_, __LINE__)[(x) ? 1 : -1]

//If x evaluates to false executes assertion_report() without any message. 
#define TEST(x)                 TEST_MSG(x, "")              /* executes always (even in release) */
#define ASSERT(x)               ASSERT_MSG(x, "")            /* is enabled by DO_ASSERTS */
#define ASSERT_SLOW(x)          ASSERT_SLOW_MSG(x, "")       /* is enabled by DO_ASSERTS_SLOW */
#define CHECK_BOUNDS(i, to)     CHECK_RANGE_BOUNDS(i, 0, to) /* if i is not within [0, to) panics. is enabled by DO_BOUNDS_CHECKS*/

//If x evaluates to false executes assertion_report() with the specified message. 
#define TEST_MSG(x, msg, ...)               (assertion_report && !(x) ? (platform_trap(), assertion_report(#x, SOURCE_INFO(), (msg), ##__VA_ARGS__)) : (void) 0)
#define ASSERT_MSG(x, msg, ...)             PP_IF(DO_ASSERTS,       TEST_MSG)(x, msg, __VA_ARGS__)
#define ASSERT_SLOW_MSG(x, msg, ...)        PP_IF(DO_ASSERTS_SLOW,  TEST_MSG)(x, msg, __VA_ARGS__)
#define CHECK_RANGE_BOUNDS(i, from, to)     PP_IF(DO_BOUNDS_CHECKS, TEST_MSG)((from) <= (i) && (i) < (to), \
                                                "Bounds check failed! %lli is not from the interval [%lli, %lli)!", \
                                                (long long) (i), (long long) (from), (long long) (to))

typedef struct Source_Info {
    int64_t line;
    const char* file;
    const char* function;
} Source_Info;

#ifdef __cplusplus
    #define SOURCE_INFO() Source_Info{__LINE__, __FILE__, __FUNCTION__}
#else
    #define SOURCE_INFO() (Source_Info){__LINE__, __FILE__, __FUNCTION__}
#endif 

//Gets called when assertion fails. 
//Does not have to terminate process since that is done at call site 
// (for easier debugging) by the assert macro itself.
//It is left unimplemented
void assertion_report(const char* expression, Source_Info source, const char* message, ...);

//Requires from platform layer:
void platform_abort();
#ifndef platform_trap
    #define platform_trap() 0
#endif // !platform_trap()

//==================== IMPLEMENTATION =======================

    //Doesnt do anything (failed branch) but still properly expands x and msg so it can be type checked.
    //Dissabled asserts expand to this.
    #define DISSABLED_TEST_MSG(x, msg, ...)           (0 ? ((void) (x), assertion_report("", SOURCE_INFO(), (msg), ##__VA_ARGS__)) : (void) 0)

    //If dissabled expand to this
    #define _IF_NOT_DO_ASSERTS(ignore)         DISSABLED_TEST_MSG
    #define _IF_NOT_DO_ASSERTS_SLOW(ignore)    DISSABLED_TEST_MSG
    #define _IF_NOT_DO_BOUNDS_CHECKS(ignore)   DISSABLED_TEST_MSG

    //Pre-Processor (PP) utils
    #define PP_CONCAT2(a, b) a ## b
    #define PP_CONCAT3(a, b, c)     PP_CONCAT2(PP_CONCAT2(a, b), c)
    #define PP_CONCAT4(a, b, c, d)  PP_CONCAT2(PP_CONCAT3(a, b, c), d)
    #define PP_CONCAT(a, b)         PP_CONCAT2(a, b)
    #define PP_ID(x) x

    //if CONDITION_DEFINE is defined: expands to x, 
    //else: expands to _IF_NOT_##CONDITION_DEFINE(x). See above how to use this.
    //The reason for its use is that simply all other things I have tried either didnt
    // work or failed to compose for obscure reasons
    #define PP_IF(CONDITION_DEFINE, x)         PP_CONCAT(_IF_NOT_, CONDITION_DEFINE)(x)
    #define _IF_NOT_(x) x

#endif