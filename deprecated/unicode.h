#ifndef JOT_UNICODE
#define JOT_UNICODE

// Utility to simply and performantly convert between different unicode standards. 
// It contains both high level per string api and low level per codepoint api. 
// It checks all input codepoints and has option either to replace them with 
// custom sequence, skip it or to report the error. 
// This makes it ideal for quick prototyping but also proper error checking.
// See below for example.
//
// Expanded from the wonderfully simple implementation at: https://github.com/Davipb/utf8-utf16-converter
// Full credit goes to its creator <3

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifndef ASSERT
#include <assert.h>
#define ASSERT(x) assert(x)
#endif

#ifndef EXPORT
    #define EXPORT
#endif

// User can typedef their own types
// for better compatibility 
// such as using char, wchar and char32_t in c++
#ifndef UNICODE_OWN_TYPES
typedef char     utf8_t;
typedef wchar_t  utf16_t;
typedef uint32_t utf32_t;
typedef uint32_t codepoint_t;

#ifndef JOT_DEFINES
typedef int64_t  isize;            //size can be signed or unsigned
#endif
#endif

#define UNICODE_MAX 0x10FFFF                /* The highest valid Unicode codepoint */
#define UNICODE_MIN 0x0                     /* The lowest valid Unicode codepoint */
#define UNICODE_ERROR 0xFFFFFFFF            /* An invalid unicode codepoint used to signal error. */
#define UNICODE_ERROR_SKIP 0xFFFFFFFE       /* An invalid unicode codepoint used to signal to a conversion functions to skip all errors without replacing them. It is not recommended you use this. */
#define UNCIODE_INVALID 0xFFFD              /* The recommended replacment character � for conversion functions */

// =========================== HIGH LEVEL INTERFACE ===========================

// All these functions do the following:
//
// Tries to convert from an array pointed to by `source` reading up to `source_len` items. 
// If `target_or_null` is not null:
//    writes into it up to `target_max_len` converted characters
//    Returns the total number of items written into `target_or_null`.
// else 
//    Returns the needed number of `target_or_null` items for the conversion NOT including the
//    null termination.
// If `source_finished_at_or_null` is not null saves to it the position where the function finished
//   (so that it can be resumed from that point on after we increased size of `target_or_null` for example).
// The fuction read and converted all codepoints successfully iff *source_finished_at_or_null == source_len.
//    If encounters an error sequence in the `source`:
//        If `replecement` is UNICODE_ERROR_SKIP:
//           skip the character and start parsing from the next one.
//        If `replecement` is UNCIODE_INVALID or any valid codepoint_t:
//           replaces the error codepoint with `replecement`. 
//        If `replecement` is  UNICODE_ERROR or any other invalid codepoint:
//           stops parsing early and returns (thus indicating error).
//
// See examples below for usage.

EXPORT isize unicode_utf8_to_utf16(utf16_t* target_or_null, isize target_max_len, utf8_t const* source, isize source_len, isize* source_finished_at_or_null, codepoint_t replecement);
EXPORT isize unicode_utf8_to_utf32(utf32_t* target_or_null, isize target_max_len, utf8_t const* source, isize source_len, isize* source_finished_at_or_null, codepoint_t replecement);

EXPORT isize unicode_utf16_to_utf8(utf8_t* target_or_null, isize target_max_len, utf16_t const* source, isize source_len, isize* source_finished_at_or_null, codepoint_t replecement);
EXPORT isize unicode_utf16_to_utf32(utf32_t* target_or_null, isize target_max_len, utf16_t const* source, isize source_len, isize* source_finished_at_or_null, codepoint_t replecement);

EXPORT isize unicode_utf32_to_utf8(utf8_t* target_or_null, isize target_max_len, utf32_t const* source, isize source_len, isize* source_finished_at_or_null, codepoint_t replecement);
EXPORT isize unicode_utf32_to_utf16(utf16_t* target_or_null, isize target_max_len, utf32_t const* source, isize source_len, isize* source_finished_at_or_null, codepoint_t replecement);

