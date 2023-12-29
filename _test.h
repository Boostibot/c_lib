#ifndef JOT_TEST
#define JOT_TEST

#include "defines.h"
#include "array.h"
#include "time.h"
#include "random.h"

typedef struct Discrete_Distribution {
    Random_State state;
	i32_Array prob_table;
	i32 prob_sum;
    bool use_state;
} Discrete_Distribution;

EXPORT Discrete_Distribution random_discrete_make(const i32 probabilities[], isize probabilities_size);
EXPORT Discrete_Distribution random_discrete_make_state(const i32 probabilities[], isize probabilities_size, Random_State state);
EXPORT i32 random_discrete(Discrete_Distribution* distribution);
EXPORT void random_discrete_deinit(Discrete_Distribution* dist);

typedef enum Test_Func_Type {
    TEST_FUNC_TYPE_SIMPLE = 0,
    TEST_FUNC_TYPE_TIMED = 1,
    TEST_FUNC_TYPE_CUSTOM = 2,
} Test_Func_Type;

typedef void (*Test_Func)();
typedef void (*Test_Func_Timed)(f64 max_time);
typedef void (*Test_Func_Custom)(void* user_data);

EXPORT bool run_test(void* func, const char* name, Test_Func_Type type, f64 max_time, void* user_data);

#define RUN_TEST(func)                  ((0 ? *(Test_Func*)0 = (func) : NULL),        run_test((void*) (func), #func, TEST_FUNC_TYPE_SIMPLE, 0, NULL))
#define RUN_TEST_TIMED(func, time)      ((0 ? *(Test_Func_Timed*)0 = (func)  : NULL), run_test((void*) (func), #func, TEST_FUNC_TYPE_TIMED, (time), NULL))
#define RUN_TEST_CUSTOM(func, context)  ((0 ? *(Test_Func_Custom*)0 = (func) : NULL), run_test((void*) (func), #func, TEST_FUNC_TYPE_CUSTOM, 0, context))

//@NOTE: The first part of these macros is a type check. The condition never gets executed but the expression is validated by the type system.

#endif


#if (defined(JOT_ALL_IMPL) || defined(JOT_TEST_IMPL)) && !defined(JOT_TEST_HAS_IMPL)
#define JOT_TEST_HAS_IMPL

typedef struct Test_Run_Context {
    void* func;
    const char* name;
    Test_Func_Type type;
    f64 max_time;
    void* user_data;
} Test_Run_Context;

EXPORT void _run_test_try(void* context)
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

    LOG_INFO("TEST", "OK");
}

EXPORT void _run_test_recover(void* context, Platform_Sandbox_Error error)
{
    (void) context;
    if(error.exception != PLATFORM_EXCEPTION_ABORT)
    {
        LOG_ERROR("TEST", "FAILED! Exception occured: %s", platform_exception_to_string(error.exception));
        log_group_push();
        log_captured_callstack("TEST", LOG_TYPE_TRACE, error.call_stack, error.call_stack_size);
        log_group_pop();
    }
}

EXPORT bool run_test(void* func, const char* name, Test_Func_Type type, f64 max_time, void* user_data)
{
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

    log_group_push();
    bool success = platform_exception_sandbox(_run_test_try, &context, _run_test_recover, &context) == 0;
    log_group_pop();
    return success;
}

EXPORT Discrete_Distribution random_discrete_make(const i32 probabilities[], isize probabilities_size)
{
    i32 prob_sum = 0;
	for(isize i = 0; i < probabilities_size; i++)
		prob_sum += probabilities[i];

	i32_Array prob_table = {0};
	array_resize(&prob_table, prob_sum);
	
	i32 k = 0;
	for(i32 i = 0; i < probabilities_size; i++)
	{
		i32 end = k + probabilities[i];
		for(; k < end; k++)
		{
			CHECK_BOUNDS(k, prob_table.size);
			prob_table.data[k] = i;
		}
	}

	Discrete_Distribution out = {0};;
    out.prob_table = prob_table;
    out.prob_sum = prob_sum;

	return out;
}

EXPORT Discrete_Distribution random_discrete_make_state(const i32 probabilities[], isize probabilities_size, Random_State state)
{
    Discrete_Distribution out = random_discrete_make(probabilities, probabilities_size);
    out.state = state;
    out.use_state = true;
    return out;
}

EXPORT i32 random_discrete(Discrete_Distribution* distribution)
{
    Random_State* state = distribution->use_state ? &distribution->state : random_state();
	
    i64 random = random_state_range(state, 0, distribution->prob_sum);
	CHECK_BOUNDS(random, distribution->prob_table.size);

	i32 index = distribution->prob_table.data[random];
	return index;
}

EXPORT void random_discrete_deinit(Discrete_Distribution* dist)
{
	array_deinit(&dist->prob_table);
	dist->prob_sum = 0;
}

#endif