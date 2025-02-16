#pragma once
#include "../mem.h"
#include "../assert.h"
#include "../defines.h"
#include "../time.h"
#include "../random.h"

static void test_memtile()
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
    for(isize i = 0; i < ARRAY_COUNT(test_cases); i++)
    {
        Test_Case test_case = test_cases[i];
        isize pattern_len = (isize) strlen(test_case.pattern);

        memset(field, 0, sizeof field);
        memset(expected, 0, sizeof expected);

        memtile(field, test_case.field_size, test_case.pattern, pattern_len);

        memcpy(expected, test_case.expected, strlen(test_case.expected));
        TEST(memcmp(field, expected, sizeof field) == 0);
    }
}

static isize memfind_trivial(const void* ptr, uint8_t byte, isize size)
{
    uint8_t* curr = (uint8_t*) ptr;
    for(isize i = 0; i < size; i++)
        if(curr[i] == byte)
            return i;

    return -1;
}

static isize memfind_last_trivial(const void* ptr, uint8_t byte, isize size)
{
    uint8_t* curr = (uint8_t*) ptr;
    for(isize i = size; i-- > 0; )
        if(curr[i] == byte)
            return i;

    return -1;
}

static isize memfind_not_trivial(const void* ptr, uint8_t byte, isize size)
{
    uint8_t* curr = (uint8_t*) ptr;
    for(isize i = 0; i < size; i++)
        if(curr[i] != byte)
            return i;

    return -1;
}

static isize memfind_last_not_trivial(const void* ptr, uint8_t byte, isize size)
{
    uint8_t* curr = (uint8_t*) ptr;
    for(isize i = size; i-- > 0; )
        if(curr[i] != byte)
            return i;

    return -1;
}

static void test_memfind_single(const char* str, char byte, isize size)
{
    if(size < 0)
        size = strlen(str);
    for(isize i = 0; i < size; i++) {
        isize first_trivial = memfind_trivial(str + i, (uint8_t) byte, size - i);
        isize first = memfind(str + i, (uint8_t) byte, size - i);

        isize first_not_trivial = memfind_not_trivial(str + i, (uint8_t) byte, size - i);
        isize first_not = memfind_not(str + i, (uint8_t) byte, size - i);
        
        isize last_trivial = memfind_last_trivial(str + i, (uint8_t) byte, size - i);
        isize last = memfind_last(str + i, (uint8_t) byte, size - i);

        isize last_not_trivial = memfind_last_not_trivial(str + i, (uint8_t) byte, size - i);
        isize last_not = memfind_last_not(str + i, (uint8_t) byte, size - i);
        
        TEST(first_trivial == first);
        TEST(first_not_trivial == first_not);
        TEST(last_trivial == last);
        TEST(last_not_trivial == last_not);
    }
}

static void test_memcheck(double time)
{
    //do some unit tests
    {
        test_memfind_single("", 'a', -1);
        test_memfind_single("b", 'a', -1);
        test_memfind_single("a", 'a', -1);
        test_memfind_single("ab", 'a', -1);
        test_memfind_single("aaaaaaaaa", 'a', -1);
        test_memfind_single("aaaaaaaaab", 'a', -1);
        test_memfind_single("aaaaaaaaaaaaaaa", 'a', -1);
        test_memfind_single("aaaaaaaaaaaaaaa", 'a', -1);
        test_memfind_single("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 'a', -1);
        test_memfind_single("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", 'a', -1);
        test_memfind_single("aaaaaaaaaaaaaaaaaaaaaaaaaaaaabaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", 'a', -1);
        test_memfind_single("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabaaaabaaaaaaaaaaaabaaaa", 'a', -1);
        test_memfind_single("baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabaaaabaaaaaaaaaaaabaaaa", 'a', -1);
        test_memfind_single("baaaaaaaabbbbbbaaaaaaaaabbbbbaaaaaaaaaaabaaaabaaaaaaaaaaaabaaaa", 'a', -1);
        test_memfind_single("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 'a', -1);
        const char* test_string = "1354 iiuq0  9uk 1' ] [1o. 1\';;'\'; a   \f\v d2564 \n\r  AA45ag534g35a4XXXXXXXXX354af8y8y79uh45ht   8952; u7;y 5 u9\f 4 g   \v\f ";
        test_memfind_single(test_string, '1', -1);
        test_memfind_single(test_string, 'X', -1);
        test_memfind_single(test_string, '\v', -1);
    }

    for(double start = clock_sec(); clock_sec() - start < time; ) {
        enum {TEST_SIZE = 1024};
        uint64_t bytes[TEST_SIZE];
        for(int i = 0; i < TEST_SIZE; i++)
            bytes[i] = random_u64();

        char c = (char) random_u64();
        test_memfind_single((const char*) bytes, c, sizeof(bytes));
    }
}

static void test_mem(double time)
{
    test_memtile();
    test_memcheck(time);
}