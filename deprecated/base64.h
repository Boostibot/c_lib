#ifndef JOT_BASE64
#define JOT_BASE64

// A simple base64 encode/decode facility. It does not require any allocations or other system resources.
// 
// This implementation idffers from msot online in three ways:
// 1: We allow decoding of multiple concatenated encoded blocks blocks. This means "YQ==YQ==" will decode to "aa".
// 2: We do error checking in the decoding and return place of the error.
// 3: We allow to set the encoding/decoding programatically.
// 
// The encoding should be the fastest possible. 
// Decoding is a little bit slower because of our support for concatenated streams and error checking.
//
// @TODO: Add option for line breaks into encode/decode to be fully standard compliant

#include <stdint.h>

#ifndef ASSERT
#include <assert.h>
#define ASSERT(x) assert(x)
#endif

#ifndef EXTERNAL
    #define EXTERNAL
#endif

#define BASE64_ENCODING_TABLE_SIZE      64  /* the required minimum size for the encoding table*/
#define BASE64_DECODING_TABLE_SIZE      256 /* the required minimum size for the decoding table*/
#define BASE64_DECODING_ERROR_VALUE     255 /* special value returned by the decoding table indicating the character is not allowed */

typedef struct Base64_Encoding
{
    const uint8_t* encoding_table; //effcitively [0, 63] -> char function
    uint8_t pad_char; //The character used for padding the stream (see below)
    bool do_pad; //Wheter or not to pad the encoded stream with pad_char    
    
    //example:
    // "a" ~~encode~~> "YQ==" when do_pad = true
    // "a" ~~encode~~> "YQ"   when do_pad = false
    //
    // (using pad_char == '=')
} Base64_Encoding;

typedef struct Base64_Decoding
{
    const uint8_t* decoding_table; //effcitively char -> [0, 63] functions
                                   //the decoding table also uses special output value
                                   //BASE64_DECODING_ERROR_VALUE (255) to mark invalid entries 
                                   //not allowed in the encoding.

    uint8_t pad_char; //The character used for padding the stream (see below)
    bool optional_pad; //Wheter or not the pading can be ommited (see below)
    bool enable_all_stream_sizes; //If this is true enables streams that satisfy (stream_length)%4 == 1 (see below for example)
    
    //example: 
    //"YQ=="    -> "a" - corect
    //"YQ="     -> "a" - correct only if optional_pad == true
    //"YQ"      -> "a" - correct only if optional_pad == true
    //"Y"       -> ""  - correct only if enable_all_stream_sizes == true
    //""        -> ""  - correct
    
    //enable_all_stream_sizes should be false by default because the sequences which it enables lose data.
} Base64_Decoding;

//Returns the needed maximu output length of the output given the input_legth
EXTERNAL int64_t base64_encode_max_output_length(int64_t input_length);

//Returns the needed maximu output length of the output given the input_legth
EXTERNAL int64_t base64_decode_max_output_length(int64_t input_length);

//Encodes input_length bytes from data into out. 
//Out needs to be at least base64_encode_max_output_length() bytes!
//Returns the exact amountof bytes written to out.
EXTERNAL int64_t base64_encode(void* out, const void* data, int64_t input_length, Base64_Encoding encoding);

//Decodes input_length bytes from data into out. 
//If the input data is malformed (contains other characters than BASE64_DIGITS 
// or the pad '=' character at wrong places) saves index of the wrong data to error_at_or_null.
//error_at_or_null can be NULL and in that case nothing is written there. The function still stops at first error.
//Unlike other other base64 decoders we allow for multiple concatenated base64 blocks.
//That means it is valid for base64 stream to this function to contain '=' inside it as long as it makes sense
// within the previous block. The next block starts right after that normally.
//Out needs to be at least base64_encode_max_output_length() bytes!
//Returns the exact amountof bytes written to out.
EXTERNAL int64_t base64_decode(void* out, const void* data, int64_t input_length, Base64_Decoding decoding, int64_t* error_at_or_null);

