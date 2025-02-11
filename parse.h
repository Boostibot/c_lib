#ifndef MODULE_PARSE
#define MODULE_PARSE

#include "defines.h"
#include "assert.h"
#include "string.h"

static inline bool match_save(isize index, isize* save_into) { *save_into = index; return true; }

EXTERNAL bool match_char(String str, isize* index, char c);
EXTERNAL bool match_any_of(String str, isize* index, String any_of);
EXTERNAL bool match_string(String str, isize* index, String sequence);
EXTERNAL bool match_any_ofc(String str, isize* index, const char* any_of);
EXTERNAL bool match_cstring(String str, isize* index, const char* sequence);

EXTERNAL bool match_not_char(String str, isize* index, char c);
EXTERNAL bool match_not_any_of(String str, isize* index, String any_of);
EXTERNAL bool match_not_string(String str, isize* index, String sequence);

EXTERNAL bool match_space(String str, isize* index);
EXTERNAL bool match_alpha(String str, isize* index);
EXTERNAL bool match_upper(String str, isize* index);
EXTERNAL bool match_lower(String str, isize* index);
EXTERNAL bool match_digits(String str, isize* index);

EXTERNAL bool match_not_space(String str, isize* index);
EXTERNAL bool match_not_alpha(String str, isize* index);
EXTERNAL bool match_not_upper(String str, isize* index);
EXTERNAL bool match_not_lower(String str, isize* index);
EXTERNAL bool match_not_digits(String str, isize* index);

EXTERNAL bool match_bool(String str, isize* index, bool* out); //matches "true" or "false"
EXTERNAL bool match_choice2(String str, isize* index, bool* out, String if_true, String if_false);
EXTERNAL bool match_choices(String str, isize* index, isize* taken, const String* choices, isize choices_count);

//starts with _, [a-z], [A-Z] then is followed by any number of [0-9], _, [a-z], [A-Z]
EXTERNAL bool match_id(String str, isize* index); 

EXTERNAL bool match_decimal_u64(String str, isize* index, u64* out); //"00113000"   -> 113000
EXTERNAL bool match_decimal_i64(String str, isize* index, i64* out); //"-00113000"  -> -113000
EXTERNAL bool match_decimal_i32(String str, isize* index, i32* out); //"-00113000"  -> -113000
EXTERNAL bool match_decimal_f32(String str, isize* index, f32* out); //"-0011.0300" -> -11.03000
EXTERNAL bool match_decimal_f64(String str, isize* index, f64* out);

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
    double num;
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
                && match_save(i, &id_from)
                && (match_not_char(str, &i, '"'), true)
                && match_save(i, &id_to)
                && match_char(str, &i,      '"'))
            || (match_char(str, &i,         '\'')
                && match_save(i, &id_from)
                && (match_not_char(str, &i, '\''), true)
                && match_save(i, &id_to)
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
        result->kind = kind;
    }
    return ok;
}

bool match_example_with_errors(String str, Match_Example_Result* result, int* error_stage)
{
    isize i = 0;

    uint64_t num = 0;
    double val = 0;
    
    String kinds[3] = {
        STRING("KIND_BIG"),
        STRING("KIND_MEDIUM"),
        STRING("KIND_SMALL"),
    };

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
        match_not_char(str, &i, quote);
        id_to = i;
        if(match_char(str, &i, quote) == false)
        {
            *error_stage = 3;
            printf("id string was not properly terminated found ...\n");
            break;
        }
        
        match_space(str, &i);
        if(0) {}
        else if(match_cstring(str, &i, "KIND_SMALL"))  kind = 0;
        else if(match_cstring(str, &i, "KIND_MEDIUM")) kind = 1;
        else if(match_cstring(str, &i, "KIND_SMALL"))  kind = 2;
        else {
            *error_stage = 4;
            printf("Invalid enum option ...\n");
            break;
        }
        match_space(str, &i);

        if(match_decimal_f64(str, &i, &val) == false)
        {
            *error_stage = 5;
            printf("Could not parse float value...\n");
            break;
        }
        
        //save parsed variable...
        result->val = val;
        result->num = num;
        result->id = string_range(str, id_from, id_to);
        result->kind = kind;
        ok = true;
    } while(0);

    return ok;
}
#endif

#endif

#define MODULE_IMPL_ALL

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_PARSE)) && !defined(MODULE_HAS_IMPL_PARSE)
#define MODULE_HAS_IMPL_PARSE

#include <math.h>
EXTERNAL bool _match_char(String str, isize* index, char c, bool positive)
{
    if(*index < str.count && (str.data[*index] == c) == positive)
    {
        *index += 1;
        return true;
    }
    return false;
}

