#ifndef MODULE_BASE64
#define MODULE_BASE64

#include <stdint.h>
#include <stdbool.h>

#ifndef EXTERNAL
    #define EXTERNAL
#endif

typedef int64_t isize;

#define BASE64_ENCODE_PAD            1 //pads to multiple of 4 chars with pad_char. "a" will be encoded as "YQ=="
#define BASE64_DECODE_PAD_ALWAYS     1 //decoded data must be padded to 4 chars else errors. For example allows "YQ==" and disallows "YQ"
#define BASE64_DECODE_PAD_NEVER      2 //decoded data must not be padded to 4 chars else errors. For example allows "YQ" and disallows "YQ=="
#define BASE64_DECODE_PARTIAL_BYTES  4 //ignores stranded char ie. sizes of 4n + 1 without counting padding. For example both "QUFB" and "QUFB0" will decode into "AAA"  
#define BASE64_DECODE_CONCATENATED   8 //allows padding to appear not only as a suffix. For example allows decoding of "YQ==YQ==" into "aa"

//Encodes/decodes input of input_size bytes into out. out_size needs to be at least base64_encode/decode_max_size else fails immediately. 
//Returns the number bytes written to out. base64_decode takes optional finished_at_or_null argument into which will be saved the position at which the parsing finished. 
//If no error occurred *finished_at_or_null == input_size, else there was an error. 
//encoding/decoding is the table of allowed characters. It should hold decoding[encoding[i]] == i for i in [0, 64) to ensure compatibility.
//padding is the char that should be used for padding, probably '='. Flags is a combination of the flags above
EXTERNAL isize base64_encode(void* out, isize out_size, const void* input, isize input_size, const char encoding[64], char padding, uint32_t flags);
EXTERNAL isize base64_decode(void* out, isize out_size, const void* input, isize input_size, const uint8_t decoding[256], char padding, uint32_t flags, isize* finished_at_or_null);

EXTERNAL isize base64_encode_max_size(isize input_size);
EXTERNAL isize base64_decode_max_size(isize input_size);

extern const char BASE64_ENCODING_STD[]; //Uses + and / for 62,63 and = padding. RFC 4648-4 / base64    https://datatracker.ietf.org/doc/html/rfc4648#section-4
extern const char BASE64_ENCODING_URL[]; //Uses - and _ for 62,63 and = padding. RFC 4648-5 / base64url https://datatracker.ietf.org/doc/html/rfc4648#section-5
extern const uint8_t BASE64_DECODING_STD[256]; //matching decoding for BASE64_ENCODING_STD. Rejects everything else
extern const uint8_t BASE64_DECODING_URL[256]; //matching decoding for BASE64_ENCODING_URL. Rejects everything else
extern const uint8_t BASE64_DECODING_COMPAT[256]; //decoding which tries to match as many schemes as possible. Matches base64, base64url, RFC 3501, Bash https://en.wikipedia.org/wiki/Base64

#define BASE64_DECODING_ERROR_VALUE 255 //special value returned by the decoding table indicating the character is not allowed

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_BASE64_IMPL)) && !defined(MODULE_BASE64_HAS_IMPL)
#define MODULE_BASE64_HAS_IMPL

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x) assert(x)
#endif

const char BASE64_ENCODING_STD[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char BASE64_ENCODING_URL[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

EXTERNAL isize base64_encode_max_size(isize input_length)
{
    ASSERT(input_length >= 0);
    return (input_length + 2)/3 * 4;
}

EXTERNAL isize base64_decode_max_size(isize input_length)
{
    ASSERT(input_length >= 0);
    return (input_length + 3)/4 * 3;
}

EXTERNAL isize base64_encode(void* output, isize output_size, const void* input, isize input_size, const char encoding[64], char pad_char, uint32_t flags)
{
    ASSERT(input_size == 0 || (input != 0 && input_size >= 0));
    ASSERT(output_size == 0 || (output != 0 && output_size >= 0));
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
            if(flags & BASE64_ENCODE_PAD)
                *out++ = (uint8_t) pad_char;
        }
        else {
            *out++ = encoding[((in[0] & 0x03) << 4) | (in[1] >> 4)];
            *out++ = encoding[(in[1] & 0x0f) << 2];
        }
        if(flags & BASE64_ENCODE_PAD)
            *out++ = (uint8_t) pad_char;
    }

    isize written = (isize)(out - (uint8_t*) output);
    ASSERT(0 <= written && written <= base64_encode_max_size(input_size));
    return written;
}

