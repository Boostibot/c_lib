#ifndef MODULE_MATCH
#define MODULE_MATCH

#include "defines.h"
#include "assert.h"
#include "string.h"

EXTERNAL bool match_any(String str, isize* index, isize count); //matches any character. Returns if *index + count <= str.count
EXTERNAL bool match_char(String str, isize* index, char c); //matches char c once. Returns true if matched
EXTERNAL bool match_chars(String str, isize* index, char c); //matches char c repeatedly. Returns true if at least one was matched
EXTERNAL bool match_one_of(String str, isize* index, String one_of); //matches any char of one_of once. Returns true if matched
EXTERNAL bool match_any_of(String str, isize* index, String any_of); //matches any char of any_of repeatedly. Returns true if at least one was matched
EXTERNAL bool match_string(String str, isize* index, String sequence);
inline static bool match_cstring(String str, isize* index, const char* sequence) { return match_string(str, index, string_of(sequence)); }

EXTERNAL bool match_not_char(String str, isize* index, char c);
EXTERNAL bool match_not_chars(String str, isize* index, char c);
EXTERNAL bool match_not_one_of(String str, isize* index, String any_of);
EXTERNAL bool match_not_any_of(String str, isize* index, String any_of);
EXTERNAL bool match_not_string(String str, isize* index, String sequence);

EXTERNAL bool match_space(String str, isize* index);
EXTERNAL bool match_alpha(String str, isize* index);
EXTERNAL bool match_upper(String str, isize* index);
EXTERNAL bool match_lower(String str, isize* index);
EXTERNAL bool match_digits(String str, isize* index);
EXTERNAL bool match_id_chars(String str, isize* index); //matches _ | [A-Z] | [a-z] | [0-9]

EXTERNAL bool match_not_space(String str, isize* index);
EXTERNAL bool match_not_alpha(String str, isize* index);
EXTERNAL bool match_not_upper(String str, isize* index);
EXTERNAL bool match_not_lower(String str, isize* index);
EXTERNAL bool match_not_digits(String str, isize* index);
EXTERNAL bool match_not_id_chars(String str, isize* index); 

EXTERNAL bool match_bool(String str, isize* index, bool* out); //matches "true" or "false"
EXTERNAL bool match_choice(String str, isize* index, bool* out, String if_true, String if_false);
EXTERNAL bool match_choices(String str, isize* index, isize* taken, const String* choices, isize choices_count);

//starts with _, [a-z], [A-Z] then is followed by any number of [0-9], _, [a-z], [A-Z]
EXTERNAL bool match_id(String str, isize* index); 

EXTERNAL bool match_decimal_u64(String str, isize* index, u64* out); //"00113000"   -> 113000
EXTERNAL bool match_decimal_i64(String str, isize* index, i64* out); //"-00113000"  -> -113000
EXTERNAL bool match_decimal_i32(String str, isize* index, i32* out); //"-00113000"  -> -113000
EXTERNAL bool match_decimal_f32(String str, isize* index, float* out); //"-0011.0300" -> -11.03000
EXTERNAL bool match_decimal_f64(String str, isize* index, double* out);

#if 1
//We want to match the following lines 3:
//[003]: "hello"  KIND_SMALL    -45.3 
//[431]: 'string' KIND_MEDIUM   131.3 
//[256]: "world"  KIND_BIG      1531.3 
// 
//We will do it in a single expression. 
//Of course this is little bit crazy and you probably should separate it
// into multiple ones but here just for the sake of the example we do it this way.
//Below is a version that is more sane and includes error checking.
typedef struct Match_Example_Result {
    double val;
    int64_t num;
    String id;
    int kind;
} Match_Example_Result;

