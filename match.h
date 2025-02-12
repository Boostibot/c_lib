#ifndef MODULE_MATCH
#define MODULE_MATCH

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "string.h"

//A file for simple, fast and convenient parsing. See the bottom of the header section for a working example.
//Note that the floating point parsing is perfectly accurate (well at least from the little testing I have done).

EXTERNAL bool match_any(String str, isize* index, isize count); //matches any character. Returns if *index + count <= str.count
EXTERNAL bool match_char(String str, isize* index, char c); //matches char c once. Returns true if matched
EXTERNAL bool match_chars(String str, isize* index, char c); //matches char c repeatedly. Returns true if at least one was matched
EXTERNAL bool match_one_of(String str, isize* index, String one_of); //matches any char of one_of once. Returns true if matched
EXTERNAL bool match_any_of(String str, isize* index, String any_of); //matches any char of any_of repeatedly. Returns true if at least one was matched
EXTERNAL bool match_string(String str, isize* index, String sequence); //matches the exact sequence. The sequence needs to match as one part
inline static bool match_cstring(String str, isize* index, const char* sequence) { return match_string(str, index, string_of(sequence)); }

//these functions match the appropriate char_is_xxxxx repeatedly and return true if at least one char was matched. 
EXTERNAL bool match_space(String str, isize* index);    
EXTERNAL bool match_alpha(String str, isize* index);
EXTERNAL bool match_upper(String str, isize* index);
EXTERNAL bool match_lower(String str, isize* index);
EXTERNAL bool match_digits(String str, isize* index);
EXTERNAL bool match_id_chars(String str, isize* index); //matches _ | [A-Z] | [a-z] | [0-9]

//starts with _, [a-z], [A-Z] then is followed by any number of [0-9], _, [a-z], [A-Z]
EXTERNAL bool match_id(String str, isize* index); 

//the "not" prefix functions match the opposite of their regular counterparts. 
//For example match_not_char matches every single character except for the one provided
EXTERNAL bool match_not_char(String str, isize* index, char c);
EXTERNAL bool match_not_chars(String str, isize* index, char c);
EXTERNAL bool match_not_one_of(String str, isize* index, String any_of);
EXTERNAL bool match_not_any_of(String str, isize* index, String any_of);
EXTERNAL bool match_not_string(String str, isize* index, String sequence);

EXTERNAL bool match_not_space(String str, isize* index);
EXTERNAL bool match_not_alpha(String str, isize* index);
EXTERNAL bool match_not_upper(String str, isize* index);
EXTERNAL bool match_not_lower(String str, isize* index);
EXTERNAL bool match_not_digits(String str, isize* index);
EXTERNAL bool match_not_id_chars(String str, isize* index); 

EXTERNAL bool match_bool(String str, isize* index, bool* out); //matches "true" or "false" and indicates out respectively
EXTERNAL bool match_choice(String str, isize* index, bool* out, String if_true, String if_false); //matches either of the strings and indicates which one
EXTERNAL bool match_choices(String str, isize* index, isize* taken, const String* choices, isize choices_count); //matches one of the strings and indicates its 0-based index

//Matching of decimal numbers. These functions are by default very strict and dont allow things like
// leading plus, leading zeroes, leading dot, trailing dot, inf, nan...
//When the number doesnt fit into the destination type report error. 
// The specific behaviour can be configured by using the _option variants with the appropriate flags
EXTERNAL bool match_decimal_u64(String str, isize* index, uint64_t* out); //matches numbers like "113000"   -> 113000. 
EXTERNAL bool match_decimal_i64(String str, isize* index, int64_t* out); //matches numbers like "-113000"  -> -113000
EXTERNAL bool match_decimal_f64(String str, isize* index, double* out); //matches numbers like "-11.0300" -> -11.03000
EXTERNAL bool match_decimal_u32(String str, isize* index, uint32_t* out); //matches numbers like "113000"  -> 113000
EXTERNAL bool match_decimal_i32(String str, isize* index, int32_t* out); //matches numbers like "-113000"  -> -113000
EXTERNAL bool match_decimal_f32(String str, isize* index, float* out);  //matches numbers like "-11.0300" -> -11.03000