EXTERNAL bool _match_any_of(String str, isize* index, String any_of, bool positive)
{
    isize i = *index;
    for(; i < str.count; i++)
    {
        char current = str.data[i];
        bool found = false;
        for(isize j = 0; j < any_of.count; j++)
            if(any_of.data[j] == current)
            {
                found = true;
                break;
            }

        if(found != positive)
            break;
    }

    bool matched = i != *index;
    *index = i;
    return matched;
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

inline static bool _match_char_category(String str, isize* index, bool (*func)(char c), bool positive)
{
    isize i = *index;
    for(; i < str.count; i++) {
        if(func(str.data[i]) != positive) 
            break;
    }
    bool matched = i != *index;
    *index = i;
    return matched;
}

EXTERNAL bool match_id(String str, isize* index)
{
    if(*index < str.count)
    {
        if(char_is_alpha(str.data[*index]) || str.data[*index] == '_')
        {
            *index += 1;
            for(; *index < str.count; *index += 1)
            {
                char c = str.data[*index];
                bool is_valid = char_is_alpha(c) || char_is_digit(c) || c == '_';
                if(is_valid == false)
                    break;
            }

            return true;
        }
    }
    return false;
}

EXTERNAL bool match_char(String str, isize* index, char c)             { return _match_char(str, index, c, true); }
EXTERNAL bool match_any_of(String str, isize* index, String any_of)    { return _match_any_of(str, index, any_of, true); }
EXTERNAL bool match_string(String str, isize* index, String sequence)  { return _match_sequence(str, index, sequence, true);}

EXTERNAL bool match_not_char(String str, isize* index, char c)             { return _match_char(str, index, c, false); }
EXTERNAL bool match_not_any_of(String str, isize* index, String any_of)    { return _match_any_of(str, index, any_of, false); }
EXTERNAL bool match_not_string(String str, isize* index, String sequence)  { return _match_sequence(str, index, sequence, false);}

EXTERNAL bool match_space(String str, isize* index)  { return _match_char_category(str, index, char_is_space, true); } 
EXTERNAL bool match_alpha(String str, isize* index)  { return _match_char_category(str, index, char_is_alpha, true); } 
EXTERNAL bool match_upper(String str, isize* index)  { return _match_char_category(str, index, char_is_upper, true); } 
EXTERNAL bool match_lower(String str, isize* index)  { return _match_char_category(str, index, char_is_lower, true); } 
EXTERNAL bool match_digits(String str, isize* index) { return _match_char_category(str, index, char_is_digit, true); }

EXTERNAL bool match_not_space(String str, isize* index)  { return _match_char_category(str, index, char_is_space, false); } 
EXTERNAL bool match_not_alpha(String str, isize* index)  { return _match_char_category(str, index, char_is_alpha, false); } 
EXTERNAL bool match_not_upper(String str, isize* index)  { return _match_char_category(str, index, char_is_upper, false); } 
EXTERNAL bool match_not_lower(String str, isize* index)  { return _match_char_category(str, index, char_is_lower, false); } 
EXTERNAL bool match_not_digits(String str, isize* index) { return _match_char_category(str, index, char_is_digit, false); }

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
    bool has_minus = match_char(str, index, '-');
    u64 uout = 0;
    bool matched_number = match_decimal_u64(str, index, &uout);
    uout = MIN(uout, INT64_MAX);
    if(has_minus)
        *out = - (i64) uout;
    else
        *out = (i64) uout;

    return matched_number;
}

EXTERNAL bool match_decimal_i32(String str, isize* index, i32* out)
{
    bool has_minus = match_char(str, index, '-');
    u64 uout = 0;
    bool matched_number = match_decimal_u64(str, index, &uout);
    if(has_minus)
        *out = - (i32) uout;
    else
        *out = (i32) uout;

    return matched_number;
}

INTERNAL f32 _quick_pow10f(isize power)
{
    switch(power)
    {
        case 0: return 1.0f;
        case 1: return 10.0f;
        case 2: return 100.0f;
        case 3: return 1000.0f;
        case 4: return 10000.0f;
        case 5: return 100000.0f;
        default: return powf(10.0f, (f32) power);
    };
}

INTERNAL f64 _quick_pow10lf(isize power)
{
    switch(power)
    {
        case 0: return 1.0;
        case 1: return 10.0;
        case 2: return 100.0;
        case 3: return 1000.0;
        case 4: return 10000.0;
        case 5: return 100000.0;
        default: return pow(10.0, (f64) power);
    };
}

EXTERNAL bool match_decimal_f32(String str, isize* index, f32* out)
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
    f32 decimal_pow = _quick_pow10f(decimal_size);
    f32 result = (f32) before_dot + (f32) after_dot / decimal_pow;
    if(has_minus)
        result = -result;

    *out = result;
    return true;
}

EXTERNAL bool match_decimal_f64(String str, isize* index, f64* out)
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
    f64 decimal_pow = _quick_pow10lf(decimal_size);
    f64 result = (f64) before_dot + (f64) after_dot / decimal_pow;
    if(has_minus)
        result = -result;

    *out = result;
    return true;
}

#if 0
#include "math.h"
INTERNAL void test_match()
{
    {
        isize index = 0;
        TEST(match_whitespace(STRING("   "), &index));
        TEST(index == 3);
    }
    
    {
        isize index = 0;
        TEST(match_whitespace(STRING("   \n \r \t "), &index));
        TEST(index == 9);
    }
    
    {
        isize index = 0;
        TEST(match_whitespace(STRING("a "), &index) == false);
        TEST(index == 0);
    }
    
    {
        isize index = 0;
        TEST(match_whitespace_custom(STRING("a "), &index, MATCH_INVERTED));
        TEST(index == 1);
    }
    
    {
        isize index = 0;
        TEST(match_whitespace_custom(STRING("a"), &index, MATCH_INVERTED));
        TEST(index == 1);
    }

    {
        f32 test_f32 = 0;
        isize index = 0;
        TEST(match_decimal_f32(STRING("12"), &index, &test_f32));
        TEST(is_near_scaledf(12.0f, test_f32, EPSILON));
    }
    
    {
        f32 test_f32 = 0;
        isize index = 0;
        TEST(match_decimal_f32(STRING("-12"), &index, &test_f32));
        TEST(is_near_scaledf(-12.0f, test_f32, EPSILON));
    }
    
    {
        f32 test_f32 = 0;
        isize index = 0;
        TEST(match_decimal_f32(STRING("-12.05"), &index, &test_f32));
        TEST(is_near_scaledf(-12.05f, test_f32, EPSILON));
    }
}
#endif

#endif