bool match_example(String str, Match_Example_Result* result)
{
    isize i = 0;
    String kinds[3] = {
        STRING("KIND_BIG"),
        STRING("KIND_MEDIUM"),
        STRING("KIND_SMALL"),
    };

    double val = 0;
    uint64_t num = 0;
    int64_t id_from = 0;
    int64_t id_to = 0;
    int64_t kind = 0;
    bool ok = true
        && match_cstring(str, &i, "[")
        && match_decimal_u64(str, &i, &num)
        && match_cstring(str, &i, "]:")
        && (match_space(str, &i), true)
        && (false
            || (match_char(str, &i,         '"')
                && (id_from = i, true)
                && (match_not_chars(str, &i,'"'), true)
                && (id_to = i, true)
                && match_char(str, &i,      '"'))
            || (match_char(str, &i,         '\'')
                && (id_from = i, true)
                && (match_not_chars(str, &i,'\''), true)
                && (id_to = i, true)
                && match_char(str, &i,      '\''))
        )
        && (match_space(str, &i), true)
        && match_choices(str, &i, &kind, kinds, 3)
        && (match_space(str, &i), true)
        && match_decimal_f64(str, &i, &val);

    if(ok) {
        result->val = val;
        result->num = num;
        result->id = string_range(str, id_from, id_to);
        result->kind = (int) kind;
    }
    return ok;
}

bool match_example_with_errors(String str, Match_Example_Result* result, int* error_stage)
{
    isize i = 0;

    uint64_t num = 0;
    double val = 0;
    
    int64_t id_from = 0;
    int64_t id_to = 0;
    int64_t kind = 0;
    bool ok = false;
    do {
        if((match_cstring(str, &i, "[")
            && match_decimal_u64(str, &i, &num)
            && match_cstring(str, &i, "]:")) == false) 
        {
            *error_stage = 1;
            printf("bad start, expected '[' + [number] + ']'\n");
            break;
        }

        match_space(str, &i);

        char quote = 'c';
        if(match_char(str, &i, '"'))
            quote = '"';
        else if(match_char(str, &i, '\''))
            quote = '\'';
        else
        {
            *error_stage = 2;
            printf("unexpected character found while looking for start of id string\n");
            break;
        }

        id_from = i;
        match_not_chars(str, &i, quote);
        id_to = i;
        if(match_char(str, &i, quote) == false)
        {
            *error_stage = 2;
            printf("id string was not properly terminated found ...\n");
            break;
        }
        
        match_space(str, &i);
        if(0) {}
        else if(match_cstring(str, &i, "KIND_SMALL"))  kind = 0;
        else if(match_cstring(str, &i, "KIND_MEDIUM")) kind = 1;
        else if(match_cstring(str, &i, "KIND_BIG"))  kind = 2;
        else {
            *error_stage = 3;
            printf("Invalid enum option ...\n");
            break;
        }
        match_space(str, &i);

        if(match_decimal_f64(str, &i, &val) == false)
        {
            *error_stage = 4;
            printf("Could not parse float value...\n");
            break;
        }
        
        //save parsed variable...
        result->val = val;
        result->num = num;
        result->id = string_range(str, id_from, id_to);
        result->kind =(int) kind;
        ok = true;
    } while(0);

    return ok;
}
#endif

#endif

#define MODULE_IMPL_ALL

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_MATCH)) && !defined(MODULE_HAS_IMPL_MATCH)
#define MODULE_HAS_IMPL_MATCH

