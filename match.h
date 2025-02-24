#ifndef MODULE_MATCH
#define MODULE_MATCH

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "string.h"

//A file for simple, fast and convenient parsing. See the bottom of the header section for a working example.
//
//The core primitive is matching: all functions take pointer to an index and try to match some specific pattern (ie. whitespace).
// If they fail they return false and do nothing. If they succeed they move the index to the end of the parsed pattern and return true.
// One can extract the parsed portion by saving the index before and after successful match, then slicing the input string. 
// Additionally some functions (number parsing) also directly output the parsed number.
// 
//One of the goals of this module is make the parsing be very detailed (but not tedious!) - it should match the bare minimum specified
// and everything else is an error. The optionality can be added later on by the programmer but done so explicitly. For example
// many parsing functions automatically skip whitespace before or after numbers/whatever else - we dont do that here.
// This is useful for implementing strict parsers of file formats/validating inputs etc.
// 
//Note that the floating point parsing is not *perfectly* accurate for extremely large or very small numbers (though it is very very close).
//If you want to make this perfectly accurate just replace the match_decimal_number_convert function for your own.

EXTERNAL bool match_any(String str, isize* index, isize count); //matches any character. Returns *index + count <= str.count
EXTERNAL bool match_char(String str, isize* index, char c); //matches char c once. Returns true if matched
EXTERNAL bool match_chars(String str, isize* index, char c); //matches char c repeatedly. Returns true if at least one was matched
EXTERNAL bool match_one_of(String str, isize* index, String one_of); //matches any char of one_of once. Returns true if matched
EXTERNAL bool match_any_of(String str, isize* index, String any_of); //matches any char of any_of repeatedly. Returns true if at least one was matched
EXTERNAL bool match_string(String str, isize* index, String sequence); //matches the exact sequence
EXTERNAL bool match_char_nocase(String str, isize* index, char c); //matches char c once ignoring ascii case. Returns true if matched
EXTERNAL bool match_string_nocase(String str, isize* index, String sequence); //matches the exact sequence ignoring ascii case
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
EXTERNAL bool match_not_char_nocase(String str, isize* index, char c);
EXTERNAL bool match_not_string_nocase(String str, isize* index, String sequence);

EXTERNAL bool match_not_space(String str, isize* index);
EXTERNAL bool match_not_alpha(String str, isize* index);
EXTERNAL bool match_not_upper(String str, isize* index);
EXTERNAL bool match_not_lower(String str, isize* index);
EXTERNAL bool match_not_digits(String str, isize* index);
EXTERNAL bool match_not_id_chars(String str, isize* index); 

EXTERNAL bool match_bool(String str, isize* index, bool* out); //matches "true" or "false" and indicates out respectively
EXTERNAL bool match_choice(String str, isize* index, bool* out, String if_true, String if_false); //matches either of the strings and indicates which one
EXTERNAL bool match_choices(String str, isize* index, isize* taken, const String* choices, isize choices_count); //matches one of the strings and indicates its 0-based index (useful for enums)

//Matching of decimal numbers. These functions are by default quite strict and dont allow things like
// leading plus, leading zeroes, leading dot, trailing dot (they will match the number just wont consume the dot)...
// When the number doesnt fit into the destination type these functions fail (floats always fit).
// The specific behaviour can be configured by using the _option variants with one of the many appropriate flags
EXTERNAL bool match_decimal_u64(String str, isize* index, uint64_t* out); //matches numbers like "1130"   -> 113. 
EXTERNAL bool match_decimal_i64(String str, isize* index, int64_t* out); //matches numbers like "-113"  -> -113
EXTERNAL bool match_decimal_f64(String str, isize* index, double* out); //matches numbers like "-11.03", "-12.3e-4", "-inf", "nan"
EXTERNAL bool match_decimal_u32(String str, isize* index, uint32_t* out); //matches numbers like "113"  -> 113
EXTERNAL bool match_decimal_i32(String str, isize* index, int32_t* out); //matches numbers like "-113"  -> -113
EXTERNAL bool match_decimal_f32(String str, isize* index, float* out);  //matches numbers like "-11.03" -> -11.03