EXTERNAL isize base64_decode(void* output, isize output_size, const void* input, isize input_size, const uint8_t decoding[256], char pad_char, uint32_t flags, isize* finished_at_or_null)
{
    ASSERT(input_size == 0 || (input != 0 && input_size >= 0));
    ASSERT(output_size == 0 || (output != 0 && output_size >= 0));
    if(output_size < base64_encode_max_size(input_size)) {
        if(finished_at_or_null) *finished_at_or_null = 0;
        return 0;
    }
    
    uint8_t* in = (uint8_t*) input;
    uint8_t* out = (uint8_t*) output;
    isize in_i = 0;
    isize out_i = 0;
    for(; in_i < input_size; ) {
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

        if(input_size % 4 != 0 && (flags & BASE64_DECODE_PAD_ALWAYS))
            break;
        
        //find the first error value, if is padding then good (and we allow it) if is something else then exit
        isize block_len = 0;
        isize block_start = in_i;
        isize block_end = in_i + 4 < input_size ? in_i + 4 : input_size;
        for(; in_i < block_end; in_i++) {
            uint8_t curr = in[in_i];
            uint8_t value = decoding[curr];
            ASSERT(in_i - block_start < 4);
            read.vals[in_i - block_start] = value;

            if(value == BASE64_DECODING_ERROR_VALUE) {
                if(curr != (uint8_t) pad_char || (flags & BASE64_DECODE_PAD_NEVER)) 
                    goto loop_end;
                break;
            }
        }

        //ensure the rest is pad char
        block_len = in_i - block_start;
        for(; in_i < block_end; in_i++)
            if(in[in_i] != (uint8_t) pad_char) 
                goto loop_end;

        //Incorrect. There remains only one byte so the input would have had only 6 bits of data. 
        if((block_len <= 1 || block_len >= 4) && (flags & BASE64_DECODE_PARTIAL_BYTES) == 0) {
            in_i = block_start;
            break;
        }
        else if(block_len == 2) {
            uint32_t n = (uint32_t) read.vals[0] << 18 | (uint32_t) read.vals[1] << 12;
            out[out_i++] = (uint8_t) (n >> 16);
        }
        else if(block_len == 3) {
            uint32_t n = (uint32_t) read.vals[0] << 18 | (uint32_t) read.vals[1] << 12 | (uint32_t) read.vals[2] << 6;
            out[out_i++] = (uint8_t) (n >> 16);
            out[out_i++] = (uint8_t) (n >> 8 & 0xFF);
        }

        //only allow the pad if we
        if(in_i != input_size && (flags & BASE64_DECODE_CONCATENATED) == 0)
            break;
    }
    loop_end:
    
    ASSERT(0 <= out_i && out_i <= base64_encode_max_size(input_size));
    ASSERT(0 <= in_i && in_i <= input_size);
    if(finished_at_or_null)
        *finished_at_or_null = in_i;
    return out_i;
}


#define EE 255
const uint8_t BASE64_DECODING_URL[256] = {
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, 62, EE, EE,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, EE, EE, EE, EE, EE, EE,
    EE,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, EE, EE, EE, EE, 63,
    EE, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
};

const uint8_t BASE64_DECODING_STD[256] = {
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, 62, EE, EE, EE, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, EE, EE, EE, EE, EE, EE,
    EE,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, EE, EE, EE, EE, EE,
    EE, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
};

const uint8_t BASE64_DECODING_COMPAT[256] = {
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, 62, 63, 62, EE, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, EE, EE, EE, EE, EE, EE,
    62,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, EE, EE, EE, EE, 63,
    EE, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
    EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE, EE,
};
#undef EE

#endif