#if defined(_MSC_VER)
    #include <intrin.h>
    inline static int32_t swar_find_last_set(uint64_t num)
    {
        unsigned long out = 0;
        _BitScanReverse64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
    
    inline static int32_t swar_find_first_set(uint64_t num)
    {
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
    
    #define ATTRIBUTE_INLINE_NEVER  __declspec(noinline)
    #define ATTRIBUTE_INLINE_ALWAYS  __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    inline static int32_t swar_find_last_set(uint64_t num)
    {
        return 64 - __builtin_clzll((unsigned long long) num) - 1;
    }
    
    inline static int32_t swar_find_first_set(uint64_t num)
    {
        return __builtin_ffsll((long long) num) - 1;
    }

    #define ATTRIBUTE_INLINE_NEVER   __attribute__((noinline))
    #define ATTRIBUTE_INLINE_ALWAYS  __attribute__((inline)) inline
#else
    #error unsupported compiler!

    #define ATTRIBUTE_INLINE_NEVER   
    #define ATTRIBUTE_INLINE_ALWAYS  inline
#endif

//from https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
#define SWAR8_L 0x0101010101010101ull
#define SWAR8_H 0x8080808080808080ull
inline static uint64_t swar8_has_less(uint64_t x, uint64_t n)  { return ((x-SWAR8_L*n) & ~x & SWAR8_H); }
inline static uint64_t swar8_has_more(uint64_t x, uint64_t n)  { return ((x+SWAR8_L*(127-n)|x) & SWAR8_H); }
inline static uint64_t swar8_has_zero(uint64_t x)              { return ((x-SWAR8_L) & ~x & SWAR8_H); }
inline static uint64_t swar8_has_equal(uint64_t x, uint64_t n) { return swar8_has_zero(x ^ n*SWAR8_L); }

//from https://stackoverflow.com/a/68717720
inline static uint64_t swar8_is_less_sign(uint64_t a, uint64_t b)
{
    uint64_t c = ~a;
    //Peter L. Montgomery's observation (newsgroup comp.arch, 2000/02/11,
    //https://groups.google.com/d/msg/comp.arch/gXFuGZtZKag/_5yrz2zDbe4J):
    //(A+B)/2 = (A AND B) + (A XOR B)/2.
    uint64_t hadd = (c & b) + (((c ^ b) >> 1) & ~SWAR8_H); //(c + b)/2
    return hadd;
}

//from https://stackoverflow.com/a/68701617
inline static uint64_t swar8_is_equal_sign(uint64_t x, uint64_t y) {
    uint64_t xored = x ^ y;
    uint64_t mask = ((((xored >> 1) | SWAR8_H) - xored) & SWAR8_H);
    return mask;
}

inline static uint64_t swar8_is_not_equal_sign(uint64_t x, uint64_t y) {
    return swar8_is_less_sign(0, x ^ y);
}

//from https://stackoverflow.com/a/68717720
inline static uint64_t swar8_sign_to_mask(uint64_t a)
{
    uint64_t s = a & SWAR8_H;    
    uint64_t m = s + s - (s >> 7); 
    return m;
}

EXTERNAL bool match_any(String str, isize* index, isize count)
{
    if(*index + count <= str.count)
    {
        *index += count;
        return true;
    }
    return false;
}

EXTERNAL bool _match_char(String str, isize* index, char c, bool positive)
{
    if(*index < str.count && (str.data[*index] == c) == positive)
    {
        *index += 1;
        return true;
    }
    return false;
}

EXTERNAL bool _match_chars(String str, isize* index, char chars, bool positive)
{
    isize start = *index;
    isize i = start;
    if(positive) {
        uint64_t pattern = SWAR8_L*chars;
        for(; i + 8 <= str.count; i += 8) {
            uint64_t c; memcpy(&c, str.data + i, 8);
            uint64_t diff = c ^ pattern;
            if(diff) {
                uint64_t diff_i = swar_find_first_set(diff);
                i += diff_i/8;
                goto end;
            }
        }

        for(; i < str.count; i ++) 
            if(str.data[i] != chars)
                goto end;
    }
    else {
        const void* ptr = memchr(str.data + i, chars, (size_t) str.count - i);
        i = ptr ? (const char*) ptr - str.data : str.count;
    }

    end:
    *index = i;
    return i == start;
}

EXTERNAL bool _match_one_of(String str, isize* index, String one_of, bool positive)
{
    isize i = *index;
    if(i < str.count)
    {
        char c = str.data[i];
        for(isize j = 0; j < one_of.count; j++)
            if((one_of.data[j] == c) == positive)
            {
                *index = i + 1;
                return true;
            }
    }
    return false;
}
EXTERNAL bool _match_any_of(String str, isize* index, String any_of, bool positive)
{
    isize start = *index;
    while(_match_one_of(str, index, any_of, positive));
    return *index == start;
}

EXTERNAL bool _match_sequence(String str, isize* index, String sequence, bool positive)
{
    if(string_has_substring_at(str, sequence, *index) == positive)
    {
        *index += sequence.count;
        return true;
    }
    return false;   
}
ATTRIBUTE_INLINE_ALWAYS
static bool _match_char_category(String str, isize* index, bool (*is_category_char)(char c), bool positive)
{
    isize start = *index;
    for(; *index < str.count; (*index)++) 
        if(is_category_char(str.data[*index]) != positive) 
            break;
    
    return *index == start;
}

#define MATCH_DO_SWAR
ATTRIBUTE_INLINE_ALWAYS
static bool _match_char_category_swar(String str, isize* index, bool (*is_category_char)(char c), uint64_t (*is_not_category_swar)(uint64_t c), bool positive)
{
    isize start = *index;
    isize i = start;

    (void) is_not_category_swar;
    #ifdef MATCH_DO_SWAR
    for(;i + 8 <= str.count; i += 8) {
        uint64_t c; memcpy(&c, str.data + i, 8);
        uint64_t is_not_category_sign = is_not_category_swar(c);
        uint64_t is_not_category_mask = swar8_sign_to_mask(is_not_category_sign);
        uint64_t sign_mask = positive ? is_not_category_mask : ~is_not_category_mask;
        if(sign_mask) {
            uint64_t found_bit = swar_find_first_set(sign_mask);
            i += found_bit/8;
            *index = i;
            return i == start;
        }
    }
    #endif

    for(; i < str.count; i++) 
        if(is_category_char(str.data[i]) != positive) 
            break;
    
    *index = i;
    return i == start;
}

ATTRIBUTE_INLINE_ALWAYS static uint64_t _match_swar_is_not_upper(uint64_t c)
{
    return swar8_is_less_sign(c, SWAR8_L*'A') | swar8_is_less_sign(SWAR8_L*'Z', c);
}

ATTRIBUTE_INLINE_ALWAYS static uint64_t _match_swar_is_not_lower(uint64_t c)
{
    return swar8_is_less_sign(c, SWAR8_L*'a') | swar8_is_less_sign(SWAR8_L*'z', c);
}

ATTRIBUTE_INLINE_ALWAYS static uint64_t _match_swar_is_not_digit(uint64_t c)
{
    return swar8_is_less_sign(c, SWAR8_L*'0') | swar8_is_less_sign(SWAR8_L*'9', c);
}

ATTRIBUTE_INLINE_ALWAYS static uint64_t _match_swar_is_not_alpha(uint64_t c)
{
    uint8_t single_mask = (uint8_t)~(uint8_t) (1 << 5);
    uint64_t masked = c & (SWAR8_L*single_mask);
    return swar8_is_less_sign(masked, SWAR8_L*'A') | swar8_is_less_sign(SWAR8_L*'Z', masked);
}

ATTRIBUTE_INLINE_ALWAYS static uint64_t _match_swar_is_not_space(uint64_t c)
{
    uint64_t is_not_space = swar8_is_not_equal_sign(c, SWAR8_L*' ');
    uint64_t is_not_special =  swar8_is_less_sign(c, SWAR8_L*'\t') | swar8_is_less_sign(SWAR8_L*'\r', c);
    return is_not_space & is_not_special;
}
ATTRIBUTE_INLINE_ALWAYS static uint64_t _match_swar_is_not_id_body_char(uint64_t c)
{
    uint64_t is_not_under = swar8_is_not_equal_sign(c, SWAR8_L*'_');
    uint64_t is_not_alpha = _match_swar_is_not_alpha(c);
    uint64_t is_not_digit = _match_swar_is_not_digit(c);
    return is_not_under & is_not_alpha & is_not_digit;
}

ATTRIBUTE_INLINE_ALWAYS static bool _match_is_id_body_char(char c)
{
    return char_is_alpha(c) || char_is_digit(c) || c == '_';
}

EXTERNAL bool match_char(String str, isize* index, char c)             { return _match_char(str, index, c, true); }
EXTERNAL bool match_chars(String str, isize* index, char c)            { return _match_chars(str, index, c, true); }
EXTERNAL bool match_any_of(String str, isize* index, String any_of)    { return _match_any_of(str, index, any_of, true); }
EXTERNAL bool match_one_of(String str, isize* index, String one_of)    { return _match_one_of(str, index, one_of, true);}
EXTERNAL bool match_string(String str, isize* index, String sequence)  { return _match_sequence(str, index, sequence, true);}

EXTERNAL bool match_not_char(String str, isize* index, char c)             { return _match_char(str, index, c, false); }
EXTERNAL bool match_not_chars(String str, isize* index, char c)            { return _match_chars(str, index, c, false); }
EXTERNAL bool match_not_any_of(String str, isize* index, String any_of)    { return _match_any_of(str, index, any_of, false); }
EXTERNAL bool match_not_one_of(String str, isize* index, String one_of)    { return _match_one_of(str, index, one_of, false);}
EXTERNAL bool match_not_string(String str, isize* index, String sequence)  { return _match_sequence(str, index, sequence, false);}

EXTERNAL bool match_space(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_space, _match_swar_is_not_space, true); } 
EXTERNAL bool match_alpha(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_alpha, _match_swar_is_not_alpha, true); } 
EXTERNAL bool match_upper(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_upper, _match_swar_is_not_upper, true); } 
EXTERNAL bool match_lower(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_lower, _match_swar_is_not_lower, true); } 
EXTERNAL bool match_digits(String str, isize* index)    { return _match_char_category_swar(str, index, char_is_digit, _match_swar_is_not_digit, true); }
EXTERNAL bool match_id_chars(String str, isize* index)  { return _match_char_category_swar(str, index, _match_is_id_body_char, _match_swar_is_not_id_body_char, true); }

