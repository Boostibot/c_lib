#ifndef MODULE_UTF
#define MODULE_UTF

#include <stdint.h>
#include <stdbool.h>

typedef int64_t isize;

#ifndef EXTERNAL
    #define EXTERNAL
#endif

//Some simple functions to decode and encode the utf formats. 
//The functions are designed to be easily used in a loop reading or writing a single codepoint at a time.
//I havent spend much effort optimizing these, still they should be decently fast: have fast paths for ascii, have no loops. 
// In case of utf8 I have spend some effort to only do one if to verify validity of all continuation bytes within a codepoint.  
//All functions here have been verified against every possible codepoint/4 byte sequence
// to succeed or fail precisely when they should according to the spec. 

#define UTF_MAX           0x10FFFF //maximum value of unicode codepoint - anything greater is invalid 
#define UTF_REPLACEMENT   0xFFFD   //unicode value which should be used for badly parsed encoding
#define UTF_ENDIAN_LITTLE 0        
#define UTF_ENDIAN_BIG    1  

//Read a code point in the give encoding starting at *index. 
// On success a valid code point is saved, index is advanced and returns true.
// On failure a code point is set to -1 and returns false. If failure is caused by index >= code point is set to 0 instead.
EXTERNAL bool utf8_decode(const void* input, isize input_size, uint32_t* out_code_point, isize* index);
EXTERNAL bool utf16_decode(const void* input, isize input_size, uint32_t* out_code_point, isize* index, uint32_t endian);
EXTERNAL bool utf32_decode(const void* input, isize input_size, uint32_t* out_code_point, isize* index, uint32_t endian);

//Write a code point in the give encoding starting at *index. If code point is invalid or index >= output_size returns false.
//Else writes it into output, advances index and returns true.
EXTERNAL bool utf8_encode(void* output, isize output_size, uint32_t code_point, isize* index);
EXTERNAL bool utf16_encode(void* output, isize output_size, uint32_t code_point, isize* index, uint32_t endian);
EXTERNAL bool utf32_encode(void* output, isize output_size, uint32_t code_point, isize* index, uint32_t endian);

//returns if the given codepoint is valid and can be encoded in utf8-utf32. 
//This doesnt mean it has assigned unicode meaning or that it will correctly render on screen.
EXTERNAL bool utf_is_valid_codepoint(uint32_t code_point);

#endif MODULE_UTF

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_UTF)) && !defined(MODULE_HAS_IMPL_UTF)
#define MODULE_HAS_IMPL_UTF

EXTERNAL bool utf_is_valid_codepoint(uint32_t code_point)
{
    //is not surrogate and is inside the range
    return ((0xD800 > code_point || code_point > 0xDFFF) && code_point <= 0x10FFFF);
}

//built from the spec and verified against every single value
EXTERNAL bool utf8_decode(const void* input, isize input_size, uint32_t* out_code_point, isize* index)
{
    uint8_t* in = (uint8_t*) input + *index;
    isize rem = input_size - *index;
    if(rem < 1) {
        *out_code_point = 0;
        return false;
    }

    uint8_t first = in[0];
    if(first <= 0x7F) {
        *out_code_point = first;
        *index += 1;
        return true;
    } 

    //the error handling strategy here is to not check individual bytes, instead accumulate the
    // errors into code_error 
    uint32_t code_point_len = 0;
    uint32_t code_point_min = 0;
    uint32_t code_point_max = 0;
    uint32_t code_point = 0;
    uint32_t code_error = 0;
    if ((first & 0xF0) < 0xE0) {
        if(rem < 2) goto error;
        code_point_len = 2;
        code_point_min = 0x80;
        code_point_max = 0x07FF;
        code_error = (in[1] ^ 0x80u);
        code_point = (first ^ 0xC0u) << 6 | (in[1] ^ 0x80u);
    } 
    else if ((first & 0xF0) == 0xE0) {
        if(rem < 3) goto error;
        code_point_len = 3;
        code_point_min = 0x0800;
        code_point_max = 0xFFFF;
        code_error = (in[1] ^ 0x80u) | (in[2] ^ 0x80u);
        code_point = (first ^ 0xE0u) << 12 | (in[1] ^ 0x80u) << 6 | (in[2] ^ 0x80u);
        
        //UTF-16 surrogates are invalid in UTF-8 
        //(we can check just here since other branches cannot produce these values) 
        if (0xD800 <= code_point && code_point <= 0xDFFF) 
            goto error;
    } 
    else {
        if(rem < 4) goto error;
        code_point_len = 4;
        code_point_min = 0x10000;
        code_point_max = 0x10FFFF;
        code_error = (in[1] ^ 0x80u) | (in[2] ^ 0x80u) | (in[3] ^ 0x80u);
        code_point = (first ^ 0xF0u) << 18 | (in[1] ^ 0x80u) << 12 | (in[2] ^ 0x80u) << 6 | (in[3] ^ 0x80u);
    }

    //if sequence too long or error happened
    if (code_point_min > code_point || code_point > code_point_max || code_error > 0x3F)
        goto error;

    *index += code_point_len;
    *out_code_point = code_point;
    return true;

    error:
    *out_code_point = (uint32_t)-1;
    return false;
}

