#ifndef JOT_TEST_ALL_H
#define JOT_TEST_ALL_H

#if defined(TEST_RUNNER)
#define JOT_ALL_IMPL
#define JOT_PANIC_IMPL
#endif

#define JOT_COUPLED
#define JOT_ALL_TEST
#include "assert.h"
#include "defines.h"
#include "profile.h"
#include "list.h"
#include "path.h"
#include "allocator_tlsf.h"
#include "slz4.h"
#include "perf.h"
#include "sort.h"

#include "_test_string.h"
#include "_test_random.h"
#include "_test_arena.h"
#include "_test_array.h"
#include "_test_hash.h"
#include "_test_log.h"
#include "_test_math.h"
#include "_test_stable_array.h"
#include "_test_lpf.h"
#include "_test_image.h"
#include "_test_chase_lev_queue.h"
// #include "_test_string_map.h"

INTERNAL void test_all(f64 total_time)
{
    PROFILE_START();
    
    LOG_INFO("TEST", "RUNNING ALL TESTS");
    int total_count = 0;
    int passed_count = 0;

    #define INCR total_count += 1, passed_count += (int)
    
    // INCR RUN_TEST(test_string_map);
    INCR RUN_TEST(platform_test_all);
    
    INCR RUN_TEST(test_list);
    INCR RUN_TEST(test_image);
    INCR RUN_TEST(test_lpf);
    INCR RUN_TEST(test_stable_array);
    INCR RUN_TEST(test_log);
    //INCR RUN_TEST(test_random);
    INCR RUN_TEST(test_path);

    INCR RUN_TEST_TIMED(test_chase_lev_queue, total_time/8);
    INCR RUN_TEST_TIMED(test_sort, total_time/8);
    INCR RUN_TEST_TIMED(test_hash, total_time/8);
    INCR RUN_TEST_TIMED(test_arena, total_time/8);
    INCR RUN_TEST_TIMED(test_array, total_time/8);
    INCR RUN_TEST_TIMED(test_math, total_time/8);
    INCR RUN_TEST_TIMED(test_string, total_time/8);
    INCR RUN_TEST_TIMED(test_allocator_tlsf, total_time/8);
    INCR RUN_TEST_TIMED(slz4_test, total_time/8);
    
    #undef INCR

    if(passed_count == total_count)
        LOG_OKAY("TEST", "TESTING FINISHED! passed %i of %i test uwu", total_count, passed_count);
    else
        LOG_WARN("TEST", "TESTING FINISHED! passed %i of %i tests", total_count, passed_count);

    PROFILE_STOP();
}

#if defined(TEST_RUNNER)

    #include "allocator_malloc.h"
    // #include "log_file.h"
    int main()
    {
        platform_init();
        
        Arena_Stack* global_stack = scratch_arena_stack();
        arena_stack_init(global_stack, "scratch_arena_stack", 64*GB, 8*MB, 0);

        File_Logger logger = {0};
        file_logger_init(&logger, "logs", FILE_LOGGER_USE);

        test_all(30);

        //no deinit code!
        return 0;
    }

    #if PLATFORM_OS == PLATFORM_OS_UNIX
        #include "platform_linux.c"
    #elif PLATFORM_OS == PLATFORM_OS_WINDOWS
        #include "platform_windows.c"
    #else
        #error Unsupported OS! Add implementation
    #endif
#endif

#endif