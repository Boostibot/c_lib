#ifndef JOT_FORMAT
#define JOT_FORMAT

#include "vformat.h"
#include "base64.h"

#define PREFORMAT_LEAST_BUFFER_SIZE 64

// Converts value in the range [0, 100) to a string.
#define TWO_DIGITS_TO_STRING(value)             \
        &"0001020304050607080910111213141516171819" \
         "2021222324252627282930313233343536373839" \
         "4041424344454647484950515253545556575859" \
         "6061626364656667686970717273747576777879" \
         "8081828384858687888990919293949596979899"[(value) * 2] \

//Extremely performant int to string conversion function. Source: fmtlib format_decimal()
//Writes some number of digits into the buffer. 
//The buffer needs to be at least PREFORMAT_LEAST_BUFFER_SIZE sized.
//Saves range [written_from, written_to) in which the result is stored
EXPORT void preformat_decimal(char* buffer, int64_t value, int64_t size, int64_t* written_from, int64_t* written_to) 
{
    char* out = buffer + size;
    while (value >= 100) 
    {
        // Integer division is slow so do it for a group of two digits instead
        // of for every digit. The idea comes from the talk by Alexandrescu
        // "Three Optimization Tips for C++". See speed-test for a comparison.
        out -= 2;
        const char* two_digits = TWO_DIGITS_TO_STRING(value % 100);
        memcpy(out, two_digits, 2);
        value /= 100;
    }

    if (value < 10) 
    {   
        out -= 1;
        *out = (char) ('0' + value);
    }
    else
    {
        out -= 2;
        const char* two_digits = TWO_DIGITS_TO_STRING(value);
        memcpy(out, two_digits, 2);
    }


    *written_from = (int64_t) (out - buffer);
    *written_to = size;
}

//Writes some number of digits into the buffer. 
//The buffer needs to be at least PREFORMAT_LEAST_BUFFER_SIZE sized.
//Saves range [written_from, written_to) in which the result is stored
EXPORT void preformat_uint(char* buffer, uint64_t num, int64_t size, uint8_t base, const char digits[64], int64_t* written_from, int64_t* written_to)
{
    ASSERT(base <= 36 && base >= 2);
    int used_size = 0;
    uint64_t last = num;
    while(true)
    {
        uint64_t div = last / base;
        uint64_t last_digit = last % base;

        buffer[size - 1 - used_size] = digits[last_digit];
        used_size ++;

        last = div;
        if(last == 0)
            break;
    }

    *written_from = size - used_size;
    *written_to = size;
}


//we dont use / and order this differently from base64 standard encoding because / is not filesystem compatible
//the 65th char is the separator. There we stick to customs and use =. 
//(We need 66 chars because C strings are null terminated and I dont want to type each char idividually)
const char CUSTOM_BASE64_DIGITS[66] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_+=";

EXPORT void format_udecimal_append_into(String_Builder* into, u64 num)
{
    char buffer[PREFORMAT_LEAST_BUFFER_SIZE + 2]; //explicitly unitilialized
    buffer[PREFORMAT_LEAST_BUFFER_SIZE + 1] = '\0'; //null terminated
    int64_t from; //explicitly unitilialized
    int64_t to; //explicitly unitilialized

    preformat_decimal(buffer, num, PREFORMAT_LEAST_BUFFER_SIZE + 1, &from, &to);
    array_append(into, buffer + from, to - from);
}

EXPORT void format_decimal_append_into(String_Builder* into, i64 num)
{
    char buffer[PREFORMAT_LEAST_BUFFER_SIZE + 2]; //explicitly unitilialized
    buffer[PREFORMAT_LEAST_BUFFER_SIZE + 1] = '\0'; //null terminated
    int64_t from; //explicitly unitilialized
    int64_t to; //explicitly unitilialized

    preformat_decimal(buffer, llabs(num), PREFORMAT_LEAST_BUFFER_SIZE + 1, &from, &to);
    if(num < 0)
        buffer[--from] = '-';

    array_append(into, buffer + from, to - from);
}

EXPORT void format_int_append_into(String_Builder* into, i64 num, u8 base)
{
    if(base == 10)
    {
        format_decimal_append_into(into, num);
    }
    else
    {
        char buffer[PREFORMAT_LEAST_BUFFER_SIZE + 2]; //explicitly unitilialized
        buffer[PREFORMAT_LEAST_BUFFER_SIZE + 1] = '\0'; //null terminated
        int64_t from; //explicitly unitilialized
        int64_t to; //explicitly unitilialized

        preformat_uint(buffer, llabs(num), PREFORMAT_LEAST_BUFFER_SIZE + 1, base, CUSTOM_BASE64_DIGITS, &from, &to);
        if(num < 0)
            buffer[--from] = '-';
        array_append(into, buffer + from, to - from);
    }
}