// =========================== CODE POINT INTERFACE ===========================
 
//The maximum size of a single codepoint in different encodings. 
//Useful for overallocating buffers to skip one iteration over the data.
#define UNICOCE_CODEPOINT_MAX_LENGTH_UTF8 4
#define UNICOCE_CODEPOINT_MAX_LENGTH_UTF16 2
#define UNICOCE_CODEPOINT_MAX_LENGTH_UTF32 1

//The following functions return the needed size to encode the given codepoint in the encoding. 
//The codepoint must be valid and should be checked before calling these functions!
EXPORT isize unicode_codepoint_length_utf8(codepoint_t codepoint);
EXPORT isize unicode_codepoint_length_utf16(codepoint_t codepoint);
EXPORT isize unicode_codepoint_length_utf32(codepoint_t codepoint);

//The following functions write the representation of the provided codepoint into the provided buffer.
//Returns the number of items written. On sucess this is always at least 1.
//If the resulting codepoint size is greater than len nothing is written and the function returns 0.
//The codepoint must be valid and should be checked before calling these functions!
EXPORT isize unicode_codepoint_encode_utf8(codepoint_t codepoint, utf8_t* utf8, isize len);
EXPORT isize unicode_codepoint_encode_utf16(codepoint_t codepoint, utf16_t* utf16, isize len);
EXPORT isize unicode_codepoint_encode_utf32(codepoint_t codepoint, utf32_t* utf32, isize len);

//The following functions read a representation of a single codepoint and store it into the codepoint pointer.
//Returns the number of items read. On sucess this is always at least 1.
//If the sequence is too short or invalid writes UNICODE_ERROR into codepoint but still returns the 
// the number of characters read. This can be used to skip or correct invalid sequences.
//Returns 0 only if len is 0.
EXPORT isize unicode_codepoint_decode_utf8(codepoint_t* codepoint, const utf8_t* utf8, isize len);
EXPORT isize unicode_codepoint_decode_utf16(codepoint_t* codepoint, const utf16_t* utf16, isize len);
EXPORT isize unicode_codepoint_decode_utf32(codepoint_t* codepoint, const utf32_t* utf32, isize len);

//Casts the single character to a codepoint. 
//If fails returns UNICODE_ERROR. Useful for prototyping.
EXPORT codepoint_t unicode_codepoint_from_ascii(char c);
EXPORT codepoint_t unicode_codepoint_from_wide(wchar_t wc);

//Interprets start of NULL TERMINATED sequence as a single codepoint and returns it. 
//If fails returns UNICODE_ERROR. Useful for prototyping.
EXPORT codepoint_t unicode_codepoint_from_utf8(const char* str);
EXPORT codepoint_t unicode_codepoint_from_utf16(const wchar_t* str);
EXPORT codepoint_t unicode_codepoint_from_utf32(const utf32_t* str);

// Returns if codepoint is valid (in the valid range and not surrogate). 
// Note that even valid codepoints might not be assigned any value and thus not be printable!
EXPORT bool unicode_codepoint_is_valid(codepoint_t codepoint); 

//Returns if the codepoint is surrogate. Surrogate codepoints are not valid unicode.
EXPORT bool unicode_codepoint_is_surrogate(codepoint_t codepoint); 


// =========================== EXAMPLE ===========================
#endif

//Feel free to runt this example!
#if defined(_UNICODE_EXAMPLE) && !defined(_UNICODE_EXAMPLE_INCLUDED)
#define _UNICODE_EXAMPLE_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static void unicode_example()
{
    wchar_t utf16[] = L"Hello this is an utf16 stream with some non ascii chars: Φφ,Χχ,Ψψ,Ωω";
    isize utf16_len = wcslen(utf16);

    char utf8[512] = "";

    //Converts the the string replacing any potential errors with UNCIODE_INVALID (�)
    isize utf8_len = unicode_utf16_to_utf8((utf8_t*) utf8, sizeof(utf8), (const utf16_t*) utf16, utf16_len, NULL, UNCIODE_INVALID);

    assert(utf8_len == (isize) strlen(utf8));
    printf("String (or portion of it) converted: %s\n", utf8);
}

