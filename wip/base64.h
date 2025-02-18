#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifndef EXTERNAL
    #define EXTERNAL
#endif

typedef int64_t isize;


#define BASE64_NO_PAD                 1  
#define BASE64_IGNORE_PARTIAL_BYTES   2
EXTERNAL isize base64_encode(void* out, isize output_size, const void* input, isize input_size, const char encoding[64], char pad_char, uint32_t flags);
EXTERNAL isize base64_decode(void* out, isize output_size, const void* input, isize input_size, const uint8_t decoding[256], char pad_char, uint32_t flags, int64_t* error_at_or_null);

EXTERNAL isize base64_encode_max_size(isize input_size);
EXTERNAL isize base64_decode_max_size(isize input_size);

extern const char base64_encoding_std[64]; //Uses + and / for 62,63 and = padding. RFC 4648-4 / base64    https://datatracker.ietf.org/doc/html/rfc4648#section-4
extern const char base64_encoding_url[64]; //Uses - and _ for 62,63 and = padding. RFC 4648-5 / base64url https://datatracker.ietf.org/doc/html/rfc4648#section-5

#define BASE64_DECODING_ERROR_VALUE 255 //special value returned by the decoding table indicating the character is not allowed
extern const uint8_t base64_decoding_std[256]; //matching decoding for base64_encoding_std. Rejects everything else
extern const uint8_t base64_decoding_url[256]; //matching decoding for base64_encoding_url. Rejects everything else
extern const uint8_t base64_decoding_compatibility[256]; //decoding which tries to match as many schemes as possible. Matches base64, base64url, RFC 3501, Bash

//Encoding:
// "a"      -> "YQ==" when do_pad = true
// "a"      -> "YQ"   when do_pad = false

//Decoding 
//"YQ=="    -> "a" - correct
//"YQ="     -> "a" - correct only with BASE64_NO_PAD
//"YQ"      -> "a" - correct only with BASE64_NO_PAD
//"Y"       -> ""  - correct only with BASE64_IGNORE_PARTIAL_BYTES
//""        -> ""  - correct

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x) assert(x)
#endif
EXTERNAL int64_t base64_encode_max_size(int64_t input_length)
{
    ASSERT(input_length >= 0);
    return (input_length + 3)/4 * 3;
}

EXTERNAL isize base64_encode(void* output, isize output_size, const void* input, isize input_size, const char encoding[64], char pad_char, uint32_t flags)
{
    ASSERT(input_size == 0 || (input != NULL && input_size >= 0));
    ASSERT(output_size == 0 || (output != NULL && output_size >= 0));
    if(output_size < base64_encode_max_size(input_size))
        return 0;
    
    uint8_t* end = (uint8_t*) input + input_size;
    uint8_t* in = (uint8_t*) input;
    uint8_t* out = (uint8_t*) output;
    while (end - in >= 3) {
        *out++ = encoding[in[0] >> 2];
        *out++ = encoding[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *out++ = encoding[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        *out++ = encoding[in[2] & 0x3f];
        in += 3;
    }

    if (end - in) {
        *out++ = encoding[in[0] >> 2];
        if (end - in == 1) {
            *out++ = encoding[(in[0] & 0x03) << 4];
            if((flags & BASE64_NO_PAD) == 0)
                *out++ = pad_char;
        }
        else {
            *out++ = encoding[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            *out++ = encoding[(in[1] & 0x0f) << 2];
        }
        if((flags & BASE64_NO_PAD) == 0)
            *out++ = pad_char;
    }

    int64_t written = (int64_t)(out - (uint8_t*) output);
    ASSERT(0 <= written && written <= base64_encode_max_size(input_size));
    return written;
}

//this is an upper estimate! We cannot know how many '=' are in the sequence so this simply doesnt take them into account.
EXTERNAL int64_t base64_decode_max_size(int64_t input_length)
{
    ASSERT(input_length >= 0);
    int64_t out_size = (input_length + 3) / 4 * 3;
    return out_size;
}

EXTERNAL int64_t base64_decode(void* output, isize output_size, const void* input, isize input_size, const uint8_t decoding[256], char pad_char, uint32_t flags, int64_t* error_at)
{
    ASSERT(input_size == 0 || (input != NULL && input_size >= 0));
    ASSERT(output_size == 0 || (output != NULL && output_size >= 0));
    if(output_size < base64_encode_max_size(input_size))
        return 0;
    
    uint8_t* in = (uint8_t*) input;
    uint8_t* out = (uint8_t*) output;
    int64_t in_i = 0;
    int64_t out_i = 0;
    for(; in_i < input_size; in_i ++) {
        union {
            uint8_t vals[4];
            uint32_t combined;
        } read = {0};

        //decode chunks of 4 and stop on first error value
        for (; in_i + 4 <= input_size; in_i += 4) {
            read.vals[0] = decoding[in[in_i + 0]];
            read.vals[1] = decoding[in[in_i + 1]];
            read.vals[2] = decoding[in[in_i + 2]];
            read.vals[3] = decoding[in[in_i + 3]];

            //https://graphics.stanford.edu/~seander/bithacks.html#ValueInWord
            const uint32_t diff = read.combined ^ (0x01010101UL*BASE64_DECODING_ERROR_VALUE);
            if((diff - 0x01010101UL) & ~diff & 0x80808080UL)
                break;

            uint32_t n = (uint32_t) read.vals[0] << 18 | (uint32_t) read.vals[1] << 12 | (uint32_t) read.vals[2] << 6 | (uint32_t) read.vals[3] << 0;
            out[out_i++] = (uint8_t) (n >> 16);
            out[out_i++] = (uint8_t) (n >> 8);
            out[out_i++] = (uint8_t) n;
        }

        if(in_i >= input_size)
            break;

        //find the error value, if is padding then good if is something else then exit
        int64_t pad_at = 0;
        for(; in_i + pad_at < input_size && pad_at < 4; in_i++) {
            uint8_t curr = in[in_i + pad_at];
            uint8_t value = decoding[curr];
            read.vals[pad_at] = value;

            if(value == BASE64_DECODING_ERROR_VALUE) {
                if(curr != pad_char) {
                    if(error_at)
                        *error_at = in_i + pad_at;
                    return out_i;
                }
                break;
            }
        }
        
        //Incorrect. There remains only one byte so the input would have had only 6 bits of data. 
        if(pad_at == 1 && (flags & BASE64_IGNORE_PARTIAL_BYTES) == 0) {
            if(error_at)
                *error_at = in_i + pad_at;
            return out_i;
        }
        else if(pad_at == 2) {
            uint32_t n = (uint32_t) read.vals[0] << 18 | (uint32_t) read.vals[1] << 12;
            out[out_i++] = (uint8_t) (n >> 16);
        }
        else if(pad_at == 3) {
            uint32_t n = (uint32_t) read.vals[0] << 18 | (uint32_t) read.vals[1] << 12 | (uint32_t) read.vals[2] << 6;;
            out[out_i++] = (uint8_t) (n >> 16);
            out[out_i++] = (uint8_t) (n >> 8 & 0xFF);
        }
        in_i += pad_at;
    }
    
    ASSERT(0 <= out_i && out_i <= base64_encode_max_size(input_size));
    ASSERT(0 <= in_i && in_i <= input_size + 4);
    if(error_at)
        *error_at = -1;
        
    return out_i;
}