//adpated from: https://gist.github.com/MightyPork/52eda3e5677b4b03524e40c9f0ab1da5
EXTERNAL bool utf8_encode(void* output, isize output_size, uint32_t code_point, isize* index) {
    uint8_t* out = (uint8_t*) output + *index;
    isize rem = output_size - *index;

    //if ((0xD800 <= code_point && code_point <= 0xDFFF) || code_point > 0x10FFFF || rem < 4)  
    if (code_point <= 0x7F) {
        if(rem < 1) return false;
        out[0] = (uint8_t) code_point;
        *index += 1;
        return true;
    }
    else if (code_point <= 0x07FF) {
        if(rem < 2) return false;
        out[0] = (uint8_t) (((code_point >> 6) & 0x1F) | 0xC0);
        out[1] = (uint8_t) (((code_point >> 0) & 0x3F) | 0x80);
        *index += 2;
        return true;
    }
    else if (code_point <= 0xFFFF) {
        if(rem < 3) return false;
        //if is surrogate, error
        if(0xD800 <= code_point && code_point <= 0xDFFF)
            return false;
        out[0] = (uint8_t) (((code_point >> 12) & 0x0F) | 0xE0);
        out[1] = (uint8_t) (((code_point >>  6) & 0x3F) | 0x80);
        out[2] = (uint8_t) (((code_point >>  0) & 0x3F) | 0x80);
        *index += 3;
        return true;
    }
    else if (code_point <= 0x10FFFF) {
        if(rem < 4) return false;
        out[0] = (uint8_t) (((code_point >> 18) & 0x07) | 0xF0);
        out[1] = (uint8_t) (((code_point >> 12) & 0x3F) | 0x80);
        out[2] = (uint8_t) (((code_point >>  6) & 0x3F) | 0x80);
        out[3] = (uint8_t) (((code_point >>  0) & 0x3F) | 0x80);
        *index += 4;
        return true;
    }

    return false;
}

//see: https://www.ietf.org/rfc/rfc2781.txt
EXTERNAL bool utf16_decode(const void* input, isize input_size, uint32_t* out_code_point, isize* index, uint32_t endian)
{
    uint8_t* in = (uint8_t*) input + *index;
    isize rem = input_size - *index;
    if(rem < 2) {
        *out_code_point = rem == 0 ? 0 : (uint32_t) -1;
        return false;
    }

    uint32_t w1 = endian ? ((uint32_t) in[0] << 8 | in[1]) : ((uint32_t) in[1] << 8 | in[0]);
    if(0xD800 > w1 || w1 > 0xDFFF) {
        *out_code_point = w1;
        *index += 2;
        return true;
    }
       
    *out_code_point = (uint32_t) -1;
    if(0xD800 > w1 || w1 > 0xDBFF || rem < 4)
        return false;
        
    uint32_t w2 = endian ? ((uint32_t) in[2] << 8 | in[3]) : ((uint32_t) in[3] << 8 | in[2]);
    if(0xDC00 > w2 || w2 > 0xDFFF)
        return false;
        
    uint32_t code_point = (w1 & 0x3FF) << 10 | (w2 & 0x3FF);
    code_point += 0x10000;

    *out_code_point = code_point;
    *index += 4;
    return true;
}