static void unicode_example_checks()
{
    wchar_t utf16[] = L"Hello this is an utf16 stream with some non ascii chars: Φφ,Χχ,Ψψ,Ωω";
    isize utf16_len = wcslen(utf16);

    char* utf8 = NULL;
    isize utf8_len = 0;

    isize reading_finished_at = 0;

    //Calculates the needed size for the converted string. Stops at first error.
    utf8_len = unicode_utf16_to_utf8(NULL, 0, (const utf16_t*) utf16, utf16_len, &reading_finished_at, UNICODE_ERROR);

    //Check wheter or not we have parsed the entire string
    if(reading_finished_at != utf16_len)
    {
        printf("Error the string contains malformed utf16!");
        abort();
    }

    utf8 = (char*) calloc(utf8_len + 1, sizeof(utf8_t)); //+1 for null termination
    if(utf8 == NULL)
    {
        printf("Out of memory!");
        abort();
    }
    
    //Converts the the string. We shouldnt encounter anny errors because we diddnt encouter them on the first runs. (asserts just for deomstration).
    isize new_utf8_len = unicode_utf16_to_utf8((utf8_t*) utf8, utf8_len, (const utf16_t*) utf16, utf16_len, &reading_finished_at, UNICODE_ERROR);
    assert(reading_finished_at == utf16_len);
    assert(new_utf8_len == utf8_len);

    printf("String successfully converted: %s\n", utf8);
    free(utf8);
}

#endif


// ========================= IMPLEMENTATION ===========================
#if (defined(JOT_ALL_IMPL) || defined(JOT_UNICODE_IMPL)) && !defined(JOT_UNICODE_HAS_IMPL)
#define JOT_UNICODE_HAS_IMPL

// The last codepoint of the Basic Multilingual Plane, which is the part of Unicode that
// UTF-16 can encode without surrogates
#define UNICODE_BMP_END 0xFFFF

// If a character, masked with UNICODE_GENERIC_UNICODE_SURROGATE_MASK, matches this value, it is a surrogate.
#define UNICODE_GENERIC_SURROGATE_VALUE 0xD800
// The mask to apply to a character before testing it against UNICODE_GENERIC_SURROGATE_VALUE
#define UNICODE_GENERIC_UNICODE_SURROGATE_MASK 0xF800

// If a character, masked with UNICODE_SURROGATE_MASK, matches this value, it is a high surrogate.
#define UNICODE_HIGH_SURROGATE_VALUE 0xD800
// If a character, masked with UNICODE_SURROGATE_MASK, matches this value, it is a low surrogate.
#define UNICODE_LOW_SURROGATE_VALUE 0xDC00
// The mask to apply to a character before testing it against UNICODE_HIGH_SURROGATE_VALUE or UNICODE_LOW_SURROGATE_VALUE
#define UNICODE_SURROGATE_MASK 0xFC00

// The value that is subtracted from a codepoint before encoding it in a surrogate pair
#define UNICODE_SURROGATE_CODEPOINT_OFFSET 0x10000
// A mask that can be applied to a surrogate to extract the codepoint value contained in it
#define UNICODE_SURROGATE_CODEPOINT_MASK 0x03FF
// The number of bits of UNICODE_SURROGATE_CODEPOINT_MASK
#define UNICODE_SURROGATE_CODEPOINT_BITS 10


// The highest codepoint that can be encoded with 1 byte in UTF-8
#define UNICODE_UTF8_1_MAX 0x7F
// The highest codepoint that can be encoded with 2 bytes in UTF-8
#define UNICODE_UTF8_2_MAX 0x7FF
// The highest codepoint that can be encoded with 3 bytes in UTF-8
#define UNICODE_UTF8_3_MAX 0xFFFF
// The highest codepoint that can be encoded with 4 bytes in UTF-8
#define UNICODE_UTF8_4_MAX 0x10FFFF