#define MATCH_NUM_ALLOW_LEADING_ZEROS   1  //allows numbers like "0001"
#define MATCH_NUM_MINUS                 2  //allows numbers like "-10" 
#define MATCH_NUM_PLUS                  4  //allows numbers like "+10"
#define MATCH_NUM_CLAMP_TO_RANGE        8  //when the number doesnt fit into the destination type, clamps it to it (ie will return UINT64_MAX instead of failure)
#define MATCH_NUM_DISALLOW_DOT          16 //disallows floating point numbers with a dot - the resulting numbers are integers but with arbitrarily large exponent
#define MATCH_NUM_ALLOW_LEADING_DOT     32 //allows numbers like ".5"
#define MATCH_NUM_ALLOW_TRAILING_DOT    64 //allows numbers like "5." - note that even in conjuction with MATCH_NUM_ALLOW_LEADING_DOT "." is still invalid
EXTERNAL bool match_decimal_f64_options(String str, isize* index, double* out, uint32_t flags);
EXTERNAL bool match_decimal_i64_options(String str, isize* index, int64_t* out, uint32_t flags);
EXTERNAL bool match_decimal_u64_options(String str, isize* index, uint64_t* out, uint32_t flags);

typedef struct Matched_Number {
    uint64_t mantissa;
    isize exponent;
    bool is_negative;
} Matched_Number ;
EXTERNAL bool match_decimal_number_int_part(String str, isize* index, Matched_Number* out, uint32_t flags);
EXTERNAL bool match_decimal_number_frac_part(String str, isize* index, Matched_Number* in_out);
EXTERNAL double match_decimal_number_convert(Matched_Number number);

//We want to match the following 3 lines:
//[003]: "hello"  KIND_SMALL    -45.3 
//[431]: 'string' KIND_MEDIUM   131.3 
//[256]: "world"  KIND_BIG      1531.3 
// 
//We will do it in a single expression. 
//Of course this is little bit crazy and you probably should separate it
// into multiple ones but here just for the sake of the example we do it this way.
typedef struct Match_Example_Result {
    double val;
    int64_t num;
    String id;
    int kind;
} Match_Example_Result;

static inline bool match_example(String str, Match_Example_Result* result)
{
    isize i = 0;
    String kinds[3] = {
        STRING("KIND_SMALL"),
        STRING("KIND_MEDIUM"),
        STRING("KIND_BIG"),
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

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_MATCH)) && !defined(MODULE_HAS_IMPL_MATCH)
#define MODULE_HAS_IMPL_MATCH

inline static bool _match_char(String str, isize* index, char c, bool positive)
{
    if(*index < str.count && (str.data[*index] == c) == positive)
    {
        *index += 1;
        return true;
    }
    return false;
}