#define MATCH_NUM_INF             1    //allows floating point infinities (does nothing for ints)
#define MATCH_NUM_NAN             2    //allows floating point nans (does nothing for ints)
#define MATCH_NUM_EXP             4    //allows floating point "1.3e-10" (negative exponents are always allowed, leading plus only with MATCH_NUM_PLUS, leading zeros only with MATCH_NUM_LEADING_ZEROS)
#define MATCH_NUM_DOT             8    //allows floating point numbers with a dot
#define MATCH_NUM_PLUS            16   //allows numbers like "+10"
#define MATCH_NUM_MINUS           32   //allows numbers like "-10" 
#define MATCH_NUM_LEADING_DOT     64   //allows numbers like ".5" - note that "." is always invalid
#define MATCH_NUM_TRAILING_DOT    128  //allows numbers like "5." - note that: "." is always invalid, "5." would match without this as just "5", the reuslt is the same but the end index differs
#define MATCH_NUM_LEADING_ZEROS   256  //allows numbers like "0001"
#define MATCH_NUM_CLAMP_TO_RANGE  512  //when the integer number doesnt fit into the destination type, clamps it to it (ie will return UINT64_MAX instead of failure for match_decimal_u64_options)
#define MATCH_NUM_CASE_SENSITIVE  1024 //compares symbols for inf, exp, dot in case sensitive manner.
#define MATCH_NUM_FLOAT_DEFAULT (MATCH_NUM_INF | MATCH_NUM_NAN | MATCH_NUM_EXP | MATCH_NUM_DOT | MATCH_NUM_MINUS)

EXTERNAL bool match_decimal_f64_options(String str, isize* index, double* out, uint32_t flags);
EXTERNAL bool match_decimal_i64_options(String str, isize* index, int64_t* out, uint32_t flags);
EXTERNAL bool match_decimal_u64_options(String str, isize* index, uint64_t* out, uint32_t flags);
EXTERNAL bool match_decimal_f64_options_ex(String str, isize* index, double* out, String dot, String exp, String inf, String nan, uint32_t flags);

//The "lowlevel" match number interface - the functions were separated this way to allow for easy
// custom floating point formats (as in for example handling differently spelled inf, nan, dot, exp etc.).
//It is very possible that you might need to reimplement parts to do exactly what you want.
EXTERNAL bool match_decimal_number_sign(String str, isize* index, bool* is_negative, uint32_t flags);
EXTERNAL bool match_decimal_number_int(String str, isize* index, uint64_t* out_mantissa, int64_t* out_exponent, uint32_t flags);
EXTERNAL bool match_decimal_number_frac(String str, isize* index, uint64_t* in_out_mantissa, int64_t* in_out_exponent);
EXTERNAL double match_decimal_number_convert(uint64_t mantissa, int64_t exponent, bool is_negative);

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
        && match_decimal_u64_options(str, &i, &num, MATCH_NUM_LEADING_ZEROS)
        && match_cstring(str, &i, "]:")
        && (match_space(str, &i), true) //optional space
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
        && (match_space(str, &i), true) //optional space
        && match_choices(str, &i, &kind, kinds, 3)
        && match_space(str, &i) //mandatory space
        && match_decimal_f64_options(str, &i, &val, MATCH_NUM_DOT | MATCH_NUM_MINUS)
        && (i == str.count || match_space(str, &i)); //the end or there is mandatory space
    
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

#ifndef INTERNAL
    #define INTERNAL inline static
#endif

INTERNAL bool _match_char(String str, isize* index, char c, bool positive)
{
    if(*index < str.count && (str.data[*index] == c) == positive) {
        *index += 1;
        return true;
    }
    return false;
}

EXTERNAL bool _match_char_nocase(String str, isize* index, char c, bool positive)
{
    if(*index < str.count)
        if((char_to_lower(str.data[*index]) == char_to_lower(c)) == positive) {
            *index += 1;
            return true;
        }
    return false;
}