// If a character, masked with UNICODE_UTF8_CONTINUATION_MASK, matches this value, it is a UTF-8 continuation byte
#define UNICODE_UTF8_CONTINUATION_VALUE 0x80
// The mask to a apply to a character before testing it against UNICODE_UTF8_CONTINUATION_VALUE
#define UNICODE_UTF8_CONTINUATION_MASK 0xC0
// The number of bits of a codepoint that are contained in a UTF-8 continuation byte
#define UNICODE_UTF8_CONTINUATION_CODEPOINT_BITS 6

// Represents a UTF-8 bit pattern that can be set or verified
typedef struct
{
    // The mask that should be applied to the character before testing it
    uint8_t mask;
    // The value that the character should be tested against after applying the mask
    uint8_t value;
} _utf8_pattern;

// The patterns for leading bytes of a UTF-8 codepoint encoding
// Each pattern represents the leading byte for a character encoded with N UTF-8 bytes,
// where N is the index + 1
EXPORT const _utf8_pattern _utf8_leading_bytes[] =
{
    { 0x80, 0x00 }, // 0xxxxxxx
    { 0xE0, 0xC0 }, // 110xxxxx
    { 0xF0, 0xE0 }, // 1110xxxx
    { 0xF8, 0xF0 }  // 11110xxx
};

// The number of elements in _utf8_leading_bytes
#define UTF8_LEADING_BYTES_LEN 4

EXPORT bool unicode_codepoint_is_surrogate(codepoint_t codepoint)
{
    codepoint_t expanded_surrogate_mask = 0xFFFF0000 | UNICODE_GENERIC_UNICODE_SURROGATE_MASK;
    codepoint_t masked = codepoint & expanded_surrogate_mask;
    bool is_surrogate = masked == UNICODE_GENERIC_SURROGATE_VALUE;
    return is_surrogate;
}

EXPORT bool unicode_codepoint_is_valid(codepoint_t codepoint)
{
    bool is_in_range = UNICODE_MIN <= codepoint && codepoint <= UNICODE_MAX;
    bool is_surrogate = unicode_codepoint_is_surrogate(codepoint);
    return is_in_range && !is_surrogate;
}

EXPORT codepoint_t unicode_codepoint_from_ascii(char c)
{
    codepoint_t out = (codepoint_t) c;
    return out;
}

EXPORT codepoint_t unicode_codepoint_from_wide(wchar_t wc)
{
    uint16_t high = (uint16_t) wc;
    if ((high & UNICODE_GENERIC_UNICODE_SURROGATE_MASK) != UNICODE_GENERIC_SURROGATE_VALUE)
        return (codepoint_t) wc;
    else
        return UNICODE_ERROR;
}

EXPORT isize unicode_codepoint_decode_utf16(codepoint_t* codepoint, const utf16_t* utf16, isize len)
{
    *codepoint = UNICODE_ERROR;
    if(len <= 0)
        return 0;
        
    uint16_t high = (uint16_t) utf16[0];

    // BMP character
    if ((high & UNICODE_GENERIC_UNICODE_SURROGATE_MASK) != UNICODE_GENERIC_SURROGATE_VALUE)
    {
        *codepoint = high;
        return 1; 
    }

    // Unmatched low surrogate, invalid
    if ((high & UNICODE_SURROGATE_MASK) != UNICODE_HIGH_SURROGATE_VALUE)
        return 1;

    // String ended with an unmatched high surrogate, invalid
    if (len == 1)
        return 1;
    
    uint16_t low = (uint16_t) utf16[1];
    // Unmatched high surrogate, invalid
    if ((low & UNICODE_SURROGATE_MASK) != UNICODE_LOW_SURROGATE_VALUE)
        return 1;

    // The high bits of the codepoint are the value bits of the high surrogate
    // The low bits of the codepoint are the value bits of the low surrogate
    codepoint_t result = high & UNICODE_SURROGATE_CODEPOINT_MASK;
    result <<= UNICODE_SURROGATE_CODEPOINT_BITS;
    result |= low & UNICODE_SURROGATE_CODEPOINT_MASK;
    result += UNICODE_SURROGATE_CODEPOINT_OFFSET;
    
    // And if all else fails, it's valid
    *codepoint = result;
    return 2;
}