EXPORT void format_uint_append_into(String_Builder* into, u64 num, u8 base)
{
    if(base == 10)
    {
        format_udecimal_append_into(into, num);
    }
    else
    {
        char buffer[PREFORMAT_LEAST_BUFFER_SIZE + 2]; //explicitly unitilialized
        buffer[PREFORMAT_LEAST_BUFFER_SIZE + 1] = '\0'; //null terminated
        int64_t from; //explicitly unitilialized
        int64_t to; //explicitly unitilialized

        preformat_uint(buffer, num, PREFORMAT_LEAST_BUFFER_SIZE + 1, base, CUSTOM_BASE64_DIGITS, &from, &to);
        array_append(into, buffer + from, to - from);
    }
}

EXPORT void format_udecimal_into(String_Builder* into, u64 num)
{
    array_clear(into);
    format_udecimal_append_into(into, num);
}
EXPORT void format_decimal_into(String_Builder* into, i64 num)
{
    array_clear(into);
    format_decimal_append_into(into, num);
}
EXPORT void format_int_into(String_Builder* into, i64 num, u8 base)
{
    array_clear(into);
    format_int_append_into(into, num, base);
}
EXPORT void format_uint_into(String_Builder* into, u64 num, u8 base)
{
    array_clear(into);
    format_uint_append_into(into, num, base);
}

EXPORT void base64_encode_append_into(String_Builder* into, const void* data, isize len, Base64_Encoding encoding)
{
    isize size_before = into->size;
    isize needed = base64_encode_max_output_length(len);
    array_resize(into, size_before + needed);

    isize actual_size = base64_encode(into->data + size_before, data, len, encoding);
    array_resize(into, size_before + actual_size);
}

EXPORT bool base64_decode_append_into(String_Builder* into, const void* data, isize len, Base64_Decoding decoding)
{
    isize size_before = into->size;
    isize needed = base64_decode_max_output_length(len);
    array_resize(into, size_before + needed);
    
    isize error_at = 0;
    isize actual_size = base64_decode(into->data + size_before, data, len, decoding, &error_at);
    if(error_at == -1)
    {
        array_resize(into, size_before + actual_size);
        return true;
    }
    else
    {
        array_resize(into, size_before);
        return false;
    }
}

EXPORT void base64_encode_into(String_Builder* into, const void* data, isize len, Base64_Encoding encoding)
{
    array_clear(into);
    base64_encode_append_into(into, data, len, encoding);
}

EXPORT bool base64_decode_into(String_Builder* into, const void* data, isize len, Base64_Decoding decoding)
{
    array_clear(into);
    return base64_decode_append_into(into, data, len, decoding);
}

#if 0
typedef enum Float_Format
{
    FP_FORMAT_EXP,
    FP_FORMAT_GENERAL,
    FP_FORMAT_FRACTION,
    FP_FORMAT_HEX,
} Float_Format;

typedef enum Float_Align
{
    ALIGN_NONE,
    ALIGN_LEFT,
    ALIGN_RIGHT,
    ALIGN_NUMERIC, //???
} Float_Align;

typedef struct Decimal_FP
{
    int64_t significand;
    int64_t exponent;
} Decimal_FP;

typedef struct Float_Format_Spec
{
    int locale_stuff;
    int sign;
    int precision;
    int width;
    bool showpoint;
    Float_Format format;
    Float_Align align;
} Float_Format_Spec;

EXPORT void preformat_decimal(char* buffer, uint64_t value, int64_t size, uint64_t* written_from, uint64_t* written_to);
INTERNAL char* write_significand(char* out, uint64_t significand, int significand_size, int integral_size, char decimal_point) {
    
    if (!decimal_point)
    {
        int64_t written_to = 0;
        int64_t written_from = 0;
        preformat_decimal(out, significand, integral_size, &written_from, &written_to);
        format_decimal_append_into(out, significand);
        return out + integral_size;
    }

    out += significand_size + 1;
    char* end = out;
    int floating_size = significand_size - integral_size;
    for (int i = floating_size / 2; i > 0; --i) {
        
        out -= 2;
        const char* two_digits = TWO_DIGITS_TO_STRING(significand % 100);
        memcpy(out, two_digits, 2);
        significand /= 100;
    }
    if (floating_size % 2 != 0) {
        *--out = '0' + significand % 10;
        significand /= 10;
    }
    *--out = decimal_point;
    
    int64_t written_to = 0;
    int64_t written_from = 0;
    preformat_decimal(out - integral_size, significand, integral_size, &written_from, &written_to);
    return end;
}