//Common encodings and decodings
//Url/filesystem safe encoding. We use this for everything. Formally RFC 4648 / Base64URL
EXTERNAL const Base64_Encoding base64_encoding_url();
EXTERNAL const Base64_Encoding base64_encoding_url_no_pad();
EXTERNAL const Base64_Encoding base64_encoding_url_utf8();   //common internet encoding
EXTERNAL const Base64_Decoding base64_decoding_universal();  //Common decoding that should work for *most* base64 encodings.

//The appropriate encoding/decoding tables used by the above encodings/decodings. See implementation
EXTERNAL const uint8_t* base64_encoding_table_url();
EXTERNAL const uint8_t* base64_encoding_table_utf8();
EXTERNAL const uint8_t* base64_decoding_table_universal();

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_BASE64_IMPL)) && !defined(JOT_BASE64_HAS_IMPL)
#define JOT_BASE64_HAS_IMPL

EXTERNAL int64_t base64_encode_max_output_length(int64_t input_length)
{
    ASSERT(input_length >= 0);
    int64_t olen = (input_length + 2) / 3 * 4;
    ASSERT(olen >= input_length && "integer overflow!");
    return olen;
}

EXTERNAL int64_t base64_encode(void* _out, const void* _data, int64_t count, Base64_Encoding encoding)
{
    ASSERT(_out != NULL && _data != NULL);
    uint8_t* src = (uint8_t*) _data;
    uint8_t* end = src + count;
    uint8_t* in = src;
    uint8_t* out = (uint8_t*) _out;

    #ifndef NDEBUG
    int64_t olen = base64_encode_max_output_length(count);
    #endif // !NDEBUG

    while (end - in >= 3) {
        *out++ = encoding.encoding_table[in[0] >> 2];
        *out++ = encoding.encoding_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *out++ = encoding.encoding_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        *out++ = encoding.encoding_table[in[2] & 0x3f];
        in += 3;
    }

    if (end - in) {
        *out++ = encoding.encoding_table[in[0] >> 2];
        if (end - in == 1) {
            *out++ = encoding.encoding_table[(in[0] & 0x03) << 4];
            if(encoding.do_pad)
                *out++ = encoding.pad_char;
        }
        else {
            *out++ = encoding.encoding_table[((in[0] & 0x03) << 4) |
                (in[1] >> 4)];
                *out++ = encoding.encoding_table[(in[1] & 0x0f) << 2];
        }
        if(encoding.do_pad)
            *out++ = encoding.pad_char;
    }

    int64_t written = (int64_t)(out - (uint8_t*) _out);
    #ifndef NDEBUG
    ASSERT(0 <= written && written <= olen);
    #endif // !NDEBUG
    return written;
}

//this is an upper estimate! We cannot know how many '=' are in the sequence so this simply doesnt take them into account.
EXTERNAL int64_t base64_decode_max_output_length(int64_t input_length)
{
    ASSERT(input_length >= 0);
    int64_t out_size = (input_length + 3) / 4 * 3;
    return out_size;
}