EXTERNAL bool match_not_space(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_space, _match_swar_is_not_space, false); } 
EXTERNAL bool match_not_alpha(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_alpha, _match_swar_is_not_alpha, false); } 
EXTERNAL bool match_not_upper(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_upper, _match_swar_is_not_upper, false); } 
EXTERNAL bool match_not_lower(String str, isize* index)     { return _match_char_category_swar(str, index, char_is_lower, _match_swar_is_not_lower, false); } 
EXTERNAL bool match_not_digits(String str, isize* index)    { return _match_char_category_swar(str, index, char_is_digit, _match_swar_is_not_digit, false); }
EXTERNAL bool match_not_id_chars(String str, isize* index)  { return _match_char_category_swar(str, index, _match_is_id_body_char, _match_swar_is_not_id_body_char, false); }

EXTERNAL bool match_id(String str, isize* index)
{
    if(*index < str.count)
        if(char_is_alpha(str.data[*index]) || str.data[*index] == '_')
        {
            *index += 1;
            match_id_chars(str, index);
            return true;
        }
    return false;
}

EXTERNAL bool match_choice(String str, isize* index, bool* out, String if_true, String if_false)
{
    if(match_string(str, index, if_true))
        *out = true;
    if(match_string(str, index, if_false))
        *out = false;
    else
        return false;
    return true;
}

