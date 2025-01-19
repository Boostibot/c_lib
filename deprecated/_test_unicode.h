#pragma once

#ifdef _TEST_UNICODE_TYPE_SET
//builtin types
#if _TEST_UNICODE_TYPE_SET == 1

    #include <stdint.h>
    #include <stddef.h>
    #define UNICODE_OWN_TYPES
    typedef char        utf8_t;
    typedef wchar_t     utf16_t;
    typedef long        utf32_t;
    typedef long        codepoint_t;
    typedef size_t      isize;

//Signed types
#elif _TEST_UNICODE_TYPE_SET == 2

    #include <stdint.h>
    #define UNICODE_OWN_TYPES
    typedef int8_t      utf8_t;
    typedef int16_t     utf16_t;
    typedef int32_t     utf32_t;
    typedef int32_t     codepoint_t;
    typedef int64_t     isize;

//Unsigned types
#elif _TEST_UNICODE_TYPE_SET == 3
    
    #include <stdint.h>
    #define UNICODE_OWN_TYPES
    typedef uint8_t      utf8_t;
    typedef uint16_t     utf16_t;
    typedef uint32_t     utf32_t;
    typedef uint32_t     codepoint_t;
    typedef uint64_t     isize;
    
//Default types
#else

#endif
#endif

//Add our example so we can run it
#define _UNICODE_EXAMPLE

#include "log.h"

#ifdef MODULE_LOG
#define printf(format, ...) LOG_INFO("TEST", (format), ##__VA_ARGS__)
#endif

#include "unicode.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <locale.h> //so that visual studio actually prints our strings


#ifndef TEST
#include <assert.h>
#define TEST(a, msg, ...) (!(a) ? printf(msg, ##__VA_ARGS__), assert((a) && (msg)) : (void) 0)
#endif // !TEST

//Handcrafted tests between utfs
static void test_unicode_utf8_to_utf16();
static void test_unicode_utf16_to_utf8();

//Test all conversion directions by generating a sequence of random UTF codepoints
//And testing if round trips are losless.
static void test_unicode_stress_roundtrips(double max_time);

//Runs all tests
static void test_unicode(double max_time)
{
    (void) max_time;
    setlocale(LC_ALL, "en_US.UTF-8"); //so that visual studio actually prints our strings
    ASSERT(sizeof(char)    == sizeof(uint8_t)  && "On this platform wide strings are utf8");
    ASSERT(sizeof(wchar_t) == sizeof(uint16_t) && "On this platform wide strings are utf16");

    //Run examples
    printf("unicode running test and examples:\n");
    unicode_example();
    unicode_example_checks();
    printf("unicode examples finished!\n");

    //Run tests
    test_unicode_utf8_to_utf16();
    printf("unicode utf8 -> utf16 finished!\n");
    
    test_unicode_utf16_to_utf8();
    printf("unicode utf16 -> utf8 finished!\n");
    
    test_unicode_stress_roundtrips(max_time);
    printf("unicode stress testing finished!\n");
}

typedef long long lli;

typedef struct Test_Unicode_Fail_At
{
    bool should_succeed;
    isize read_fail_at;
    isize write_fail_at;
} Test_Unicode_Fail_At;

typedef enum Test_Unicode_Compare{
    TEST_UNI_EQUAL,
    TEST_UNI_NOT_EQUAL,
} Test_Unicode_Compare;

static void test_unicode_single_utf8_to_utf16(Test_Unicode_Compare compare, const char* input, const wchar_t* expected_output, Test_Unicode_Fail_At fail_at, codepoint_t replacement);
static void test_unicode_single_utf16_to_utf8(Test_Unicode_Compare compare, const wchar_t* input, const char* expected_output, Test_Unicode_Fail_At fail_at, codepoint_t replacement);