EXTERNAL int64_t base64_decode(void* _out, const void* _data, int64_t input_length, Base64_Decoding decoding, int64_t* error_at)
{
    ASSERT(_out != NULL && _data != NULL);
    #define E BASE64_DECODING_ERROR_VALUE /* Error value */

    uint8_t* data = (uint8_t*) _data;
    uint8_t* out = (uint8_t*) _out;
    if(error_at)
        *error_at = -1;

    #ifndef NDEBUG
    int64_t max_length = base64_decode_max_output_length(input_length);
    #endif // !NDEBUG

    int64_t in_i = 0;
    int64_t out_i = 0;
    for(; in_i < input_length; in_i ++)
    {
        uint8_t values[4] = {0};

        for (; in_i + 4 <= input_length; in_i += 4)
        {
            //simply translate all values within a block
            values[0] = decoding.decoding_table[data[in_i + 0]];
            values[1] = decoding.decoding_table[data[in_i + 1]];
            values[2] = decoding.decoding_table[data[in_i + 2]];
            values[3] = decoding.decoding_table[data[in_i + 3]];

            //Check if it contains any strange characters (all of those were given the value E in the list)
            const uint32_t combined_by_bytes = values[0] << 24 | values[1] << 16 | values[2] << 8 | values[3] << 0;
            const uint32_t error_mask = (uint32_t) E << 24 | (uint32_t) E << 16 | (uint32_t) E << 8 | (uint32_t) E << 0;
            const uint32_t masked_combined = combined_by_bytes ^ error_mask;
            
            //https://graphics.stanford.edu/~seander/bithacks.html#ValueInWord
            #define haszero(v) (((v) - 0x01010101UL) & ~(v) & 0x80808080UL)

            bool has_error_value = !!haszero(masked_combined);

            #undef haszero

            if(has_error_value)
            {
                ASSERT(values[0] == E 
                    || values[1] == E
                    || values[2] == E
                    || values[3] == E);
                break;
            }

            //join them together
            uint32_t n = values[0] << 18 | values[1] << 12 | values[2] << 6 | values[3] << 0;

            //simply bit splice it into the data;
            out[out_i++] = (uint8_t) (n >> 16);
            out[out_i++] = (uint8_t) (n >> 8 & 0xFF);
            out[out_i++] = (uint8_t) (n & 0xFF);
        }

        if(in_i >= input_length)
            break;

        //find the exact location of the '=' character (or error) in the next 4 bytes 
        int64_t max_pad_i = 4;
        if(max_pad_i + in_i > input_length)
        {
            //If padding is forced the text must be in exact
            //chunks of 4 always
            if(decoding.optional_pad == false)
            {
                if(error_at)
                    *error_at = input_length;
                break;
            }

            max_pad_i = input_length - in_i;
        }

        int64_t pad_at = 0;
        for(; pad_at < max_pad_i; pad_at++)
        {
            uint8_t curr = data[in_i + pad_at];
            uint8_t value = decoding.decoding_table[curr];
            values[pad_at] = value;

            //if found the problematic value break;
            if(value == E)
            {
                //if found something that is not pad_char then it is really an error
                //set error at and return.
                if(curr != decoding.pad_char)
                {
                    if(error_at)
                        *error_at = pad_at;
                    in_i = input_length;
                }
                break;
            }
        }
        
        uint32_t n = values[0] << 18 | values[1] << 12;
        switch(pad_at)
        {
            default:
                ASSERT(false && "unreachable!"); 
                break;

            case 0:
                //Nothing. On correctly structured data this
                //shoudl be impossible but we alow extra = when it doesnt
                //change the meaning of the data.
                break;

            case 1: 
                //This is incorrect. The first output byte has only 6 bits of data.
                //However we signal an error only of decoding.enable_all_stream_sizes is false
                //Else we just ignore it.
                if(decoding.enable_all_stream_sizes == false)
                {
                    if(error_at)
                        *error_at = in_i + pad_at;
                    in_i = input_length - pad_at;
                }
                break;
                
            case 2: 
                out[out_i++] = (uint8_t) (n >> 16);
                break;

            case 3: 
                n |= values[2] << 6;
                out[out_i++] = (uint8_t) (n >> 16);
                out[out_i++] = (uint8_t) (n >> 8 & 0xFF);
                break;
        }
        
        in_i += pad_at;
    }
    
    #ifndef NDEBUG
    ASSERT(0 <= out_i && out_i <= max_length);
    ASSERT(0 <= in_i && in_i <= input_length + 4);
    #endif // !NDEBUG

    #undef E
    return out_i;
}