// Calculates the number of UTF-8 characters it would take to encode a codepoint
// The codepoint won't be checked for validity, that should be done beforehand.
EXPORT isize unicode_codepoint_length_utf8(codepoint_t codepoint)
{
    ASSERT(unicode_codepoint_is_valid(codepoint));
    // An array with the max values would be more elegant, but a bit too heavy
    // for this common function
    
    if (codepoint <= UNICODE_UTF8_1_MAX)
        return 1;

    if (codepoint <= UNICODE_UTF8_2_MAX)
        return 2;

    if (codepoint <= UNICODE_UTF8_3_MAX)
        return 3;

    return 4;
}

EXPORT isize unicode_codepoint_encode_utf8(codepoint_t codepoint, utf8_t* utf8, isize len)
{
    isize size = unicode_codepoint_length_utf8(codepoint);
    
    uint8_t* utf8_ = (uint8_t*) utf8;
    // Not enough space left on the string
    if (size > len)
        return 0;

    ASSERT(size >= 1);
    // Write the continuation bytes in reverse order first
    for (isize cont_index = size - 1; cont_index > 0; cont_index--)
    {
        uint8_t cont = (uint8_t) (codepoint & ~UNICODE_UTF8_CONTINUATION_MASK);
        cont |= UNICODE_UTF8_CONTINUATION_VALUE;

        utf8_[cont_index] = cont;
        codepoint >>= UNICODE_UTF8_CONTINUATION_CODEPOINT_BITS;
    }

    // Write the leading byte
    _utf8_pattern pattern = _utf8_leading_bytes[size - 1];

    uint8_t lead = codepoint & ~(pattern.mask);
    lead |= pattern.value;

    utf8_[0] = lead;

    return size;
}


EXPORT isize unicode_codepoint_decode_utf8(codepoint_t* codepoint, const utf8_t* utf8, isize len)
{
    *codepoint = UNICODE_ERROR;
    if(len <= 0)
        return 0;

    uint8_t leading = (uint8_t) utf8[0];

    // The number of bytes that are used to encode the codepoint
    isize encoding_len = 0;
    // The pattern of the leading byte
    _utf8_pattern leading_pattern;
    // If the leading byte matches the current leading pattern
    bool matches = false;
    
    do
    {
        encoding_len++;
        ASSERT(encoding_len >= 1);
        leading_pattern = _utf8_leading_bytes[encoding_len - 1];

        matches = (leading & leading_pattern.mask) == leading_pattern.value;

    } while (!matches && encoding_len < UTF8_LEADING_BYTES_LEN);

    // Leading byte doesn't match any known pattern, consider it invalid
    if (!matches)
        return 1;

    uint32_t codepoint_ = leading & ~leading_pattern.mask;
    
    ASSERT(encoding_len >= 1);
    isize i = 1;
    for (; i < encoding_len; i++)
    {
        // String ended before all continuation bytes were found
        // Invalid encoding
        if (i >= len)
            return i;

        uint8_t continuation = (uint8_t) utf8[i];

        // Number of continuation bytes not the same as advertised on the leading byte
        // Invalid encoding
        if ((continuation & UNICODE_UTF8_CONTINUATION_MASK) != UNICODE_UTF8_CONTINUATION_VALUE)
            return i + 1;

        codepoint_ <<= UNICODE_UTF8_CONTINUATION_CODEPOINT_BITS;
        codepoint_ |= continuation & ~UNICODE_UTF8_CONTINUATION_MASK;
    }

    isize proper_len = unicode_codepoint_length_utf8(codepoint_);

    // Overlong encoding: too many bytes were used to encode a short codepoint
    // Invalid encoding
    if (proper_len != encoding_len)
        return i;

    // Surrogates are invalid Unicode codepoints, and should only be used in UTF-16
    // Invalid encoding
    if (codepoint_ < UNICODE_BMP_END && (codepoint_ & UNICODE_GENERIC_UNICODE_SURROGATE_MASK) == UNICODE_GENERIC_SURROGATE_VALUE)
        return i;

    // UTF-8 can encode codepoints larger than the Unicode standard allows
    // Invalid encoding
    if (codepoint_ > UNICODE_MAX)
        return i;

    *codepoint = codepoint_;
    return i;
}


