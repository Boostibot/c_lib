#pragma once
#include "_test_string.h"
#include "_test_random.h"
#include "_test_array.h"
#include "_test_hash_index.h"
//#include "_test_log.h"
#include "_test_math.h"
#include "_test_stable_array.h"
#include "_test_lpf.h"
#include "_test_image.h"

INTERNAL void test_all(f64 total_time)
{
    LOG_INFO("TEST", "RUNNING ALL TESTS");
    int total_count = 0;
    int passed_count = 0;

    #define INCR total_count += 1, passed_count += (int)

    INCR RUN_TEST(test_image);
    INCR RUN_TEST(test_lpf);
    INCR RUN_TEST(test_stable_array);
    //INCR RUN_TEST(test_log);
    //INCR RUN_TEST(test_random);
    
    INCR RUN_TEST_TIMED(test_hash_index, total_time/4);
    INCR RUN_TEST_TIMED(test_string, total_time/4);
    INCR RUN_TEST_TIMED(test_array, total_time/4);
    INCR RUN_TEST_TIMED(test_math, total_time/4);
    
    #undef INCR

    if(passed_count == total_count)
        LOG_OKAY("TEST", "TESTING FINISHED! passed %i of %i test uwu", total_count, passed_count);
    else
        LOG_WARN("TEST", "TESTING FINISHED! passed %i of %i tests", total_count, passed_count);
}