//typedef memory_buffer char[500];
char zero = '0';
char decimal_point = '.';
char exp_char = 'e';
char sign_char = '-';

template <typename Char, typename It>
FMT_CONSTEXPR auto write_exponent(int exp, It it) -> It {
  FMT_ASSERT(-10000 < exp && exp < 10000, "exponent out of range");
  if (exp < 0) {
    *it++ = static_cast<Char>('-');
    exp = -exp;
  } else {
    *it++ = static_cast<Char>('+');
  }
  if (exp >= 100) {
    const char* top = digits2(to_unsigned(exp / 100));
    if (exp >= 1000) *it++ = static_cast<Char>(top[0]);
    *it++ = static_cast<Char>(top[1]);
    exp %= 100;
  }
  const char* d = digits2(to_unsigned(exp));
  *it++ = static_cast<Char>(d[0]);
  *it++ = static_cast<Char>(d[1]);
  return it;
}


INTERNAL char* write_nonfinite(char* out, bool is_nan, Float_Format_Spec fspecs) 
{

}
INTERNAL char* write_float(char* out, f64 value, Float_Format_Spec fspecs) 
{
  //float_specs fspecs = parse_float_type_spec(specs);
  //fspecs.sign = specs.sign;
  
  // value < 0 is false for NaN so use signbit.
  if (signbit(value)) 
  {  
    fspecs.sign = -1;
    value = -value;
  } else if (fspecs.sign == -1) {
    fspecs.sign = 0;
  }

  if (!isfinite(value))
    return write_nonfinite(out, isnan(value), fspecs);

  if (fspecs.align == ALIGN_NUMERIC && fspecs.sign) 
  {
    char* it = out;
    *it++ = sign_char;
    out = out;
    fspecs.sign = 0;
    if (fspecs.width != 0) 
        --fspecs.width;
  }

  char buffer[500] = {0};
  char* buffer_it = buffer;
  if (fspecs.format == FP_FORMAT_HEX) 
  {
    if (fspecs.sign) 
        *buffer_it++ = sign_char;

    ASSERT("@TODO: Format hexfloat!");
    format_hexfloat(convert_float(value), fspecs.precision, fspecs, buffer_it);
    return write_bytes<align::right>(out, {buffer.data(), buffer.size()},
                                     fspecs);
  }

  int precision = fspecs.precision >= 0 || fspecs.type == presentation_type::none
                      ? fspecs.precision
                      : 6;
  if (fspecs.format == float_format::exp) {
    if (precision == INT_MAX)
      throw_format_error("number is too big");
    else
      ++precision;
  } else if (fspecs.format != float_format::fixed && precision == 0) {
    precision = 1;
  }
  if (const_check(std::is_same<T, float>())) fspecs.binary32 = true;
  int exp = format_float(convert_float(value), precision, fspecs, buffer);
  fspecs.precision = precision;
  auto f = big_decimal_fp{buffer.data(), static_cast<int>(buffer.size()), exp};
  return write_float(out, f, specs, fspecs, loc);
}