// Calculates the number of UTF-16 characters it would take to encode a codepoint
// The codepoint won't be checked for validity, that should be done beforehand.
EXPORT isize unicode_codepoint_length_utf16(codepoint_t codepoint)
{
    ASSERT(unicode_codepoint_is_valid(codepoint));
    if (codepoint <= UNICODE_BMP_END)
        return 1;

    return 2;
}

EXPORT isize unicode_codepoint_encode_utf16(codepoint_t codepoint, utf16_t* utf16, isize len)
{
    uint16_t* utf16_ = (uint16_t*) utf16;

    // Not enough space on the string
    if (0 >= len)
        return 0;

    if (codepoint <= UNICODE_BMP_END)
    {
        utf16_[0] = (uint16_t) codepoint;
        return 1;
    }

    // Not enough space on the string for two surrogates
    if (1 >= len)
        return 0;

    codepoint -= UNICODE_SURROGATE_CODEPOINT_OFFSET;

    uint16_t low = UNICODE_LOW_SURROGATE_VALUE;
    low |= codepoint & UNICODE_SURROGATE_CODEPOINT_MASK;

    codepoint >>= UNICODE_SURROGATE_CODEPOINT_BITS;

    uint16_t high = UNICODE_HIGH_SURROGATE_VALUE;
    high |= codepoint & UNICODE_SURROGATE_CODEPOINT_MASK;

    utf16_[0] = high;
    utf16_[1] = low;

    return 2;
}

EXPORT isize unicode_codepoint_length_utf32(codepoint_t codepoint)
{
    ASSERT(unicode_codepoint_is_valid(codepoint));
    (void) codepoint;
    return 1;
}

EXPORT isize unicode_codepoint_decode_utf32(codepoint_t* codepoint, const utf32_t* utf32, isize len)
{
    if(len <= 0)
    {
        *codepoint = UNICODE_ERROR;
        return 0;
    }
    else
    {
        *codepoint = (codepoint_t) utf32[0];
        return 1;
    }
}

EXPORT isize unicode_codepoint_encode_utf32(codepoint_t codepoint, utf32_t* utf32, isize len)
{
    if(len <= 0)
        return 0;

    uint32_t* utf32_ = (uint32_t*) utf32;
    utf32_[0] = (uint32_t) codepoint;
    return 1;
}

#define _UNICODE_CONCAT_(a, b, c, d) a ## b ## c ## d
#define _UNICODE_CONCAT4(a, b, c, d) _UNICODE_CONCAT_(a, b, c, d)
#define _UNICODE_CONCAT3(a, b, c) _UNICODE_CONCAT_(a, b, c,)
#define _UNICODE_CONCAT2(a, b) _UNICODE_CONCAT_(a, b,,)

#define UTF(x) _UNICODE_CONCAT_(utf,x,_t,)