//========================================= TABLE DEFINITIONS ============================================
const uint8_t BASE64_ENCODING_TABLE_URL[BASE64_ENCODING_TABLE_SIZE + 1] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
const uint8_t BASE64_ENCODING_TABLE_UTF8[BASE64_ENCODING_TABLE_SIZE + 1] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const uint8_t BASE64_DECODING_TABLE_UNIVERSAL[BASE64_DECODING_TABLE_SIZE] = { 
    #define E BASE64_DECODING_ERROR_VALUE
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    //       '+' ',' '-' '.' '/'
    E, E, E, 62, 63, 62, 62, 63,
    
  //0   1   2   3   4   5   6   7
    52, 53, 54, 55, 56, 57, 58, 59,

 // 8   9
    60, 61, E, E, E, E, E, E, 

  //   A  B  C  ...
    E, 0, 1, 2, 3, 4, 5, 6, 
    7, 8, 9, 10, 11, 12, 13, 14, 
    15, 16, 17, 18, 19, 20, 21, 22, 

// ...  Y   Z               '_'
    23, 24, 25, E, E, E, E, 63, 

//     a   b   c   ...
    E, 26, 27, 28, 29, 30, 31, 32, 
    33, 34, 35, 36, 37, 38, 39, 40, 
    41, 42, 43, 44, 45, 46, 47, 48, 

//  ... y   z
    49, 50, 51, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E,
    #undef E
};



//Feel free enable to define tables for specific types 
#ifdef BASE64_SPECIFIC_DECONDINGS

const uint8_t BASE64_TABLE_DECODING_UTF8[BASE64_DECODING_TABLE_SIZE] = { 
    #define E BASE64_DECODING_ERROR_VALUE
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, 62 /* '+' */, E, E, E, 63, /* '/' */
    52, 53, 54, 55, 56, 57, 58, 59, 
    60, 61, E, E, E, E, E, E, 
    E, 0, 1, 2, 3, 4, 5, 6, 
    7, 8, 9, 10, 11, 12, 13, 14, 
    15, 16, 17, 18, 19, 20, 21, 22, 
    23, 24, 25, E, E, E, E, E, 
    E, 26, 27, 28, 29, 30, 31, 32, 
    33, 34, 35, 36, 37, 38, 39, 40, 
    41, 42, 43, 44, 45, 46, 47, 48, 
    49, 50, 51, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E,
    #undef E
};


const uint8_t BASE64_TABLE_DECODING_URL[BASE64_DECODING_TABLE_SIZE] = { 
    #define E BASE64_DECODING_ERROR_VALUE
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, 62, /* '-' */E, E, 
    52, 53, 54, 55, 56, 57, 58, 59, 
    60, 61, E, E, E, E, E, E, 
    E, 0, 1, 2, 3, 4, 5, 6, 
    7, 8, 9, 10, 11, 12, 13, 14, 
    15, 16, 17, 18, 19, 20, 21, 22, 
    23, 24, 25, E, E, E, E, 63, /* '_' */
    E, 26, 27, 28, 29, 30, 31, 32, 
    33, 34, 35, 36, 37, 38, 39, 40, 
    41, 42, 43, 44, 45, 46, 47, 48, 
    49, 50, 51, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E, 
    E, E, E, E, E, E, E, E,
    #undef E
};

#endif

EXTERNAL const uint8_t* base64_encoding_table_url()
{
    return BASE64_ENCODING_TABLE_URL;
}
EXTERNAL const uint8_t* base64_encoding_table_utf8()
{
    return BASE64_ENCODING_TABLE_UTF8;
}
EXTERNAL const uint8_t* base64_decoding_table_universal()
{
    return BASE64_DECODING_TABLE_UNIVERSAL;
}

EXTERNAL const Base64_Encoding base64_encoding_url()
{
    Base64_Encoding out = {BASE64_ENCODING_TABLE_URL, '=', true};
    return out;
}
EXTERNAL const Base64_Encoding base64_encoding_url_no_pad()
{
    Base64_Encoding out = {BASE64_ENCODING_TABLE_URL, '=', false};
    return out;
}
EXTERNAL const Base64_Encoding base64_encoding_url_utf8()
{
    Base64_Encoding out = {BASE64_ENCODING_TABLE_UTF8, '=', true};
    return out;
}
EXTERNAL const Base64_Decoding base64_decoding_universal()
{
    Base64_Decoding out = {BASE64_DECODING_TABLE_UNIVERSAL, '=', true, false};
    return out;
}

#endif