static void test_unicode_utf8_to_utf16()
{
    Test_Unicode_Fail_At SUCCEED = {true};
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "", L"",                           SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_NOT_EQUAL, "a", L"",                          SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_NOT_EQUAL, "", L"a",                          SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "a", L"a",                         SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "Hello world!", L"Hello world!",   SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "Hello!", L"Hello!",               SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_NOT_EQUAL, "Hello!", L"Hell!",                SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "ř", L"ř",                         SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "řčě", L"řčě",                     SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_NOT_EQUAL, "řcě", L"řce",                     SUCCEED, UNICODE_ERROR);

    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "西", L"西",                        SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "西艾弗", L"西艾弗",                 SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "豆贝尔维", L"豆贝尔维",              SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_NOT_EQUAL, "豆贝尔1", L"豆贝尔维",              SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     "开开开开", L"开开开开",              SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf8_to_utf16(TEST_UNI_NOT_EQUAL, "开开开维", L"开开开开",              SUCCEED, UNICODE_ERROR);

    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     
        "Αα,Ββ,Γγ,Δδ,Εε,Ζζ,Ηη,Θθ,Ιι,Κκ,Λλ,Μμ,Νν,Ξξ,Οο,Ππ,Ρρ,Σσ/ς,Ττ,Υυ,Φφ,Χχ,Ψψ,Ωω",
        L"Αα,Ββ,Γγ,Δδ,Εε,Ζζ,Ηη,Θθ,Ιι,Κκ,Λλ,Μμ,Νν,Ξξ,Οο,Ππ,Ρρ,Σσ/ς,Ττ,Υυ,Φφ,Χχ,Ψψ,Ωω", SUCCEED, UNICODE_ERROR);

    //Invalid 2 Octet Sequence "\xc3\x28"
    {
        Test_Unicode_Fail_At FAIL = {false};
        FAIL.read_fail_at = 0;
        FAIL.write_fail_at = 0;
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "\xc3\x28", L"", FAIL, UNICODE_ERROR);
    
        FAIL.read_fail_at = 1;
        FAIL.write_fail_at = 1;
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "a\xc3\x28", L"a", FAIL, UNICODE_ERROR);
    
        FAIL.read_fail_at = 12;
        FAIL.write_fail_at = 12;
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "Hello world!""\xc3\x28""abc", L"Hello world!", FAIL, UNICODE_ERROR);
        
        TEST(unicode_codepoint_from_utf8("a") == unicode_codepoint_from_ascii('a'), "ascii should work correctly");
        TEST(unicode_codepoint_from_utf8("/") == unicode_codepoint_from_ascii('/'), "ascii should work correctly");
        TEST(unicode_codepoint_from_utf8("a") == unicode_codepoint_from_utf8("az"), "should interpret only one codepoint");
        TEST(unicode_codepoint_from_utf8("č") == unicode_codepoint_from_utf8("čž"), "should interpret only one codepoint");

        //Testing patching sequence up
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "Hello world!""\xc3\x28""abc", L"Hello world!?abc", SUCCEED, unicode_codepoint_from_ascii('?'));
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "Hello world!""\xc3\x28""abc", L"Hello world!Xabc", SUCCEED, unicode_codepoint_from_ascii('X'));
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "Hello world!""\xc3\x28\xc3\x28""abc", L"Hello world!ččabc", SUCCEED, unicode_codepoint_from_utf8("č"));
    }

    {
        Test_Unicode_Fail_At FAIL = {false};
        FAIL.read_fail_at = 3;
        FAIL.write_fail_at = 3;
        
        //Invalid Sequence Identifier "\xc3\x28"
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xc3\x28""xxx", L"abc", FAIL, UNICODE_ERROR);
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xc3\x28""xxx", L"abc�xxx", SUCCEED, UNCIODE_INVALID);
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xc3\x28""xxx", L"abc�xxx", SUCCEED, unicode_codepoint_from_utf8("�"));
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xc3\x28""xxx", L"abcxxx", SUCCEED, UNICODE_ERROR_SKIP);
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xc3\x28", L"abc", SUCCEED, UNICODE_ERROR_SKIP);

        //Invalid Sequence Identifier "\xa0\xa1"
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xa0\xa1""xxx", L"abc", FAIL, UNICODE_ERROR);

        //Invalid 3 Octet Sequence (in 2nd Octet) "\xe2\x28\xa1",
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xe2\x28\xa1""xxx", L"abc", FAIL, UNICODE_ERROR);

        //Invalid 3 Octet Sequence (in 3rd Octet) "\xe2\x82\x28",
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xe2\x82\x28""xxx", L"abc", FAIL, UNICODE_ERROR);

        //Invalid 4 Octet Sequence (in 2nd Octet) "\xf0\x28\x8c\xbc",
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xf0\x28\x8c\xbc""xxx", L"abc", FAIL, UNICODE_ERROR);

        //Invalid 4 Octet Sequence (in 3rd Octet) "\xf0\x90\x28\xbc",
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xf0\x90\x28\xbc""xxx", L"abc", FAIL, UNICODE_ERROR);

        //Invalid 4 Octet Sequence (in 4th Octet) "\xf0\x28\x8c\x28",
        test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL, "abc""\xf0\x28\x8c\x28""xxx", L"abc", FAIL, UNICODE_ERROR);
    }
}