INTERNAL bool _match_chars(String str, isize* index, char chars, bool positive)
{
    isize start = *index;
    isize i = start;
    if(positive) {
        uint64_t pattern = 0x0101010101010101ull*chars;
        for(; i + 8 <= str.count; i += 8) {
            uint64_t c; memcpy(&c, str.data + i, 8);
            if(c ^ pattern) 
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

INTERNAL bool _match_one_of(String str, isize* index, String one_of, bool positive)
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
INTERNAL bool _match_any_of(String str, isize* index, String any_of, bool positive)
{
    isize start = *index;
    while(_match_one_of(str, index, any_of, positive));
    return *index == start;
}

INTERNAL bool _match_string(String str, isize* index, String sequence, bool positive)
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

EXTERNAL bool _match_string_nocase(String str, isize* index, String sequence, bool positive)
{
    if(*index + sequence.count <= str.count) 
    {
        String sub = string_range(str, *index, *index + sequence.count);
        if(string_is_equal_nocase(sub, sequence) == positive)
        {
            *index += sequence.count;
            return true;
        }
    }
    return false;   
}

INTERNAL bool _match_char_category(String str, isize* index, bool (*is_category_char)(char c), bool positive)
{
    isize start = *index;
    for(; *index < str.count; (*index)++) 
        if(is_category_char(str.data[*index]) != positive) 
            break;
    
    return *index != start;
}

INTERNAL bool _match_is_id_body_char(char c)
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
EXTERNAL bool match_string(String str, isize* index, String sequence)  { return _match_string(str, index, sequence, true);}
EXTERNAL bool match_char_nocase(String str, isize* index, char c) { return _match_char_nocase(str, index, c, true);}
EXTERNAL bool match_string_nocase(String str, isize* index, String sequence) { return _match_string_nocase(str, index, sequence, true);}

EXTERNAL bool match_not_char(String str, isize* index, char c)             { return _match_char(str, index, c, false); }
EXTERNAL bool match_not_chars(String str, isize* index, char c)            { return _match_chars(str, index, c, false); }
EXTERNAL bool match_not_any_of(String str, isize* index, String any_of)    { return _match_any_of(str, index, any_of, false); }
EXTERNAL bool match_not_one_of(String str, isize* index, String one_of)    { return _match_one_of(str, index, one_of, false);}
EXTERNAL bool match_not_string(String str, isize* index, String sequence)  { return _match_string(str, index, sequence, false);}
EXTERNAL bool match_not_char_nocase(String str, isize* index, char c) { return _match_char_nocase(str, index, c, false);}
EXTERNAL bool match_not_string_nocase(String str, isize* index, String sequence) { return _match_string_nocase(str, index, sequence, false);}

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
EXTERNAL bool match_decimal_number_sign(String str, isize* index, bool* is_negative, uint32_t flags)
{
    if((flags & MATCH_NUM_MINUS) && match_char(str, index, '-')) 
        *is_negative = true;
    else if((flags & MATCH_NUM_PLUS) && match_char(str, index, '+')) 
        *is_negative = false;
    else
        return false;
    return true;
}

EXTERNAL bool match_decimal_number_int(String str, isize* index, uint64_t* out_mantissa, int64_t* out_exponent, uint32_t flags)
{
    isize i = *index;
    uint64_t mantissa = 0;
    int64_t exponent = 0;

    //handle first char - needs to be present else error
    if(i >= str.count)
        return false;
    else {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            return false;

        mantissa = digit_value;
        i += 1;
    }

    //handle second char - if not allowing leading zeros dont allow first to be 0
    if(i >= str.count)
        goto end;
    else {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            goto end;
        
        if((flags & MATCH_NUM_LEADING_ZEROS) == 0 && mantissa == 0)
            return false;

        mantissa = mantissa*10 + digit_value;
        i += 1;
    }
    
    //handle up to 16 more digits without having to worry about overflow
    //We also parse up to 4 digits at a time for speed
    for(int j = 0; j < 4; j++)  {
        if(i + 4 > str.count)
            break;

        uint8_t d0 = (uint8_t) str.data[i+0] - (uint8_t) '0';
        uint8_t d1 = (uint8_t) str.data[i+1] - (uint8_t) '0';
        uint8_t d2 = (uint8_t) str.data[i+2] - (uint8_t) '0';
        uint8_t d3 = (uint8_t) str.data[i+3] - (uint8_t) '0';
        
        if(d0 > 9) { i += 0; goto end; } mantissa = mantissa*10 + d0;
        if(d1 > 9) { i += 1; goto end; } mantissa = mantissa*10 + d1;
        if(d2 > 9) { i += 2; goto end; } mantissa = mantissa*10 + d2;
        if(d3 > 9) { i += 3; goto end; } mantissa = mantissa*10 + d3;
        i += 4;
    }
    
    //big numbers need to handle overflow gracefully
    for(; i < str.count; i++) {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            goto end;

        if(mantissa > UINT64_MAX/10)
            exponent += 1;
        else
            mantissa = mantissa*10 + digit_value;
    }

    end:
    *index = i;
    *out_mantissa = mantissa;
    *out_exponent = exponent;
    return true;
}

EXTERNAL bool match_decimal_number_frac(String str, isize* index, uint64_t* in_out_mantissa, int64_t* in_out_exponent)
{
    isize start = *index;
    isize i = start; 
    for(; i < str.count; i++) {
        uint64_t digit_value = (uint64_t) str.data[i] - (uint64_t) '0';
        if(digit_value > 9)
            break;

        if(*in_out_mantissa <= UINT64_MAX/10) {
            *in_out_exponent -= 1;
            *in_out_mantissa = *in_out_mantissa*10 + digit_value;
        }
    }

    *index = i;
    return i != start;
}

#include <math.h>
EXTERNAL double match_decimal_number_convert(uint64_t mantissa, int64_t exponent, bool is_negative)
{
    const static double pow10[] = {
         1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9, 1e10,
        1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20,
        1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29, 1e30,
        1e31, 1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39, 1e40,
    };

    double result = (double) mantissa;
    if(exponent != 0 && mantissa != 0)
    {
        isize abs_dec_places = exponent < 0 ? -exponent : exponent;
        double decimal_pow = 0;
        if(abs_dec_places <= 40)
            decimal_pow = pow10[abs_dec_places - 1];
        else
            decimal_pow = pow(10, (double) abs_dec_places);

        if(exponent < 0)
            result /= decimal_pow;
        else
            result *= decimal_pow;
    }
    if(is_negative)
        result = -result;
    return result;
}

INTERNAL bool _match_string_maybe_nocase(String str, isize* index, String seq, uint32_t flags)
{
    if(flags & MATCH_NUM_CASE_SENSITIVE)
        return match_string(str, index, seq);
    else
        return match_string_nocase(str, index, seq);
}

INTERNAL bool _match_decimal_f64_options(String str, isize* index, double* out, String dot_text, String exp_text, String inf_text, String nan_text, bool default_dot_exp, uint32_t flags)
{
    isize i = *index;
    uint64_t mantissa = 0;
    int64_t exponent = 0;
    bool is_negative = false;

    if((flags & MATCH_NUM_NAN) && _match_string_maybe_nocase(str, index, nan_text, flags)) {
        *out = nan("match");
        return true;
    }

    match_decimal_number_sign(str, &i, &is_negative, flags);
    if((flags & MATCH_NUM_INF) && _match_string_maybe_nocase(str, &i, inf_text, flags)) {
        *out = is_negative ? -(double) INFINITY : (double) INFINITY;
        *index = i;
        return true;
    }

    bool failed_int_part = false;
    if(match_decimal_number_int(str, &i, &mantissa, &exponent, flags) == false) {
        failed_int_part = true;
        if((flags & MATCH_NUM_LEADING_DOT) == 0)
            return false;
    }
        
    if((flags & MATCH_NUM_DOT)) {
        isize i2 = i; //so that we dont corrupt the index in case only the dot matches
        if(default_dot_exp ? match_char(str, &i2, '.') : _match_string_maybe_nocase(str, &i2, dot_text, flags)) {
            if(match_decimal_number_frac(str, &i2, &mantissa, &exponent))
                i = i2;
            else if(failed_int_part) //the number is just "[+|-]dot" fail
                return false;
            else if(flags & MATCH_NUM_TRAILING_DOT) //consume dot
                i = i2;
        }
    }
    
    if((flags & MATCH_NUM_EXP)) {
        isize i2 = i;
        if(default_dot_exp ? (match_char(str, &i2, 'e') || match_char(str, &i2, 'E')) : _match_string_maybe_nocase(str, &i2, exp_text, flags)) {
            uint64_t exponent_notation_mantissa = 0;
            int64_t  exponent_notation_exponent = 0;
            bool     exponent_notation_is_negative = false;
            match_decimal_number_sign(str, &i2, &exponent_notation_is_negative, flags | MATCH_NUM_MINUS);
            if(match_decimal_number_int(str, &i2, &exponent_notation_mantissa, &exponent_notation_exponent, flags)) {
                int64_t exp_notation_value = 0;
                //Clamp the exponent notation value - those numbers will be infinity anyway so the precise constant doesnt matter.
                //We just want to make sure we dont overflow so we use INT64_MAX/2.
                if(exponent_notation_exponent > 0 || exponent_notation_mantissa > INT64_MAX/2)
                    exp_notation_value = INT64_MAX/2;
                else
                    exp_notation_value = (int64_t) exponent_notation_mantissa;

                exponent += exponent_notation_is_negative ? -exp_notation_value : exp_notation_value;
                i = i2;
            }
        }
    }

    *index = i;
    *out = match_decimal_number_convert(mantissa, exponent, is_negative);
    return true;
}

EXTERNAL bool match_decimal_f64_options_ex(String str, isize* index, double* out, String dot, String exp, String inf, String nan, uint32_t flags)
{
    return _match_decimal_f64_options(str, index, out, dot, exp, inf, nan, false, flags);
}

EXTERNAL bool match_decimal_f64_options(String str, isize* index, double* out, uint32_t flags)
{
    return _match_decimal_f64_options(str, index, out, STRING("."), STRING("e"), STRING("inf"), STRING("nan"), true, flags);
}

EXTERNAL bool match_decimal_i64_options(String str, isize* index, int64_t* out, uint32_t flags)
{
    isize i = *index;
    uint64_t mantissa = 0;
    int64_t exponent = 0;
    bool is_negative = false;
    match_decimal_number_sign(str, &i, &is_negative, flags);
    if(match_decimal_number_int(str, &i, &mantissa, &exponent, flags))
    {
        if(exponent == 0 && mantissa <= (is_negative ? -(uint64_t)INT64_MIN : INT64_MAX)) {
            *index = i;
            *out = is_negative ? -(int64_t) mantissa : (int64_t) mantissa ;
            return true;
        }
        else if(flags & MATCH_NUM_CLAMP_TO_RANGE) {
            *index = i;
            *out = is_negative ? INT64_MIN : INT64_MAX;
            return true;
        }
    }
    return false;
}
EXTERNAL bool match_decimal_u64_options(String str, isize* index, uint64_t* out, uint32_t flags)
{
    isize i = *index;
    uint64_t mantissa = 0;
    int64_t exponent = 0;
    bool is_negative = false;
    match_decimal_number_sign(str, &i, &is_negative, flags); //yes I will allow this (in the default case this is disabled)
    if(match_decimal_number_int(str, &i, &mantissa, &exponent, flags))
    {
        if(exponent == 0) {
            *index = i;
            *out = mantissa;
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
    return match_decimal_f64_options(str, index, out, MATCH_NUM_FLOAT_DEFAULT);
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
#include <math.h>
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

//index > 0 => ok and i must equal index
//index < 0 => ok and i must equal end of string (for convenience)
//index = 0 => failed
static void test_match_f64(const char* input, double expected, isize index)
{
    isize i = 0;
    double obtained = 0;
    double epsilon = 1e-15;
    String str = string_of(input);
    TEST(match_decimal_f64(str, &i, &obtained) == (index != 0));
    if(index >= 0) 
        TEST(i == index);
    else
        TEST(i == str.count);
    
    TEST(fpclassify(obtained) == fpclassify(expected));
    TEST(!(fabs(obtained - expected) >= epsilon));
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
    //some correct numbers
    test_match_f64("0", 0, -1);
    test_match_f64("1", 1, -1);
    test_match_f64("151351", 151351, -1);
    test_match_f64("5451.15544", 5451.15544, -1);
    test_match_f64("0.15544", 0.15544, -1);
    test_match_f64("-0.15544", -0.15544, -1);
    test_match_f64("-0.15544", -0.15544, -1);
    test_match_f64("-1e-10", -1e-10, -1);
    test_match_f64("-1E-10", -1E-10, -1);
    test_match_f64("35.01e-0", 35.01, -1);
    test_match_f64("-3554554.531e-21", -3554554.531e-21, -1);
    test_match_f64("-3554554.531E-21", -3554554.531E-21, -1);
    test_match_f64("inf", INFINITY, -1);
    test_match_f64("-inf", -INFINITY, -1);
    test_match_f64("-iNf", -INFINITY, -1);
    test_match_f64("-iNF", -INFINITY, -1);
    test_match_f64("nan", nan(""), -1);
    test_match_f64("NaN", nan(""), -1);
    test_match_f64("NAN", nan(""), -1);

    //bonkers numbers
    test_match_f64("999999999999999999999990000000000000", 999999999999999999999990000000000000.0, -1);
    test_match_f64("484864846444165115131135648668", 484864846444165115131135648668.0, -1);
    test_match_f64("0.484864846444165115131135648668", 0.484864846444165115131135648668, -1);
    test_match_f64("0.0000484864846444165115131135648668", 0.0000484864846444165115131135648668, -1);
    test_match_f64("-484864846444165115131135648668.45443513515313518798784131845535778", -484864846444165115131135648668.45443513515313518798784131845535778, -1);
    test_match_f64("0.4848648e153153185458445464644", INFINITY, -1);
    test_match_f64("0e153153185458445464644", 0, -1);
    test_match_f64("-0e153153185458445464644", 0, -1);
    test_match_f64("999999999999999999999990000000000000e-153153185458445464644", 0, -1);
    test_match_f64("-999999999999999999999990000000000000E-153153185458445464644", 0, -1);
    test_match_f64("-484864846444165115131135648668.45443513515313518798784131845535778e8458464351533511156413513515115315", -INFINITY, -1);
    test_match_f64("-11.45443513515313518798784131845535778E-8458464351533511156413513515115315", 0, -1);
    
    //failed
    test_match_f64("", 0, 0);
    test_match_f64("a", 0, 0);
    test_match_f64("01", 0, 0);
    test_match_f64("001", 0, 0);
    test_match_f64("-0154153", 0, 0);
    test_match_f64("+1", 0, 0);
    test_match_f64("?!", 0, 0);
    test_match_f64("-+1", 0, 0);
    test_match_f64("+-1", 0, 0);
    test_match_f64(".7", 0, 0);
    test_match_f64("-nan", 0, 0);
    test_match_f64("+nan", 0, 0);
    test_match_f64("+inf", 0, 0);

    //parial matches
    test_match_f64("-35.", -35, 3);
    test_match_f64("35.", 35, 2);
    test_match_f64("35agajgj", 35, 2);
    test_match_f64("35.01e", 35.01, 5);
    test_match_f64("35.01e-", 35.01, 5);
    test_match_f64("35.01e00", 35.01, 5);
    test_match_f64("35.01e-0a", 35.01, 8);
    test_match_f64("35.01e1.5454", 35.01e1, 7);
    test_match_f64("35.01e00a", 35.01, 5);
    test_match_f64("-3554554.531E-21", -3554554.531E-21, -1);
    test_match_f64("inF.", INFINITY, 3);
    test_match_f64("-Infinity", -INFINITY, 4);
    test_match_f64("-INfajkkjjaf", -INFINITY, 4);
    test_match_f64("NAnnanan", nan(""), 3);
    
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

    test_match_ok_example("[003]: \"hello\"  KIND_SMALL    -45.3 ",        3, "hello", 0, -45.3);
    test_match_ok_example("[431]: 'string'   KIND_MEDIUM   131.3   xxx",   431, "string", 1, 131.3);
    test_match_ok_example("[256]: \"world\"  KIND_BIG      1531.3  51854", 256, "world", 2, 1531.3);
    test_match_ok_example("[516316316464]: \"very long id\"  KIND_BIG  \v\f\n  484864846444165115131135648668.45443513515313518798784131845535778", 
        516316316464, "very long id", 2, 484864846444165115131135648668.45443513515313518798784131845535778);
    test_match_ok_example("[0]:\"\"KIND_SMALL -0", 0, "", 0, 0);
    
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
    test_match_failed_example("[003]: \"hello\"  KIND_SMALL    -45.3aaa");
    test_match_failed_example("[003]: \"hello\"  KIND_SMALL    inf");
    test_match_failed_example("[003]: \"hello\"  KIND_SMALL    infinity");
    test_match_failed_example("[003]: \"hello\"  KIND_SMALL    1.3e5");
    test_match_failed_example("[003]: \"hello\"  KIND_SMALL    nan");
    test_match_failed_example("[256]: \"world\"  KIND_MEDIUM   a");
    test_match_failed_example("[256]: \"world\"  KIND_MEDIUM   ");
}

#endif