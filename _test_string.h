#pragma once
#include "string.h"
#include "_test.h"
#include "vformat.h"

static void test_find_first_single(String string, char c, isize from)
{
    isize truth = string_find_first_char_vanilla(string, c, from);
    isize unsafe = string_find_first_char_unsafe(string, c, from);
    isize sse = string_find_first_char_sse(string, c, from);

    if(truth != sse || truth != unsafe || truth != sse)
        TEST(false, "test_find_first_single failed! with string '%.*s' char '%c' and from: %lli", STRING_PRINT(string), c, (lli) from);
}

static isize string_find_first_char_strlen(String string, char c, isize from)
{
    return (isize) strlen(string.data + from); (void) c;
}

static void test_find_first(f64 time)
{
    test_find_first_single(STRING("hello world"), 'o', 1);
    test_find_first_single(STRING("hello world"), ' ', 1);
    test_find_first_single(STRING("hello world hello world"), 'x', 1);
    test_find_first_single(STRING("hello world hello world"), 'h', 1);
    test_find_first_single(STRING("hello world hello world hello world x"), 'x', 0);
    test_find_first_single(STRING("hello world hello world hello world x"), 'x', 30);

    String_Builder data = {0};
    builder_resize(&data, 1024*1024);

    for(isize i = 0; i < data.size; i++)
        data.data[i] = (char) (random_u64() % 256);

    String str = data.string;
    for(f64 start = clock_s(), now = 0; (now = clock_s()) < start + time;)
    {
        char search_for = (char) (random_u64() % 256);
        isize from = random_range(0, str.size);
        test_find_first_single(str, search_for, from);
    }

    builder_deinit(&data);
}

static void bemchmark_find_first(isize max_size, u64 max_value, f64 discard, f64 time)
{
    String_Builder data = {0};
    builder_resize(&data, max_size);

    for(isize i = 0; i < data.size; i++)
        data.data[i] = (char) (random_u64() % max_value);

    String str = data.string;

    typedef struct Test_Case {
        isize (*func)(String string, char c, isize from);
        const char* name;
        isize num_found;
        Perf_Counter counter;
        Perf_Stats stats;
    } Test_Case;

    Test_Case cases[] = {
        {string_find_first_char_vanilla, "vanilla"},
        {string_find_first_char_unsafe, "unsafe"},
        {string_find_first_char_sse, "sse"},
        {string_find_first_char_strlen, "strlen"},
    };
    
    isize repeats = 8;
    for(isize case_i = 0; case_i < STATIC_ARRAY_SIZE(cases); case_i ++)
    {
        Test_Case* test_case = &cases[case_i];

        for(f64 start = clock_s(), now = 0; (now = clock_s()) < start + time;)
        {
            char find = (char) (random_u64() % max_value);
            isize from = random_range(0, str.size);

            i64 running = perf_start();
            for(isize i = 0; i < repeats; i++)
            {
                if(test_case->func(str, find, from) != -1)
                    test_case->num_found += 1;
            }

            if(now >= start + discard)
                perf_end(&test_case->counter, running);
        }

        test_case->stats = perf_get_stats(test_case->counter, repeats);
    }

    LOG_INFO("TEST", "printing results for max_size: %lli max_value: %lli", (lli) max_size, (lli) max_value);
        for(isize case_i = 0; case_i < STATIC_ARRAY_SIZE(cases); case_i ++)
        {
            Test_Case* test_case = &cases[case_i];
            LOG_INFO(">TEST", "%20s total: %15.8lf avg: %12.8lf runs: %-8lli σ/μ %13.6lf [%13.6lf %13.6lf] (ms) found: %lli", 
                    test_case->name,
			        test_case->stats.total_s*1000,
			        test_case->stats.average_s*1000,
                    (lli) test_case->stats.runs,
                    test_case->stats.normalized_standard_deviation_s,
			        test_case->stats.min_s*1000,
			        test_case->stats.max_s*1000,
                    test_case->num_found
		        );
        }

    builder_deinit(&data);
}

static void test_memset_pattern()
{
    typedef struct  {
        const char* pattern;
        isize field_size;
        const char* expected;
    } Test_Case;
    
    Test_Case test_cases[] = {
        {"",            0,  ""},
        {"a",           0,  ""},
        {"ba",          1,  "b"},
        {"hahe",        7,  "hahehah"},
        {"xxxxyyyy",    7,  "xxxxyyy"},
        {"hahe",        9,  "hahehaheh"},
        {"hahe",        24, "hahehahehahehahehahehahe"},
        {"hahe",        25, "hahehahehahehahehahehaheh"},
        {"hahe",        26, "hahehahehahehahehahehaheha"},
        {"hahe",        27, "hahehahehahehahehahehahehah"},
    };
    
    char field[128] = {0};
    char expected[128] = {0};
    for(isize i = 0; i < STATIC_ARRAY_SIZE(test_cases); i++)
    {
        Test_Case test_case = test_cases[i];
        isize pattern_len = (isize) strlen(test_case.pattern);

        memset(field, 0, sizeof field);
        memset(expected, 0, sizeof expected);

        memset_pattern(field, test_case.field_size, test_case.pattern, pattern_len);

        memcpy(expected, test_case.expected, strlen(test_case.expected));
        TEST(memcmp(field, expected, sizeof field) == 0);
    }
}

static void test_string(f64 time)
{
    test_memset_pattern();
    test_find_first(time);
}