static void test_unicode_utf16_to_utf8()
{
    Test_Unicode_Fail_At SUCCEED = {true};
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"", "",                           SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_NOT_EQUAL, L"a", "",                          SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_NOT_EQUAL, L"", "a",                          SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"a", "a",                         SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"Hello world!", "Hello world!",   SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"Hello!", "Hello!",               SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_NOT_EQUAL, L"Hello!", "Hell!",                SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"ř", "ř",                         SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"řčě", "řčě",                     SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_NOT_EQUAL, L"řcě", "řce",                     SUCCEED, UNICODE_ERROR);

    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"西", "西",                        SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"西艾弗", "西艾弗",                 SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"豆贝尔维", "豆贝尔维",              SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_NOT_EQUAL, L"豆贝尔1", "豆贝尔维",              SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_EQUAL,     L"开开开开", "开开开开",              SUCCEED, UNICODE_ERROR);
    test_unicode_single_utf16_to_utf8(TEST_UNI_NOT_EQUAL, L"开开开维", "开开开开",              SUCCEED, UNICODE_ERROR);

    test_unicode_single_utf8_to_utf16(TEST_UNI_EQUAL,     
        "Αα,Ββ,Γγ,Δδ,Εε,Ζζ,Ηη,Θθ,Ιι,Κκ,Λλ,Μμ,Νν,Ξξ,Οο,Ππ,Ρρ,Σσ/ς,Ττ,Υυ,Φφ,Χχ,Ψψ,Ωω",
        L"Αα,Ββ,Γγ,Δδ,Εε,Ζζ,Ηη,Θθ,Ιι,Κκ,Λλ,Μμ,Νν,Ξξ,Οο,Ππ,Ρρ,Σσ/ς,Ττ,Υυ,Φφ,Χχ,Ψψ,Ωω", SUCCEED, UNICODE_ERROR);
}

static void test_unicode_single_utf8_to_utf16(Test_Unicode_Compare compare, const char* input, const wchar_t* expected_output, Test_Unicode_Fail_At fail_at, codepoint_t replacement)
{
    ASSERT(input != NULL && expected_output != NULL);

    isize input_len = strlen(input);

    wchar_t* converted = NULL;
    isize converted_len = 0;

    isize reading_finished_at = 0;
    isize writing_finished_at = unicode_utf8_to_utf16(NULL, 0, (const utf8_t*) input, input_len, &reading_finished_at, replacement);

    bool suceeded = reading_finished_at == input_len;
    TEST(suceeded == fail_at.should_succeed, "The parsing should fail excatly when we want it to fail!");
    if(!suceeded)
    {
        TEST(reading_finished_at == fail_at.read_fail_at, "reading finished: %lli should have %lli", (lli) reading_finished_at, (lli) fail_at.read_fail_at);
        TEST(writing_finished_at == fail_at.write_fail_at, "writing finished: %lli should have %lli", (lli) writing_finished_at, (lli) fail_at.write_fail_at);
    }

    TEST(writing_finished_at >= 0 && reading_finished_at >= 0, "should always be possitive");

    converted = (wchar_t*) calloc(writing_finished_at + 1, sizeof(utf16_t)); //+1 for null termination
    converted_len = writing_finished_at;
    TEST(converted != NULL, "OUT OF MEMORY");
    
    isize new_reading_finished_at = 0;
    isize new_writing_finished_at = unicode_utf8_to_utf16((utf16_t*) converted, converted_len, (const utf8_t*) input, input_len, &new_reading_finished_at, replacement);
    TEST(reading_finished_at == new_reading_finished_at, "the 'fake run' (%lli) reading size should match the 'writing run' reading size (%lli)", (lli) reading_finished_at, (lli) new_reading_finished_at);
    TEST(writing_finished_at == new_writing_finished_at, "the 'fake run' (%lli) writing size should match the 'writing run' writing size (%lli)", (lli) writing_finished_at, (lli) new_writing_finished_at);

    bool new_suceeded = new_reading_finished_at == input_len;
    TEST(new_suceeded == fail_at.should_succeed, "The 'write run' should fail exactly the same way as the 'fake run'!");

    bool are_equal = wcscmp(converted, expected_output) == 0;
    bool should_be_equal = compare == TEST_UNI_EQUAL;

    if(are_equal != should_be_equal)
    {
        printf("outputs dont match (or match when they shouldnt!)"
            "\nexpected:  %ls"
            "\nconverted: %ls", expected_output, converted
        );
    }
    TEST(are_equal == should_be_equal, "outputs dont match (or match when they shouldnt!)"
        "\ninput:     %s"
        "\nexpected:  %ls"
        "\nconverted: %ls", input, expected_output, converted
    );

    free(converted);
}

