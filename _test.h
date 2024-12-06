#ifndef JOT_TEST
#define JOT_TEST

#include "defines.h"
#include "assert.h"
#include "array.h"
#include "time.h"
#include "random.h"
#include "log.h"
#include "profile.h"

typedef enum Test_Func_Type {
    TEST_FUNC_TYPE_SIMPLE,
    TEST_FUNC_TYPE_TIMED,
    TEST_FUNC_TYPE_CUSTOM,
} Test_Func_Type;

typedef void (*Test_Func)();
typedef void (*Test_Func_Timed)(f64 max_time);
typedef void (*Test_Func_Custom)(void* user_data);

EXTERNAL bool run_test(void* func, const char* name, Test_Func_Type type, f64 max_time, void* user_data);

#define RUN_TEST(func)                  ((void) sizeof(*(Test_Func*)0 = (func)),           run_test((void*) (func), #func, TEST_FUNC_TYPE_SIMPLE, 0, NULL))
#define RUN_TEST_TIMED(func, time)      ((void) sizeof(*(Test_Func_Timed*)0 = (func)),     run_test((void*) (func), #func, TEST_FUNC_TYPE_TIMED, (time), NULL))
#define RUN_TEST_CUSTOM(func, context)  ((void) sizeof(*(Test_Func_Custom*)0 = (func)),    run_test((void*) (func), #func, TEST_FUNC_TYPE_CUSTOM, 0, context))

//@NOTE: The first part of these macros is a type check. The comparison never gets executed but the expression goes though type checking.
#endif


#if (defined(JOT_ALL_IMPL) || defined(JOT_TEST_IMPL)) && !defined(JOT_TEST_HAS_IMPL)
#define JOT_TEST_HAS_IMPL

typedef struct Test_Run_Context {
    void* func;
    const char* name;
    Test_Func_Type type;
    u32 _;
    f64 max_time;
    void* user_data;
} Test_Run_Context;

EXTERNAL void _run_test_try(void* context)
{
    Test_Run_Context* c = (Test_Run_Context*) context;
    switch(c->type)
    {
        case TEST_FUNC_TYPE_SIMPLE: 
            ((Test_Func) c->func)(); 
        break;
        case TEST_FUNC_TYPE_TIMED: 
            ((Test_Func_Timed) c->func)(c->max_time); 
        break;
        case TEST_FUNC_TYPE_CUSTOM: 
            ((Test_Func_Custom) c->func)(c->user_data); 
        break;
        default: UNREACHABLE();
    }
}

EXTERNAL void _run_test_recover(void* context, Platform_Sandbox_Error error)
{
    Test_Run_Context* c = (Test_Run_Context*) context;
    if(error.exception != PLATFORM_EXCEPTION_ABORT)
    {
        PROFILE_INSTANT("failed test");
        LOG_ERROR("TEST", "Exception occurred in test '%s': %s", c->name, platform_exception_to_string(error.exception));
        // log_captured_callstack(log_trace(">TEST"), error.call_stack, error.call_stack_size);
    }
}

EXTERNAL bool run_test(void* func, const char* name, Test_Func_Type type, f64 max_time, void* user_data)
{
    PROFILE_START();
    Test_Run_Context context = {0};
    context.user_data = user_data;
    context.max_time = max_time;
    context.name = name;
    context.func = func;
    context.type = type;

    switch(type)
    {
        case TEST_FUNC_TYPE_SIMPLE: LOG_INFO("TEST", "%s ...", name); break;
        case TEST_FUNC_TYPE_TIMED:  LOG_INFO("TEST", "%s (time = %lfs) ...", name, max_time); break;
        case TEST_FUNC_TYPE_CUSTOM: LOG_INFO("TEST", "%s (custom) ...", name); break;
        default: UNREACHABLE();
    }

    log_indent();
    bool success = platform_exception_sandbox(_run_test_try, &context, _run_test_recover, &context) == 0;
    log_outdent();
    if(success)
        LOG_OKAY("TEST", "%s OK", name);
    else
        LOG_ERROR("TEST", "%s FAILED", name);

    PROFILE_STOP();
    return success;
}

#endif