EXTERNAL bool match_bool(String str, isize* index, bool* out)
{
    return match_choice(str, index, out, STRING("true"), STRING("false"));
}

EXTERNAL bool match_choices(String str, isize* index, isize* taken, const String* choices, isize choices_count)
{
    for(isize i = 0; i < choices_count; i++)
        if(match_string(str, index, choices[i]))
        {
            *taken = i;
            return true;
        }
    return false;
}

//matches a sequence of digits in decimal: "00113000" -> 113000
EXTERNAL bool match_decimal_u64(String str, isize* index, u64* out)
{
    u64 parsed = 0;
    isize i = *index;
    for(; i < str.count; i++)
    {
        u64 digit_value = (u64) str.data[i] - (u64) '0';
        if(digit_value > 9)
            break;

        u64 new_parsed = parsed*10 + digit_value;

        //Correctly handle overflow. This is correct because unsigned numbers
        //have defined overflow
        if(new_parsed < parsed)
            parsed = UINT64_MAX;
        else
            parsed = new_parsed;

    }

    bool matched = i != *index;
    *index = i;

    ASSERT(parsed <= UINT64_MAX);
    *out = (u64) parsed;
    return matched;
}

//matches a sequence of signed digits in decimal: "-00113000" -> -113000
EXTERNAL bool match_decimal_i64(String str, isize* index, i64* out)
{
    u64 uout = 0;
    bool has_minus = match_char(str, index, '-');
    bool matched_number = match_decimal_u64(str, index, &uout);
    if(uout > INT64_MAX)
        uout = INT64_MAX;

    if(has_minus)
        *out = - (i64) uout;
    else
        *out = (i64) uout;

    return matched_number;
}

EXTERNAL bool match_decimal_i32(String str, isize* index, i32* out)
{
    u64 uout = 0;
    bool has_minus = match_char(str, index, '-');
    bool matched_number = match_decimal_u64(str, index, &uout);
    if(uout > INT32_MAX)
        uout = INT32_MAX;

    if(has_minus)
        *out = - (i32) uout;
    else
        *out = (i32) uout;

    return matched_number;
}