static void test_unicode_single_utf16_to_utf8(Test_Unicode_Compare compare, const wchar_t* input, const char* expected_output, Test_Unicode_Fail_At fail_at, codepoint_t replacement)
{
    ASSERT(input != NULL && expected_output != NULL);

    isize input_len = wcslen(input);

    char* converted = NULL;
    isize converted_len = 0;

    isize reading_finished_at = 0;
    isize writing_finished_at = unicode_utf16_to_utf8(NULL, 0, (const utf16_t*) input, input_len, &reading_finished_at, replacement);

    bool suceeded = reading_finished_at == input_len;
    TEST(suceeded == fail_at.should_succeed, "The parsing should fail excatly when we want it to fail!");
    if(!suceeded)
    {
        TEST(reading_finished_at == fail_at.read_fail_at, "reading finished: %lli should have %lli", (lli) reading_finished_at, (lli) fail_at.read_fail_at);
        TEST(writing_finished_at == fail_at.write_fail_at, "writing finished: %lli should have %lli", (lli) writing_finished_at, (lli) fail_at.write_fail_at);
    }

    TEST(writing_finished_at >= 0 && reading_finished_at >= 0, "should always be possitive");

    converted = (char*) calloc(writing_finished_at + 1, sizeof(utf8_t)); //+1 for null termination
    converted_len = writing_finished_at;
    TEST(converted != NULL, "OUT OF MEMORY");
    
    isize new_reading_finished_at = 0;
    isize new_writing_finished_at = unicode_utf16_to_utf8((utf8_t*) converted, converted_len, (const utf16_t*) input, input_len, &new_reading_finished_at, replacement);
    TEST(reading_finished_at == new_reading_finished_at, "the 'fake run' (%lli) reading size should match the 'writing run' reading size (%lli)", (lli) reading_finished_at, (lli) new_reading_finished_at);
    TEST(writing_finished_at == new_writing_finished_at, "the 'fake run' (%lli) writing size should match the 'writing run' writing size (%lli)", (lli) writing_finished_at, (lli) new_writing_finished_at);

    bool new_suceeded = new_reading_finished_at == input_len;
    TEST(new_suceeded == fail_at.should_succeed, "The 'write run' should fail exactly the same way as the 'fake run'!");

    bool are_equal = strcmp(converted, expected_output) == 0;
    bool should_be_equal = compare == TEST_UNI_EQUAL;
    TEST(are_equal == should_be_equal, "outputs dont match (or match when they shouldnt!)"
        "\ninput:     %ls"
        "\nexpected:  %s"
        "\nconverted: %s", input, expected_output, converted
    );

    free(converted);
}



static double _unicode_clock_s();
static uint64_t _unicode_random_splitmix(uint64_t* state);
static bool _unicode_are_memory_block_equal(const void* a, isize a_size, const void* b, isize b_size, isize type_size);