//The code is absolutely identical for all 6 case so we make a macro.
#define _UNICODE_DEFINE_CONVERSION(from, to)                                                                                                                                                                        \
    EXPORT isize _UNICODE_CONCAT4(unicode_utf,from,_to_utf,to)(UTF(to)* write_or_null, isize write_len, UTF(from) const* read, isize read_len, isize* read_finished_at, codepoint_t replacement_policy)             \
    {                                                                                                                                                                                                               \
        isize write_index = 0;                                                                                                                                                                                      \
        isize read_index = 0;                                                                                                                                                                                       \
                                                                                                                                                                                                                    \
        for (;;)                                                                                                                                                                                                    \
        {                                                                                                                                                                                                           \
            codepoint_t codepoint = 0;                                                                                                                                                                              \
            isize read_size = 0;                                                                                                                                                                                    \
            isize write_size = 0;                                                                                                                                                                                   \
                                                                                                                                                                                                                    \
            read_size = _UNICODE_CONCAT2(unicode_codepoint_decode_utf,from)(&codepoint, read + read_index, read_len - read_index);                                                                                  \
            if(read_size <= 0)                                                                                                                                                                                      \
                break;                                                                                                                                                                                              \
                                                                                                                                                                                                                    \
            if(codepoint == UNICODE_ERROR)                                                                                                                                                                          \
            {                                                                                                                                                                                                       \
                if(replacement_policy == UNICODE_ERROR_SKIP)                                                                                                                                                        \
                {                                                                                                                                                                                                   \
                    ASSERT(read_index + read_size <= read_len);                                                                                                                                                     \
                    read_index += read_size;                                                                                                                                                                        \
                    continue;                                                                                                                                                                                       \
                }                                                                                                                                                                                                   \
                if(unicode_codepoint_is_valid(replacement_policy) == false)                                                                                                                                         \
                    break;                                                                                                                                                                                          \
                codepoint = replacement_policy;                                                                                                                                                                     \
            }                                                                                                                                                                                                       \
                                                                                                                                                                                                                    \
            ASSERT(unicode_codepoint_is_valid(codepoint));                                                                                                                                                          \
            if (write_or_null == NULL)                                                                                                                                                                              \
            {                                                                                                                                                                                                       \
                write_size = _UNICODE_CONCAT2(unicode_codepoint_length_utf,to)(codepoint);                                                                                                                          \
                ASSERT(write_size > 0 && "length can never be zero!");                                                                                                                                              \
            }                                                                                                                                                                                                       \
            else                                                                                                                                                                                                    \
            {                                                                                                                                                                                                       \
                write_size = _UNICODE_CONCAT2(unicode_codepoint_encode_utf,to)(codepoint, write_or_null + write_index, write_len - write_index);                                                                    \
                if(write_size <= 0)                                                                                                                                                                                 \
                    break;                                                                                                                                                                                          \
            }                                                                                                                                                                                                       \
                                                                                                                                                                                                                    \
            read_index += read_size;                                                                                                                                                                                \
            write_index += write_size;                                                                                                                                                                              \
        }                                                                                                                                                                                                           \
        if(read_finished_at)                                                                                                                                                                                        \
            *read_finished_at = read_index;                                                                                                                                                                         \
        return write_index;                                                                                                                                                                                         \
    }                                                                                                                                                                                                               \
    
_UNICODE_DEFINE_CONVERSION(8, 16) 
_UNICODE_DEFINE_CONVERSION(8, 32) 
_UNICODE_DEFINE_CONVERSION(16, 8) 
_UNICODE_DEFINE_CONVERSION(16, 32) 
_UNICODE_DEFINE_CONVERSION(32, 8) 
_UNICODE_DEFINE_CONVERSION(32, 16) 

#undef _UNICODE_DEFINE_CONVERSION
#undef _UNICODE_CONCAT_
#undef _UNICODE_CONCAT4
#undef _UNICODE_CONCAT3
#undef _UNICODE_CONCAT2
#undef UTF

EXPORT codepoint_t unicode_codepoint_from_utf8(const char* str)
{
    isize len = 0;
    if(str != NULL)
        while(len < 4 && str[len++] != 0);

    codepoint_t out = 0;
    unicode_codepoint_decode_utf8(&out, (const utf8_t*) str, len);
    return out;
}

EXPORT codepoint_t unicode_codepoint_from_utf16(const wchar_t* str)
{
    isize len = 0;
    if(str != NULL)
        while(len < 2 && str[len++] != 0);

    codepoint_t out = 0;
    unicode_codepoint_decode_utf16(&out, (const utf16_t*) str, len);
    return out;
}

EXPORT codepoint_t unicode_codepoint_from_utf32(const utf32_t* str)
{
    if(str == NULL || str[0] == 0)
        return UNICODE_ERROR;

    codepoint_t out = (codepoint_t) str[0];
    if(unicode_codepoint_is_valid(out))
        return out;
    else
        return UNICODE_ERROR;
}

#endif