#include <math.h>
EXTERNAL bool match_decimal_f64(String str, isize* index, double* out)
{
    u64 before_dot = 0;
    u64 after_dot = 0;
    
    bool has_minus = match_char(str, index, '-');
    bool matched_before_dot = match_decimal_u64(str, index, &before_dot);
    if(!matched_before_dot)
        return false;

    match_char(str, index, '.');

    isize decimal_index = *index;
    match_decimal_u64(str, index, &after_dot);

    isize decimal_size = *index - decimal_index;
    double decimal_pow = 0;
    switch(decimal_size)
    {
        case 0: decimal_pow = 1.0; break;
        case 1: decimal_pow = 10.0; break;
        case 2: decimal_pow = 100.0; break;
        case 3: decimal_pow = 1000.0; break;
        case 4: decimal_pow = 10000.0; break;
        case 5: decimal_pow = 100000.0; break;
        default: decimal_pow = pow(10.0, (double) decimal_size); break;
    };

    double result = (double) before_dot + (double) after_dot / decimal_pow;
    if(has_minus)
        result = -result;

    *out = result;
    return true;
}

EXTERNAL bool match_decimal_f32(String str, isize* index, float* out)
{
    double wider = 0;
    if(match_decimal_f64(str, index, &wider) == false)
        return false;

    *out = (float) wider;
    return true;
}


static void test_match_category(String str, bool (*is_category_char)(char c), uint64_t (*is_not_category_swar)(uint64_t c), bool positive, isize expected)
{
    isize i1 = 0;
    isize i2 = 0;
    bool  b1 = _match_char_category(str, &i1, is_category_char, positive);
    bool  b2 = _match_char_category_swar(str, &i2, is_category_char, is_not_category_swar, positive);

    TEST(b1 == b2 && i1 == i2);
    TEST(expected < 0 || expected == i1);
}

static void test_match_all_categories(String input)
{
    for(isize i = 0; i < input.count; i++)
        for(int k = 0; k < 2; k++)
        {
            test_match_category(string_tail(input, i), char_is_space, _match_swar_is_not_space, k == 0, -1); 
            test_match_category(string_tail(input, i), char_is_alpha, _match_swar_is_not_alpha, k == 0, -1); 
            test_match_category(string_tail(input, i), char_is_upper, _match_swar_is_not_upper, k == 0, -1); 
            test_match_category(string_tail(input, i), char_is_lower, _match_swar_is_not_lower, k == 0, -1); 
            test_match_category(string_tail(input, i), char_is_digit, _match_swar_is_not_digit, k == 0, -1);
            test_match_category(string_tail(input, i), _match_is_id_body_char, _match_swar_is_not_id_body_char, k == 0, -1);
        }
}

//[003]: "hello"  KIND_SMALL    -45.3 
//[431]: 'string' KIND_MEDIUM   131.3 
//[256]: "world"  KIND_BIG      1531.3 
static void test_ok_example(const char* input, int64_t num, const char* id, int kind, double val)
{
    double epsilon = 1e-8;
    String str = string_of(input);
    Match_Example_Result result = {0};
    bool b1 = match_example(str, &result);
    int found_error_stage = 0;
    bool b2 = match_example_with_errors(str, &result, &found_error_stage);

    TEST(b1 == b2 && b1 == true);
    TEST(result.num == num);
    TEST(result.kind == kind);
    TEST(fabs(result.val - val) < epsilon);
    TEST(string_is_equal(result.id, string_of(id)));
}

static void test_failed_example(const char* input, int error_stage)
{
    String str = string_of(input);
    Match_Example_Result result = {0};
    bool b1 = match_example(str, &result);
    int found_error_stage = 0;
    bool b2 = match_example_with_errors(str, &result, &found_error_stage);

    TEST(b1 == false && b2 == false);
    TEST(error_stage == found_error_stage);
}

static inline uint64_t _match_random_xiroshiro256(uint64_t s[4]) 
{
	#define ROTL(x, k) (((x) << (k)) | ((x) >> (64 - (k))))

	const uint64_t result = ROTL(s[0] + s[3], 23) + s[0];
	const uint64_t t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;
	s[3] = ROTL(s[3], 45);

	return result;
	#undef ROTL
}