static void test_unicode_stress_roundtrips(double max_time)
{
    enum {
        MAX_SIZE = 1024*4, 
        MAX_ITERS = 1000*1000, //so we dont get stuck in an infinite loop
        MIN_ITERS = 10, //for debugging
        MAX_SIZE_MASK = MAX_SIZE - 1,
    };

    uint64_t seed = clock();
    uint64_t random_state = seed;
    double start = _unicode_clock_s();
	for(isize i = 0; i < MAX_ITERS; i++)
	{
        //If time is up stop
        double now = _unicode_clock_s();
		if(now - start >= max_time && i >= MIN_ITERS)
			break;

        //Determine the size of codepoint sequence to generate
        uint64_t state_before_iteration = random_state; (void) state_before_iteration;
        isize sequence_size = (isize) _unicode_random_splitmix(&random_state) & MAX_SIZE_MASK;

        //Worst cases
        isize max_utf32_size = sequence_size;
        isize max_utf16_size = sequence_size * 2;
        isize max_utf8_size = sequence_size * 4;

        //Allocate space for all roundtrips
        //Original sequence
        utf32_t* utf32 = (utf32_t*) calloc(max_utf32_size + 1, sizeof(utf32_t)); //+1 for null termination
        
        //Clockwise roundtrip
        utf16_t* utf16_cw = (utf16_t*) calloc(max_utf16_size + 1, sizeof(utf16_t));
        utf8_t*  utf8_cw = (utf8_t*) calloc(max_utf8_size + 1, sizeof(utf8_t));
        utf32_t* utf32_cw = (utf32_t*) calloc(max_utf32_size + 1, sizeof(utf32_t)); 
        
        //Counter-clockwise roundtrip
        utf8_t*  utf8_ccw = (utf8_t*) calloc(max_utf8_size + 1, sizeof(utf8_t));
        utf16_t* utf16_ccw = (utf16_t*) calloc(max_utf16_size + 1, sizeof(utf16_t));
        utf32_t* utf32_ccw = (utf32_t*) calloc(max_utf32_size + 1, sizeof(utf32_t)); 

        //There and back testing
        utf32_t* utf32_from_16 = (utf32_t*) calloc(max_utf32_size + 1, sizeof(utf32_t));
        utf32_t* utf32_from_8 = (utf32_t*) calloc(max_utf32_size + 1, sizeof(utf32_t));

        TEST(utf32 && utf16_cw && utf8_cw && utf32_cw && utf8_ccw && utf16_ccw && utf32_ccw, "OUT OF MEMORY!");

        //Fill a buffer with random codepoints and call that the utf32 sequence
        for(isize j = 0; j < sequence_size; j++)
        {
            //Generate a valid codepoint
            codepoint_t valid_codepoint = 0;
            while(true)
            {
                //UNICODE_MAX == 0x10FFFF means that we can use 0x1FFFFF
                //as a mask for our random value and almost guarantee that 
                //the reuslting codepoint will be valid
                #define _UNCIODE_TEST_MASK 0x1FFFFF
                ASSERT((_UNCIODE_TEST_MASK >= UNICODE_MAX) && ((UNICODE_MAX & _UNCIODE_TEST_MASK) == UNICODE_MAX));

                codepoint_t codepoint = (codepoint_t) (_unicode_random_splitmix(&random_state) & _UNCIODE_TEST_MASK);
                if(unicode_codepoint_is_valid(codepoint))
                {
                    valid_codepoint = codepoint;
                    break;
                }
            }

            ASSERT(unicode_codepoint_is_valid(valid_codepoint));
            ASSERT(utf32 != NULL);
            utf32[j] = valid_codepoint;
        }
        
        isize finished_at = 0;

        //Do the conversion rountrips. All conversions must not fail
        //Clockwise roundtrip
        isize utf16_cw_size = unicode_utf32_to_utf16(utf16_cw, max_utf16_size, utf32, sequence_size, &finished_at, UNICODE_ERROR); 
        TEST(finished_at == sequence_size, "Roundtrip conversion of valid codepoints must not fail!");

        isize utf8_cw_size = unicode_utf16_to_utf8(utf8_cw, max_utf8_size, utf16_cw, utf16_cw_size, &finished_at, UNICODE_ERROR);
        TEST(finished_at == utf16_cw_size, "Roundtrip conversion of valid codepoints must not fail!");

        isize utf32_cw_size = unicode_utf8_to_utf32(utf32_cw, max_utf32_size, utf8_cw, utf8_cw_size, &finished_at, UNICODE_ERROR);
        TEST(finished_at == utf8_cw_size, "Roundtrip conversion of valid codepoints must not fail!");
        
        //Counter-clockwise roundtrip
        isize utf8_ccw_size = unicode_utf32_to_utf8(utf8_ccw, max_utf8_size, utf32, sequence_size, &finished_at, UNICODE_ERROR); 
        TEST(finished_at == sequence_size, "Roundtrip conversion of valid codepoints must not fail!");

        isize utf16_ccw_size = unicode_utf8_to_utf16(utf16_ccw, max_utf16_size, utf8_ccw, utf8_ccw_size, &finished_at, UNICODE_ERROR);
        TEST(finished_at == utf8_ccw_size, "Roundtrip conversion of valid codepoints must not fail!");

        isize utf32_ccw_size = unicode_utf16_to_utf32(utf32_ccw, max_utf32_size, utf16_ccw, utf8_ccw_size, &finished_at, UNICODE_ERROR);
        TEST(finished_at == utf16_ccw_size, "Roundtrip conversion of valid codepoints must not fail!");
        
        //There and back
        isize utf32_from_8_size = unicode_utf8_to_utf32(utf32_from_8, max_utf32_size, utf8_ccw, utf8_ccw_size, &finished_at, UNICODE_ERROR); 
        TEST(finished_at == utf8_ccw_size, "Roundtrip conversion of valid codepoints must not fail!");
        
        isize utf32_from_16_size = unicode_utf16_to_utf32(utf32_from_16, max_utf32_size, utf16_cw, utf16_cw_size, &finished_at, UNICODE_ERROR); 
        TEST(finished_at == utf16_cw_size, "Roundtrip conversion of valid codepoints must not fail!");

        //The two roundtrips must match in all of the vertices.
        TEST(_unicode_are_memory_block_equal(utf8_cw, utf8_cw_size, utf8_ccw, utf8_ccw_size, sizeof(utf8_t)), "must match at all vertices!");
        TEST(_unicode_are_memory_block_equal(utf16_cw, utf16_cw_size, utf16_ccw, utf16_ccw_size, sizeof(utf16_t)), "must match at all vertices!");
        TEST(_unicode_are_memory_block_equal(utf32_cw, utf32_cw_size, utf32_ccw, utf32_ccw_size, sizeof(utf32_t)), "must match at all vertices!");

        //The original sequence must be perserved
        TEST(_unicode_are_memory_block_equal(utf32_cw, utf32_cw_size, utf32, sequence_size, sizeof(utf32_t)), "The original sequence must be perserved!");
        TEST(_unicode_are_memory_block_equal(utf32_from_16, utf32_from_16_size, utf32, sequence_size, sizeof(utf32_t)), "The original sequence must be perserved!");
        TEST(_unicode_are_memory_block_equal(utf32_from_8, utf32_from_8_size, utf32, sequence_size, sizeof(utf32_t)), "The original sequence must be perserved!");

        //Free all allocated data
        free(utf32);
        free(utf16_cw);
        free(utf8_cw);
        free(utf32_cw);
        free(utf8_ccw);
        free(utf16_ccw);
        free(utf32_ccw);
        free(utf32_from_16);
        free(utf32_from_8);
    }
}

//Generates next random value
//Seed can be any value
//Taken from: https://prng.di.unimi.it/splitmix64.c
static uint64_t _unicode_random_splitmix(uint64_t* state) 
{
	uint64_t z = (*state += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

static double _unicode_clock_s()
{
    return (double) clock() / (double) CLOCKS_PER_SEC;;
}

static bool _unicode_are_memory_block_equal(const void* a, isize a_size, const void* b, isize b_size, isize type_size)
{
    if(a_size != b_size)
        return false;

    bool state = memcmp(a, b, a_size*type_size) == 0;
    return state;
}

#ifdef printf
#undef printf
#endif