//see: https://www.ietf.org/rfc/rfc2781.txt
EXTERNAL bool utf16_encode(void* output, isize output_size, uint32_t code_point, isize* index, uint32_t endian)
{
    uint8_t* out = (uint8_t*) output + *index;
    isize rem = output_size - *index;
    if(rem < 2)
        return false;
        
    if(code_point < 0x10000)
    {   
        //if is surrogate error
        if (0xD800 <= code_point && code_point <= 0xDFFF)  
            return false;

        if(endian) {
            out[0] = (uint8_t) (code_point >> 8); 
            out[1] = (uint8_t) (code_point & 0xFF); 
        }
        else {
            out[0] = (uint8_t) (code_point & 0xFF); 
            out[1] = (uint8_t) (code_point >> 8); 
        }
        *index += 2;
        return true;
    }
    
    //if is too big or not enough space, return error
    if (code_point > 0x10FFFF || rem < 4)  
        return false;

    uint32_t prime = code_point - 0x10000;
    uint32_t w1 = 0xD800 | (prime >> 10);
    uint32_t w2 = 0xDC00 | (prime & 0x3FF);

    if(endian) {
        out[0] = (uint8_t) (w1 >> 8); 
        out[1] = (uint8_t) (w1 & 0xFF); 
        out[2] = (uint8_t) (w2 >> 8); 
        out[3] = (uint8_t) (w2 & 0xFF); 
    }
    else {
        out[0] = (uint8_t) (w1 & 0xFF); 
        out[1] = (uint8_t) (w1 >> 8); 
        out[2] = (uint8_t) (w2 & 0xFF); 
        out[3] = (uint8_t) (w2 >> 8); 
    }
    
    *index += 4;
    return true;
}

//simple little/big endian serialization
EXTERNAL bool utf32_decode(const void* input, isize input_size, uint32_t* out_code_point, isize* index, uint32_t endian)
{
    uint8_t* in = (uint8_t*) input + *index;
    isize rem = input_size - *index;
    if(rem < 4) {
        *out_code_point = rem == 0 ? 0 : (uint32_t) -1;
        return false;
    }
    
    uint32_t code_point = 0;
    if(endian) {
        code_point |= (uint32_t) in[0] << 24;
        code_point |= (uint32_t) in[1] << 16;
        code_point |= (uint32_t) in[2] << 8;
        code_point |= (uint32_t) in[3];
    }
    else {
        code_point |= (uint32_t) in[0];
        code_point |= (uint32_t) in[1] << 8;
        code_point |= (uint32_t) in[2] << 16;
        code_point |= (uint32_t) in[3] << 24;
    }

    if ((0xD800 <= code_point && code_point <= 0xDFFF) || code_point > 0x10FFFF || rem < 4)  {
        *out_code_point = (uint32_t) -1;
        return false;
    }

    *out_code_point = code_point;
    *index += 4;
    return true;
}

EXTERNAL bool utf32_encode(void* output, isize output_size, uint32_t code_point, isize* index, uint32_t endian)
{
    uint8_t* out = (uint8_t*) output + *index;
    isize rem = output_size - *index;
    if ((0xD800 <= code_point && code_point <= 0xDFFF) || code_point > 0x10FFFF || rem < 4)  
        return false;
    
    if(endian) {
        out[0] = (uint8_t) (code_point >> 24); 
        out[1] = (uint8_t) (code_point >> 16); 
        out[2] = (uint8_t) (code_point >> 8); 
        out[3] = (uint8_t) (code_point); 
    }
    else {
        out[0] = (uint8_t) (code_point); 
        out[1] = (uint8_t) (code_point >> 8); 
        out[2] = (uint8_t) (code_point >> 16); 
        out[3] = (uint8_t) (code_point >> 24); 
    }

    *index += 4;
    return true;
}

#endif
