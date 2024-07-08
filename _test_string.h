#pragma once
#include "string.h"
#include "_test.h"
#include "vformat.h"

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
    for(isize i = 0; i < ARRAY_SIZE(test_cases); i++)
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


static void test_string_find_single(const char* in_string_c, const char* search_for_c)
{
    String in_string = string_of(in_string_c);
    String search_for = string_of(search_for_c);

    for(isize from_i = 0; from_i <= in_string.size; from_i++)
    {
        char* std_found = strstr(in_string_c + from_i, search_for_c);
        isize std_found_i = std_found ? std_found - in_string_c : -1;
        isize our_found_i = string_find_first(in_string, search_for, from_i);
        TEST(std_found_i == our_found_i);
        TEST(std_found_i == our_found_i);
    }
}

static void test_string_find()
{
    test_string_find_single("hello world", "hello");
    test_string_find_single("hello world", "world");
    test_string_find_single("hello world", "l");
    test_string_find_single("hello world", "orldw");
    test_string_find_single("hello world", "ll");
    test_string_find_single("world", "world world");
    test_string_find_single("wwwwwwww", "ww");
    test_string_find_single("abababaaa", "ba");
}

static void test_string(f64 time)
{
    test_string_find();
    test_memset_pattern();
    (void) time;
}