#include "time.h"
static void test_match()
{
    TEST(swar8_is_equal_sign(
        0xAABB00CC01FF1302ull, 
        0xAE7B00CC01FF1402ull) == 
        0x0000808080800080ull);

    test_match_category(STRING("    a           "), char_is_space, _match_swar_is_not_space, true, 4);
    test_match_category(STRING("  \n\r ........."), char_is_space, _match_swar_is_not_space, true, 5);
    test_match_category(STRING("  \n\r a........"), char_is_space, _match_swar_is_not_space, true, 5);
    test_match_category(STRING("  x\n\r a......."), char_is_space, _match_swar_is_not_space, true, 2);
    
    test_match_category(STRING("  x\n\r a......."), char_is_space, _match_swar_is_not_space, false, 0);
    test_match_category(STRING("x1?\n\r xxa....."), char_is_space, _match_swar_is_not_space, false, 3);

    test_match_all_categories(STRING(""));
    test_match_all_categories(STRING(" 13545 aaa hello_world2564 \n\r  AAAHCGKJAHKajha45531zfakhg   \v\f "));
    test_match_all_categories(STRING(" 1354 iiuq0  9uk 1' ] [1o. 1\';;'\'; a   \f\v d2564 \n\r  AA45ag534g35a4XXXXXXXXX354af8y8y79uh45ht   8952; u7;y 5 u9\r 4 g   \v\f "));
    
    if(0)
    {
        double before = clock_sec();
        {
            uint64_t rand_state[4] = {1, 1, 1, 1};
            enum {RAND_ROUNDS = 128};
            for(int i = 0; i < 10000; i++)
            {
                uint64_t rand_text[RAND_ROUNDS] = {0};
                for(int k = 0; k < RAND_ROUNDS; k++)
                    rand_text[k] = _match_random_xiroshiro256(rand_state);

                test_match_all_categories(string_make((char*) rand_text, sizeof rand_text));
            }
        }
        double after = clock_sec();
        printf("took %lfs\n", after-before);
    }
    
    {
        String_Builder builder = builder_make(allocator_get_default(), 0);
        builder_resize(&builder, 1000000);
        memset(builder.data, ' ', builder.count);

        volatile isize sum = 0;
        double before = clock_sec();
        for(int i = 0; i < 10000; i++) {
            isize k = 0;
            sum += match_space(builder.string, &k);
        }
        double after = clock_sec();
        printf("space %lfs\n", after-before);

        builder_deinit(&builder);
    }

    test_ok_example("[003]: \"hello\"  KIND_SMALL    -45.3 ",        3, "hello", 0, -45.3);
    test_ok_example("[431]: 'string'   KIND_MEDIUM   131.3   xxx",   431, "string", 1, 131.3);
    test_ok_example("[256]: \"world\"  KIND_BIG      1531.3  51854", 256, "world", 2, 1531.3);
    test_ok_example("[0]:\"\"KIND_SMALL-0", 0, "", 0, 0);
    
    test_failed_example("[]:    \"hello\"  KIND_SMALL    -45.3 ",        1);
    test_failed_example("[003:  \"hello\"  KIND_SMALL    -45.3 ",        1);
    test_failed_example("[431]: string'    KIND_MEDIUM   131.3   xxx",   2);
    test_failed_example("[431]: 'string    KIND_MEDIUM   131.3   xxx",   2);
    test_failed_example("[431]: string\"   KIND_MEDIUM   131.3   xxx",   2);
    test_failed_example("[431]: \"string   KIND_MEDIUM   131.3   xxx",   2);
    test_failed_example("[256]: \"world\"  _BIG          1531.3  51854", 3);
    test_failed_example("[256]: \"world\"  9KIND_SMALL   1531.3  51854", 3);
    test_failed_example("[256]: \"world\"  KIND_SMALLL   1531.3  51854", 4);
    test_failed_example("[256]: \"world\"  KIND_MEDIUMHH 1531.3  51854", 4);
    test_failed_example("[256]: \"world\"  KIND_SMALL    +1531.3  51854", 4);
    test_failed_example("[256]: \"world\"  KIND_MEDIUM   inf ", 4);
    test_failed_example("[256]: \"world\"  KIND_MEDIUM   a", 4);
    test_failed_example("[256]: \"world\"  KIND_MEDIUM   ", 4);
}

#endif