inline static bool _match_chars(String str, isize* index, char chars, bool positive)
{
    isize start = *index;
    isize i = start;
    if(positive) {
        uint64_t pattern = 0x0101010101010101ull*chars;
        for(; i + 8 <= str.count; i += 8) {
            uint64_t c; memcpy(&c, str.data + i, 8);
            uint64_t diff = c ^ pattern;
            if(diff) 
                break;
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

inline static bool _match_one_of(String str, isize* index, String one_of, bool positive)
{
    isize i = *index;
    if(i < str.count) {
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
inline static bool _match_any_of(String str, isize* index, String any_of, bool positive)
{
    isize start = *index;
    while(_match_one_of(str, index, any_of, positive));
    return *index == start;
}

inline static bool _match_sequence(String str, isize* index, String sequence, bool positive)
{
    if(*index + sequence.count <= str.count) 
    {
        if((memcmp(str.data + *index, sequence.data, sequence.count) == 0) == positive) {
            *index += sequence.count;
            return true;
        }
    }
    return false;   
}
inline static bool _match_char_category(String str, isize* index, bool (*is_category_char)(char c), bool positive)
{
    isize start = *index;
    for(; *index < str.count; (*index)++) 
        if(is_category_char(str.data[*index]) != positive) 
            break;
    
    return *index == start;
}

inline static bool _match_is_id_body_char(char c)
{
    return char_is_alpha(c) || char_is_digit(c) || c == '_';
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

EXTERNAL bool match_space(String str, isize* index)     { return _match_char_category(str, index, char_is_space, true); } 
EXTERNAL bool match_alpha(String str, isize* index)     { return _match_char_category(str, index, char_is_alpha, true); } 
EXTERNAL bool match_upper(String str, isize* index)     { return _match_char_category(str, index, char_is_upper, true); } 
EXTERNAL bool match_lower(String str, isize* index)     { return _match_char_category(str, index, char_is_lower, true); } 
EXTERNAL bool match_digits(String str, isize* index)    { return _match_char_category(str, index, char_is_digit, true); }
EXTERNAL bool match_id_chars(String str, isize* index)  { return _match_char_category(str, index, _match_is_id_body_char, true); }

EXTERNAL bool match_not_space(String str, isize* index)     { return _match_char_category(str, index, char_is_space, false); } 
EXTERNAL bool match_not_alpha(String str, isize* index)     { return _match_char_category(str, index, char_is_alpha, false); } 
EXTERNAL bool match_not_upper(String str, isize* index)     { return _match_char_category(str, index, char_is_upper, false); } 
EXTERNAL bool match_not_lower(String str, isize* index)     { return _match_char_category(str, index, char_is_lower, false); } 
EXTERNAL bool match_not_digits(String str, isize* index)    { return _match_char_category(str, index, char_is_digit, false); }
EXTERNAL bool match_not_id_chars(String str, isize* index)  { return _match_char_category(str, index, _match_is_id_body_char, false); }

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


//Numbers
#include <math.h>
EXTERNAL bool match_decimal_number_int_part(String str, isize* index, Matched_Number* out, uint32_t flags)
{
    isize i = *index;
    Matched_Number floating = {0};

    //handle prefix
    if(flags & MATCH_NUM_MINUS & MATCH_NUM_PLUS)
    {
        if(match_char(str, &i, '-')) 
            floating.is_negative = true;
        else 
            match_char(str, &i, '+');
    }
    else if(flags & MATCH_NUM_MINUS) 
        floating.is_negative = match_char(str, &i, '-');
    else if(flags & MATCH_NUM_PLUS) 
        match_char(str, &i, '+');

    //handle first char - needs to be present else error
    if(i >= str.count)
        return false;
    {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            return false;

        floating.mantissa = floating.mantissa*10 + digit_value;
        i += 1;
    }

    //handle second char - if not allowing leading zeros dont allow first to be 0
    if(i >= str.count)
        goto end;
    {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            goto end;
        
        if((flags & MATCH_NUM_ALLOW_LEADING_ZEROS) == 0 && floating.mantissa == 0)
            return false;

        floating.mantissa = floating.mantissa*10 + digit_value;
        i += 1;
    }
    
    //handle up to 16 more digits without having to worry about overflow
    for(int j = 0; j < 4; j++) 
    {
        if(i + 4 > str.count)
            break;

        u8 d0 = (u8) str.data[i+0] - (u8) '0';
        u8 d1 = (u8) str.data[i+1] - (u8) '0';
        u8 d2 = (u8) str.data[i+2] - (u8) '0';
        u8 d3 = (u8) str.data[i+3] - (u8) '0';
        
        if(d0 > 9) { i += 0; goto end; } floating.mantissa = floating.mantissa*10 + d0;
        if(d1 > 9) { i += 1; goto end; } floating.mantissa = floating.mantissa*10 + d1;
        if(d2 > 9) { i += 2; goto end; } floating.mantissa = floating.mantissa*10 + d2;
        if(d3 > 9) { i += 3; goto end; } floating.mantissa = floating.mantissa*10 + d3;
        i += 4;
    }

    //big numbers need to handle overflow gracefully
    for(; i < str.count; i++)
    {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            goto end;

        if(floating.mantissa > UINT64_MAX/10)
            floating.exponent += 1;
        else
            floating.mantissa = floating.mantissa*10 + digit_value;
    }

    end:
    *index = i;
    *out = floating;
    return true;
}

EXTERNAL bool match_decimal_number_frac_part(String str, isize* index, Matched_Number* in_out)
{
    isize start = *index;
    isize i = start; 
    for(; i < str.count; i++)
    {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            break;

        if(in_out->mantissa <= UINT64_MAX/10) {
            in_out->exponent -= 1;
            in_out->mantissa = in_out->mantissa*10 + digit_value;
        }
    }

    *index = i;
    return i != start;
}

EXTERNAL double match_decimal_number_convert(Matched_Number number)
{
    static double pow10[] = {
         1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9, 1e10,
        1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20,
        1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29, 1e30,
        1e31, 1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39, 1e40,
    };

    double result = (double) number.mantissa;
    if(number.exponent != 0)
    {
        isize abs_dec_places = number.exponent < 0 ? -number.exponent : number.exponent;
        double decimal_pow = 0;
        if(abs_dec_places <= 40)
            decimal_pow = pow10[abs_dec_places - 1];
        else
            decimal_pow = pow(10, (double) abs_dec_places);

        if(number.exponent < 0)
            result /= decimal_pow;
        else
            result *= decimal_pow;
    }
    if(number.is_negative)
        result = -result;
    return result;
}

EXTERNAL bool match_decimal_f64_options(String str, isize* index, double* out, uint32_t flags)
{
    isize i = *index;
    
    Matched_Number number = {0};
    bool failed_int_part = false;
    if(match_decimal_number_int_part(str, &i, &number, flags) == false) {
        failed_int_part = true;
        if((flags & MATCH_NUM_ALLOW_LEADING_DOT) == 0)
            return false;
    }
        
    if((flags & MATCH_NUM_DISALLOW_DOT) == 0 && match_char(str, &i, '.'))
        if(match_decimal_number_frac_part(str, &i, &number) == false)
        {
            if((flags & MATCH_NUM_ALLOW_TRAILING_DOT) == 0 || failed_int_part == false)
                return false;
        }

    double result = match_decimal_number_convert(number);
    *out = result;
    return true;
}

EXTERNAL bool match_decimal_i64_options(String str, isize* index, int64_t* out, uint32_t flags)
{
    Matched_Number number = {0};
    isize i = *index;
    if(match_decimal_number_int_part(str, &i, &number, flags))
    {
        if(number.exponent == 0 && (number.mantissa <= number.is_negative ? -INT64_MIN : INT64_MAX)) {
            *index = i;
            *out = number.is_negative ? -(int64_t) number.mantissa : (int64_t) number.mantissa ;
            return true;
        }
        else if(flags & MATCH_NUM_CLAMP_TO_RANGE) {
            *index = i;
            *out = number.is_negative ? INT64_MIN : INT64_MAX;
            return true;
        }
    }
    return false;
}
EXTERNAL bool match_decimal_u64_options(String str, isize* index, uint64_t* out, uint32_t flags)
{
    Matched_Number number = {0};
    isize i = *index;
    if(match_decimal_number_int_part(str, &i, &number, flags))
    {
        if(number.exponent == 0) {
            *index = i;
            *out = number.mantissa;
            return true;
        }
        else if(flags & MATCH_NUM_CLAMP_TO_RANGE) {
            *index = i;
            *out = UINT64_MAX;
            return true;
        }
    }
    return false;
}

EXTERNAL bool match_decimal_u64(String str, isize* index, uint64_t* out)
{
    return match_decimal_u64_options(str, index, out, 0);
}
EXTERNAL bool match_decimal_i64(String str, isize* index, int64_t* out)
{
    return match_decimal_i64_options(str, index, out, MATCH_NUM_MINUS);
}
EXTERNAL bool match_decimal_f64(String str, isize* index, double* out)
{
    return match_decimal_f64_options(str, index, out, MATCH_NUM_MINUS);
}
EXTERNAL bool match_decimal_u32(String str, isize* index, uint32_t* out)
{
    uint64_t wider = 0;
    isize i = *index;
    if(match_decimal_u64_options(str, &i, &wider, 0) == false)
        return false;

    if(wider > UINT32_MAX)
        return false;
    *index = i;
    *out = (uint32_t) wider;
    return true;
}
EXTERNAL bool match_decimal_i32(String str, isize* index, int32_t* out)
{
    int64_t wider = 0;
    isize i = *index;
    if(match_decimal_i64_options(str, &i, &wider, MATCH_NUM_MINUS) == false)
        return false;
        
    if(wider < INT32_MIN || wider > INT32_MAX)
        return false;
    *index = i;
    *out = (int32_t) wider;
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

#endif

#if (defined(MODULE_ALL_TEST) || defined(MODULE_MATCH_TEST)) && !defined(MODULE_MATCH_HAS_TEST)
#define MODULE_MATCH_HAS_TEST

#include "string.h"
static void test_match_ok_example(const char* input, int64_t num, const char* id, int kind, double val)
{
    double epsilon = 1e-8;
    String str = string_of(input);
    Match_Example_Result result = {0};
    bool b1 = match_example(str, &result);

    TEST(b1 == true);
    TEST(result.num == num);
    TEST(result.kind == kind);
    TEST(fabs(result.val - val) < epsilon);
    TEST(string_is_equal(result.id, string_of(id)));
}

static void test_match_failed_example(const char* input)
{
    String str = string_of(input);
    Match_Example_Result result = {0};
    bool b1 = match_example(str, &result);
    TEST(b1 == false);
}

static void test_match_f64(const char* input, double expected)
{
    double obtained = 0;
    isize i = 0;
    
    bool success = isnan(expected) == false;
    TEST(match_decimal_f64(string_of(input), &i, &obtained) == success);
    if(success)
        TEST(obtained == expected);
}

static void test_match_i64(const char* input, int64_t expected, bool success, uint32_t options)
{
    int64_t obtained = 0;
    isize i = 0;
    TEST(match_decimal_i64_options(string_of(input), &i, &obtained, options) == success);
    if(success)
        TEST(obtained == expected);
}

static void test_match()
{
    test_match_f64("0", 0);
    test_match_f64("1", 1);
    test_match_f64("151351", 151351);
    test_match_f64("5451.15544", 5451.15544);
    test_match_f64("0.15544", 0.15544);
    test_match_f64("-0.15544", -0.15544);
    test_match_f64("-0.15544", -0.15544);
    test_match_f64("999999999999999999999990000000000000", 999999999999999999999990000000000000.0);
    test_match_f64("484864846444165115131135648668", 484864846444165115131135648668.0);
    test_match_f64("0.484864846444165115131135648668", 0.484864846444165115131135648668);
    test_match_f64("0.0000484864846444165115131135648668", 0.0000484864846444165115131135648668);
    test_match_f64("-484864846444165115131135648668.45443513515313518798784131845535778", -484864846444165115131135648668.45443513515313518798784131845535778);
    
    test_match_f64("", nan(""));
    test_match_f64("a", nan(""));
    test_match_f64("01", nan(""));
    test_match_f64("001", nan(""));
    test_match_f64("-0154153", nan(""));
    test_match_f64("+1", nan(""));
    test_match_f64("?!", nan(""));
    test_match_f64("-+1", nan(""));
    test_match_f64("+-1", nan(""));
    test_match_f64(".7", nan(""));
    test_match_f64("5451.", nan(""));
    test_match_f64("-5451.", nan(""));
    
    test_match_i64("0", 0, true, 0);
    test_match_i64("1", 1, true, 0);
    test_match_i64("151351", 151351, true, 0);
    test_match_i64("-151351", -151351, false, 0);
    test_match_i64("-151351", -151351, true, MATCH_NUM_MINUS);
    test_match_i64("+151351", 151351, false, 0);
    test_match_i64("+151351", 151351, true, MATCH_NUM_PLUS);
    test_match_i64("+-151351", 151351, false, MATCH_NUM_PLUS);
    test_match_i64("5451.15544", 5451, true, 0);
    test_match_i64("0.15544", 0, true, 0);
    test_match_i64("9999999999999999999999", 0, false, 0);
    test_match_i64("9999999999999999999999", INT64_MAX, true, MATCH_NUM_CLAMP_TO_RANGE);
    test_match_i64("-9999999999999999999999", INT64_MIN, true, MATCH_NUM_MINUS | MATCH_NUM_CLAMP_TO_RANGE);

    test_match_ok_example("[3]: \"hello\"  KIND_SMALL    -45.3 ",        3, "hello", 0, -45.3);
    test_match_ok_example("[431]: 'string'   KIND_MEDIUM   131.3   xxx",   431, "string", 1, 131.3);
    test_match_ok_example("[256]: \"world\"  KIND_BIG      1531.3  51854", 256, "world", 2, 1531.3);
    test_match_ok_example("[516316316464]: \"very long id\"  KIND_BIG  \v\f\n  484864846444165115131135648668.45443513515313518798784131845535778", 
        516316316464, "very long id", 2, 484864846444165115131135648668.45443513515313518798784131845535778);
    test_match_ok_example("[0]:\"\"KIND_SMALL-0", 0, "", 0, 0);
    
    test_match_failed_example("");
    test_match_failed_example("[]:    \"hello\"  KIND_SMALL    -45.3 ");
    test_match_failed_example("[003:  \"hello\"  KIND_SMALL    -45.3 ");
    test_match_failed_example("[431]: string'    KIND_MEDIUM   131.3   xxx");
    test_match_failed_example("[431]: 'string    KIND_MEDIUM   131.3   xxx");
    test_match_failed_example("[431]: string\"   KIND_MEDIUM   131.3   xxx");
    test_match_failed_example("[431]: \"string   KIND_MEDIUM   131.3   xxx");
    test_match_failed_example("[256]: \"world\"  _BIG          1531.3  51854");
    test_match_failed_example("[256]: \"world\"  9KIND_SMALL   1531.3  51854");
    test_match_failed_example("[256]: \"world\"  KIND_SMALLL   1531.3  51854");
    test_match_failed_example("[256]: \"world\"  KIND_MEDIUMHH 1531.3  51854");
    test_match_failed_example("[256]: \"world\"  KIND_SMALL    +1531.3  51854");
    test_match_failed_example("[256]: \"world\"  KIND_MEDIUM   inf ");
    test_match_failed_example("[256]: \"world\"  KIND_MEDIUM   a");
    test_match_failed_example("[256]: \"world\"  KIND_MEDIUM   ");
}

#endif