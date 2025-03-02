#ifndef MODULE_TEST_ALL
#define MODULE_TEST_ALL

#if defined(TEST_RUNNER)
#define MODULE_IMPL_ALL
#endif

#define MODULE_ALL_COUPLED
#define MODULE_ALL_TEST

#include "../defines.h"
#include "../assert.h"
#include "../profile.h"
#include "../allocator_tlsf.h"
#include "../allocator_tracking.h"
#include "../perf.h"
#include "../sort.h"
#include "../mem.h"
#include "../map.h"
#include "../utf.h"

#include "../list.h"
#include "../path.h"
#include "../slz4.h"
#include "../match.h"
#include "../base64.h"

#include "test_platform.h"
#include "test_random.h"
#include "test_arena.h"
#include "test_array.h"
#include "test_hash.h"
#include "test_log.h"
#include "test_mem.h"
#include "test_map.h"
#include "test_math.h"
#include "test_stable.h"
#include "test_image.h"
#include "test_utf.h"
#include "test_base64.h"
#include "test_serialize.h"
#include "test_spmc_queue.h"
#include "test_debug_allocator.h"
#include "test_unicode.h"

typedef enum Test_Func_Type {
    TEST_FUNC_TYPE_SIMPLE,
    TEST_FUNC_TYPE_TIMED,
} Test_Func_Type;

typedef void (*Test_Func)();
typedef void (*Test_Func_Timed)(double max_time);

typedef struct Test_Run_Context {
    void* func;
    const char* name;
    Test_Func_Type type;
    int _;
    double max_time;
} Test_Run_Context;

static bool run_test(Test_Run_Context context);
static int run_tests(int* total, double time, ...);

#define UNIT_TEST(func) SINIT(Test_Run_Context){(void*) (func), #func, TEST_FUNC_TYPE_SIMPLE}
#define TIMED_TEST(func, ...) SINIT(Test_Run_Context){(void*) (func), #func, TEST_FUNC_TYPE_TIMED, 0, ##__VA_ARGS__}

static void test_all(double total_time)
{
    //unicode_format_ranges_file("GeneralCategory.txt", "GeneralCategory_C.txt");
    run_tests(NULL, total_time, 
        UNIT_TEST(platform_test_all),
        UNIT_TEST(test_unicode_unit),
        UNIT_TEST(test_list),
        UNIT_TEST(test_image),
        UNIT_TEST(test_serialize),
        // UNIT_TEST(test_random),
        UNIT_TEST(test_path),
        UNIT_TEST(test_log),
        UNIT_TEST(test_match),
        TIMED_TEST(test_stable),
        TIMED_TEST(test_map),
        TIMED_TEST(test_base64),
        TIMED_TEST(test_utf),
        TIMED_TEST(test_array),
        TIMED_TEST(test_hash),
        TIMED_TEST(test_hash),
        TIMED_TEST(test_arena),
        TIMED_TEST(test_math),
        TIMED_TEST(test_mem),
        TIMED_TEST(test_sort),
        TIMED_TEST(test_debug_allocator),
        TIMED_TEST(slz4_test),
        TIMED_TEST(test_allocator_tlsf),
        TIMED_TEST(test_spmc_queue),
        UNIT_TEST(NULL)
    );
}

#if defined(TEST_RUNNER)
    void allocator_panic(Allocator_Error error)
    {
        (void) error;
        PANIC("Allocator error");
    }

    int main()
    {
        platform_init();
        
        Scratch_Arena* global_stack = global_scratch_arena();
        scratch_arena_init(global_stack, "global_scratch_arena", 64*GB, 8*MB, 0);

        File_Logger logger = {0};
        file_logger_init(&logger, "logs", FILE_LOGGER_USE);

        test_all(30);

        //no deinit code!
        return 0;
    }

    #if PLATFORM_OS == PLATFORM_OS_UNIX
        #include "../platform_linux.c"
    #elif PLATFORM_OS == PLATFORM_OS_WINDOWS
        #include "../platform_windows.c"
    #else
        #error Unsupported OS! Add implementation
    #endif
#endif

static void _run_test_try(void* context)
{
    Test_Run_Context* c = (Test_Run_Context*) context;
    if(c->type == TEST_FUNC_TYPE_SIMPLE)
        ((Test_Func) c->func)(); 
    if(c->type == TEST_FUNC_TYPE_TIMED)
        ((Test_Func_Timed) c->func)(c->max_time); 
}

static bool run_test(Test_Run_Context context)
{
    PROFILE_START();
    switch(context.type)
    {
        case TEST_FUNC_TYPE_SIMPLE: LOG_INFO("TEST", "%s ...", context.name); break;
        case TEST_FUNC_TYPE_TIMED:  LOG_INFO("TEST", "%s (time = %lfs) ...", context.name, context.max_time); break;
        default: UNREACHABLE();
    }

    Platform_Sandbox_Error error;
    bool ok = platform_exception_sandbox(_run_test_try, &context, &error);
    if(ok)
        LOG_OKAY("TEST", "%s OK", context.name);
    else {
        LOG_ERROR("TEST", "Exception occurred in test '%s': %s", context.name, error.exception);
        LOG_ERROR("TEST", "%s FAILED", context.name);
    }

    panic_recovered();
    PROFILE_STOP();
    return ok;
}

static int run_tests(int* total, double time, ...)
{
    Test_Run_Context contexts[256] = {0};
    int count = 0;
    int timed_count = 0;

    va_list ap;
    va_start(ap, time);
    for(; count < 256; count++)
    {
        contexts[count] = va_arg(ap, Test_Run_Context);
        timed_count += contexts[count].type == TEST_FUNC_TYPE_TIMED;
        if(contexts[count].func == NULL)
            break;
    }
    va_end(ap);

    LOG_INFO("TEST", "RUNNING %i TESTS (time = %lfs)", count, time);
    double time_for_one = time/timed_count;
    int successfull = 0;
    for(int i = 0; i < count; i++)
    {
        if(contexts[i].max_time == 0) 
            contexts[i].max_time = time_for_one;
        successfull += run_test(contexts[i]);
    }

    if(total)
        *total = count;

    if(successfull == count)
        LOG_OKAY("TEST", "TESTING FINISHED! passed %i of %i test uwu", count, successfull);
    else
        LOG_WARN("TEST", "TESTING FINISHED! passed %i of %i tests", count, successfull);

    return successfull;
}

#endif