
#include "../utf.h"
#ifndef TEST
    #include <assert.h>
    #define TEST(x) assert(x)
#endif

//the most naive by-the-spec implementation I trust to be correct. It is as strict as possible.
bool utf8_decode_tester(const void* input, isize input_size, uint32_t* out_code_point, isize* index)
{
    uint8_t* in = (uint8_t*) input + *index;
    isize rem = input_size - *index;
    *out_code_point = (uint32_t)-1;

    if(rem < 1) {
        *out_code_point = 0;
        return false;
    }

    uint32_t code_point_len = 0;
    uint8_t first = in[0];
    uint32_t code_point = 0;
    if (first <= 0x7F) {
        code_point = first;
        code_point_len = 1;
    } 
    else if ((first & 0xE0) == 0xC0) {
        if(rem < 2) return false;

        if ((in[1] & 0xC0) != 0x80) 
            return false;

        code_point = ((uint32_t)(first & 0x1F) << 6) | (uint32_t)(in[1] & 0x3F);
        if (code_point < 0x0080 || code_point > 0x07FF)
            return false;

        code_point_len = 2;
    } 
    else if ((first & 0xF0) == 0xE0) {
        if(rem < 3) return false;
        if ((in[1] & 0xC0) != 0x80 || (in[2] & 0xC0) != 0x80) 
            return false;
        
        code_point = ((uint32_t)(first & 0x0F) << 12) | ((uint32_t)(in[1] & 0x3F) << 6) | (uint32_t)(in[2] & 0x3F);
        if (code_point < 0x0800 || code_point > 0xFFFF)
            return false;

        code_point_len = 3;
    } 
    else if ((first & 0xF8) == 0xF0) {
        if(rem < 4) return false;

        if ((in[1] & 0xC0) != 0x80 || (in[2] & 0xC0) != 0x80 || (in[3] & 0xC0) != 0x80) 
            return false;
        
        code_point = ((uint32_t)(first & 0x07) << 18) | ((uint32_t)(in[1] & 0x3F) << 12) | ((uint32_t)(in[2] & 0x3F) << 6) | (uint32_t)(in[3] & 0x3F);
        if (code_point < 0x010000 || code_point > 0x10FFFF)
            return false;
            
        code_point_len = 4;
    } 
    else {
        return false;
    }

    if (0xD800 <= code_point && code_point <= 0xDFFF) 
        return false;
        
    *index += code_point_len;
    *out_code_point = code_point;
    return true;
}


static void test_utf_decode_utf8(uint32_t bytes)
{
    union {
        uint32_t val;
        uint8_t ser[4];
    } caster = {(uint32_t) bytes};

    for(int len = 0; len <= sizeof(caster.ser); len++) {
        uint32_t tester_code_point = (uint32_t) -1;
        isize tester_index = 0;
        bool tester_ok = utf8_decode_tester(caster.ser, len, &tester_code_point, &tester_index);
        
        uint32_t tested_code_point = (uint32_t) -1;
        isize tested_index = 0;
        bool tested_ok = utf8_decode(caster.ser, len, &tested_code_point, &tested_index);

        if(len == 0)
            TEST(tester_code_point == 0);
        else
            TEST(tester_ok == utf_is_valid_codepoint(tester_code_point));
        TEST(tester_ok == tested_ok);
        TEST(tester_code_point == tested_code_point);
        TEST(tester_index == tested_index);
        TEST(tester_index == tested_index);
    }
}

static void test_utf_encode_utf8(uint32_t codepoint)
{
    uint32_t encoded_code_point = (uint32_t) codepoint;
    uint8_t encoded[4] = {(uint8_t) -1};
    isize encoded_index = 0;
    bool encoded_ok = utf8_encode(encoded, 4, encoded_code_point, &encoded_index);
    TEST(encoded_ok == utf_is_valid_codepoint(encoded_code_point));
    if(encoded_ok == false)
        encoded_index = 0;
    else {
        uint32_t decoded_code_point = (uint32_t) -1;
        isize decoded_index = 0;
        bool decoded_ok = utf8_decode(encoded, encoded_index, &decoded_code_point, &decoded_index);
        TEST(decoded_ok == encoded_ok);
        TEST(decoded_index == encoded_index);
        TEST(decoded_code_point == encoded_code_point);
    }

    for(int i = 0; i < encoded_index; i++) {
        isize encoded_index2 = 0;
        bool encoded_ok2 = utf8_encode(encoded, i, encoded_code_point, &encoded_index2);
        TEST(encoded_ok2 == false);
        TEST(encoded_index2 == 0);
    }
}

static void test_utf_roundtrip_utf16_utf32(uint32_t codepoint, bool is_utf32, uint32_t endian)
{
    uint32_t encoded_code_point = (uint32_t) codepoint;
    wchar_t encoded[4] = {(wchar_t) -1};
    isize encoded_index = 0;

    bool encoded_ok = is_utf32
        ? utf32_encode(encoded, 4, encoded_code_point, &encoded_index, endian)
        : utf16_encode(encoded, 4, encoded_code_point, &encoded_index, endian);
    TEST(encoded_ok == utf_is_valid_codepoint(encoded_code_point));
    if(encoded_ok == false)
        encoded_index = 0;
    else {
        uint32_t decoded_code_point = (uint32_t) -1;
        isize decoded_index = 0;
        bool decoded_ok = is_utf32
            ? utf32_decode(encoded, encoded_index, &decoded_code_point, &decoded_index, endian)
            : utf16_decode(encoded, encoded_index, &decoded_code_point, &decoded_index, endian);
        TEST(decoded_ok == utf_is_valid_codepoint(encoded_code_point));
        TEST(decoded_index == decoded_index);
        TEST(decoded_code_point == encoded_code_point);
    }

    for(int i = 0; i < encoded_index; i++) {
        isize encoded_index2 = 0;
        bool encoded_ok2 = is_utf32
            ? utf32_encode(encoded, i, encoded_code_point, &encoded_index2, endian)
            : utf16_encode(encoded, i, encoded_code_point, &encoded_index2, endian);
        TEST(encoded_ok2 == false);
        TEST(encoded_index2 == 0);
    }
}

#include <time.h>
#include <stdlib.h>
static void test_utf(double time_limit)
{
    uint32_t test_all_till = UINT16_MAX;
    // uint32_t test_all_till = UINT32_MAX; //can be enabled if we want to be thorough

    double start = (double) clock() / (double) CLOCKS_PER_SEC;
    for(uint32_t val = 0; ; val += 1) {
        test_utf_encode_utf8(val);
        test_utf_decode_utf8(val);
        if(val == test_all_till)
            break;
    }

    while(true) {
        double now = (double) clock() / (double) CLOCKS_PER_SEC;
        if(now - start > time_limit)
            break;

        uint32_t val = ((uint32_t) rand() & 0xFFFF) | ((uint32_t) rand() & 0xFFFF) << 16;
        uint32_t flags = (uint32_t) rand() & 0xFFFF;

        bool is_utf8 = (bool) (flags & 1);
        bool is_utf16_or_32 = (bool) (flags & 2);
        bool endian = (bool) (flags & 4);
        if(is_utf8) {
            test_utf_encode_utf8(val);
            test_utf_decode_utf8(val);
        }
        else {
            test_utf_roundtrip_utf16_utf32(val, is_utf16_or_32, endian);
        }
    }
    
}