EXPORT void preformat_float(char* out, Decimal_FP f, Float_Format_Spec fspecs) 
{
//const format_specs<char>& specs
//locale_ref loc
    int64_t significand = f.significand;
    int64_t significand_size = 0; //get_significand_size(f);
    
    bool has_decimal_point = false; //todo
    char* p = out;
    
    char decimal_point = '.';
    int sign = fspecs.sign;
    size_t size = to_unsigned(significand_size) + (sign ? 1 : 0);

    int output_exp = f.exponent + significand_size - 1;
    bool use_exp_format = false;
    {
        if (fspecs.format == FP_FORMAT_EXP) 
        {
            use_exp_format = true;
        }
        else if (fspecs.format != FP_FORMAT_GENERAL)
        {
            use_exp_format = false;
        }
        else
        {
            // Use the fixed notation if the exponent is in [exp_lower, exp_upper),
            // e.g. 0.0001 instead of 1e-04. Otherwise use the exponent notation.
            const int exp_lower = -4; 
            const int exp_upper = fspecs.precision > 0 ? fspecs.precision : 16;
            use_exp_format = output_exp < exp_lower ||
                             output_exp >= exp_upper;
        }
    }

    if (use_exp_format) 
    {
        int num_zeros = 0;
        if (fspecs.showpoint) 
        {
            num_zeros = fspecs.precision - significand_size;
            if (num_zeros < 0) 
                num_zeros = 0;
            size += num_zeros;
        } 
        else if (significand_size == 1) 
        {
            decimal_point = 0;
        }

        int abs_output_exp = abs(output_exp);
        int exp_digits = 2;
        if (abs_output_exp >= 100) 
        {
            if(abs_output_exp >= 1000)
                exp_digits = 4;
            else
                exp_digits = 3;
        }

        size += (decimal_point ? 1 : 0) + 2 + exp_digits;
        int write = 0; //[=](iterator it) 
        {
            if (sign) 
                *p++ = sign_char;
            // Insert a decimal point after the first digit and add an exponent.
            p = write_significand(p, significand, significand_size, 1, decimal_point);
            if (num_zeros > 0) 
                it = detail::fill_n(it, num_zeros, zero);

            *it++ = static_cast<char>(exp_char);
            return write_exponent<char>(output_exp, it);
        };
        if(specs.width > 0 )
        {
            write_padded<align::right>(out, specs, size, write)
        }
        else
        {
            base_iterator(out, write(reserve(out, size)));
        }
    }

    int exp = f.exponent + significand_size;
    if (f.exponent >= 0) {
        // 1234e5 -> 123400000[.0+]
        size += to_unsigned(f.exponent);
        int num_zeros = fspecs.precision - exp;
        abort_fuzzing_if(num_zeros > 5000);
        if (fspecs.showpoint) {
        ++size;
        if (num_zeros <= 0 && fspecs.format != float_format::fixed) num_zeros = 0;
        if (num_zeros > 0) size += to_unsigned(num_zeros);
        }
        auto grouping = Grouping(loc, fspecs.locale);
        size += to_unsigned(grouping.count_separators(exp));
        return write_padded<align::right>(out, specs, size, [&](iterator it) {
        if (sign) 
            *it++ = detail::sign<char>(sign);

        it = write_significand<char>(it, significand, significand_size, f.exponent, grouping);
        if (!fspecs.showpoint) 
            return it;

        *it++ = decimal_point;
        return num_zeros > 0 ? detail::fill_n(it, num_zeros, zero) : it;
        });
    } else if (exp > 0) {
        // 1234e-2 -> 12.34[0+]
        int num_zeros = fspecs.showpoint ? fspecs.precision - significand_size : 0;
        size += 1 + to_unsigned(num_zeros > 0 ? num_zeros : 0);
        auto grouping = Grouping(loc, fspecs.locale);
        size += to_unsigned(grouping.count_separators(exp));
        return write_padded<align::right>(out, specs, size, [&](iterator it) {
        if (sign) *it++ = detail::sign<char>(sign);
        it = write_significand(it, significand, significand_size, exp,
        decimal_point, grouping);
        return num_zeros > 0 ? detail::fill_n(it, num_zeros, zero) : it;
        });
    }
    // 1234e-6 -> 0.001234
    int num_zeros = -exp;
    if (significand_size == 0 && fspecs.precision >= 0 &&
    fspecs.precision < num_zeros) {
        num_zeros = fspecs.precision;
    }
    bool pointy = num_zeros != 0 || significand_size != 0 || fspecs.showpoint;
    size += 1 + (pointy ? 1 : 0) + to_unsigned(num_zeros);
    return write_padded<align::right>(out, specs, size, [&](iterator it) {
        if (sign) *it++ = detail::sign<char>(sign);
        *it++ = zero;
        if (!pointy) return it;
        *it++ = decimal_point;
        it = detail::fill_n(it, num_zeros, zero);
        return write_significand<char>(it, significand, significand_size);
    });
}

#undef TWO_DIGITS_TO_STRING
#endif
#endif // !JOT_FORMAT

#if (defined(JOT_ALL_IMPL) || defined(JOT_FORMAT_IMPL)) && !defined(JOT_FORMAT_HAS_IMPL)
#define JOT_FORMAT_HAS_IMPL
    
#endif
