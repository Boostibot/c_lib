#pragma once
#include "_test_string.h"
#include "_test_random.h"
#include "_test_array.h"
#include "_test_hash_index.h"
#include "_test_log.h"
#include "_test_math.h"
#include "_test_hash_index.h"
#include "_test_stable_array.h"
#include "_test_lpf.h"
#include "_test_image.h"

INTERNAL void test_gcd();

INTERNAL void test_all()
{
    LOG_INFO("TEST", "RUNNING ALL TESTS");
    int total_count = 0;
    int passed_count = 0;

    #define INCR total_count += 1, passed_count += (int)

    INCR RUN_TEST(test_image);
    INCR RUN_TEST(test_format_lpf);
    INCR RUN_TEST(test_stable_array);
    INCR RUN_TEST(test_stable_array);
    INCR RUN_TEST(test_log);
    //INCR RUN_TEST(test_random);
    
    INCR RUN_TEST_TIMED(test_hash_index, 3.0);
    INCR RUN_TEST_TIMED(test_string, 1);
    INCR RUN_TEST_TIMED(test_array, 3.0);
    INCR RUN_TEST_TIMED(test_math, 3.0);
    INCR RUN_TEST_TIMED(test_hash_index, 3.0);
    
    #undef INCR

    if(passed_count == total_count)
        LOG_SUCCESS("TEST", "TESTING FINISHED! passed %i of %i test uwu", total_count, passed_count);
    else
        LOG_WARN("TEST", "TESTING FINISHED! passed %i of %i tests", total_count, passed_count);
}
