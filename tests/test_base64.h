#pragma once

#include "../base64.h"
#include "../string.h"
#include "../time.h"
#include "../random.h"
#include "../allocator.h"
#include "../assert.h"

#ifndef INTERNAL
    #define INTERNAL static inline 
#endif

INTERNAL void test_base64_encode(bool success, const char encoding[64], char padding, uint32_t flags, const char* input, const char* output);
INTERNAL void test_base64_decode(bool success, const uint8_t decoding[256], char padding, uint32_t flags, const char* input, const char* output, isize should_finish_at_or_begative);

INTERNAL void test_base64_unit()
{
    //ENCODE =================
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "", "");
    test_base64_encode(true,  BASE64_ENCODING_STD, '=', BASE64_ENCODE_PAD, "", "");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', 0, "", "");
    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "");
    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "", "a");

    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "YQ==");
    test_base64_encode(true,  BASE64_ENCODING_URL, '%', BASE64_ENCODE_PAD, "a", "YQ%%");
    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "YQ=");
    test_base64_encode(false, BASE64_ENCODING_URL, '%', BASE64_ENCODE_PAD, "a", "YQ%");
    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "YQ");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', 0, "a", "YQ");
    
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "aa", "YWE=");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', 0, "aa", "YWE");

    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "eQ==");
    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "eQ=");
    test_base64_encode(false, BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "a", "eQ");

    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "Hello world this is a text 123", "SGVsbG8gd29ybGQgdGhpcyBpcyBhIHRleHQgMTIz");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "non printable %^&8(6$", "bm9uIHByaW50YWJsZSAlXiY4KDYk");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD, "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==");
    test_base64_encode(true,  BASE64_ENCODING_STD, '=', BASE64_ENCODE_PAD, "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', 0, "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ");
    
    test_base64_encode(true,  BASE64_ENCODING_STD, '=', BASE64_ENCODE_PAD, "čšžýá", "xI3FocW+w73DoQ==");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', BASE64_ENCODE_PAD,  "čšžýá", "xI3FocW-w73DoQ==");
    test_base64_encode(true,  BASE64_ENCODING_URL, '=', 0,  "čšžýá", "xI3FocW-w73DoQ");

    //DECODE =================
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "", "", -1);
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "a", "", 0);
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "", "a", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', BASE64_DECODE_PARTIAL_BYTES, "a", "", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', BASE64_DECODE_PARTIAL_BYTES, "QUFB0", "AAA", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', BASE64_DECODE_PARTIAL_BYTES, "QUFB", "AAA", -1);

    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "YQ==", "a", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "YQ=", "a", -1);
    test_base64_decode(false,  BASE64_DECODING_COMPAT, '=', BASE64_DECODE_PAD_ALWAYS, "YQ=", "a", 0);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "YQ", "a", -1);
    
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "YWE=", "aa", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "YWE", "aa", -1);
    test_base64_decode(true,  BASE64_DECODING_STD,    '=', 0, "xI3FocW+w73DoQ==", "čšžýá", -1);
    test_base64_decode(true,  BASE64_DECODING_URL,    '=', 0, "xI3FocW-w73DoQ==", "čšžýá", -1);
    
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "eQ==", "a", -1);
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "eQ=", "a", -1);
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "eQ", "a", -1);
    
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "YQ==YQ==", "aa", 4); 
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', BASE64_DECODE_CONCATENATED, "YQ==YQ==", "aa", -1); 
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "YQYQ", "aa", -1);

    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "SGVsbG8gd29ybGQgdGhpcyBpcyBhIHRleHQgMTIz", "Hello world this is a text 123", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "bm9uIHByaW50YWJsZSAlXiY4KDYk", "non printable %^&8(6$", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==", "non printable %^&8(6$a", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', BASE64_DECODE_CONCATENATED, "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==", "non printable %^&8(6$anon printable %^&8(6$a", -1);
    
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "bm9uIHByaW50YWJs%%ZSAlXiY4KDYkYQ", "", 16);
    test_base64_decode(false, BASE64_DECODING_COMPAT, '=', 0, "bm9uIHByaW50YWJs*ZSAlXiY4KDYkYQ", "", 16);
    
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "X/==", "_", -1);
    test_base64_decode(true,  BASE64_DECODING_COMPAT, '=', 0, "X_==", "_", -1);
}

INTERNAL void test_base64_encode(bool success, const char encoding[64], char padding, uint32_t flags, const char* in, const char* out)
{
    String input = string_of(in);
    String expected = string_of(out);
    String_Builder encoded = builder_make(allocator_get_default(), 0);
    builder_resize(&encoded, base64_encode_max_size(input.count), 0);

    isize size = base64_encode(encoded.data, encoded.count, input.data, input.count, encoding, padding, flags);
    TEST(0 <= size && size <= encoded.count);
    builder_resize(&encoded, size, 0);

    TEST(string_is_equal(encoded.string, expected) == success);
    builder_deinit(&encoded);
}

INTERNAL void test_base64_decode(bool success, const uint8_t decoding[256], char padding, uint32_t flags, const char* in, const char* out, isize should_finish_at_or_begative)
{
    String input = string_of(in);
    String expected = string_of(out);
    String_Builder encoded = builder_make(allocator_get_default(), 0);
    builder_resize(&encoded, base64_encode_max_size(input.count), 0);

    isize finished_at = 0;
    isize size = base64_decode(encoded.data, encoded.count, input.data, input.count, decoding, padding, flags, &finished_at);
    TEST(0 <= size && size <= encoded.count);
    builder_resize(&encoded, size, 0);

    if(should_finish_at_or_begative < 0)
        TEST(string_is_equal(encoded.string, expected) == success);
    else
        TEST(finished_at == should_finish_at_or_begative);

    builder_deinit(&encoded);
}

INTERNAL void test_base64_stress(double max_seconds)
{
    enum {
        MAX_SIZE = 256, 
        MAX_BLOCKS = 10,
        ITERS_WITH_ENCODING = 10,
    };

    String_Builder input = builder_make(allocator_get_default(), 0);
    String_Builder encoded = builder_make(allocator_get_default(), 0);
    String_Builder decoded = builder_make(allocator_get_default(), 0);
    
	double start_time = clock_sec();
	for(isize iter = 0; clock_sec() - start_time < max_seconds; iter++)
	{
        //make encoding/decoding tables
        char encoding[256] = {0};
        for(uint32_t i = 0; i < 256; i++)
            encoding[i] = (char)i;

        random_shuffle(encoding, 256, 1);

        uint8_t decoding[256] = {0};
        memset(decoding, -1, sizeof decoding);
        for(uint8_t i = 0; i < 64; i++)
            decoding[(uint8_t) encoding[i]] = i;

        //test a few times using this table
        for(isize k = 0; k < ITERS_WITH_ENCODING; k++)
        {
            //prepare the flags
            char padding = encoding[64]; //random char not in the encoding itself
            bool do_pad = random_bool();
            bool do_partial_bytes = random_bool();
            isize num_blocks = do_pad ? random_range(1, MAX_BLOCKS + 1) : 1;
            uint32_t encode_flags = do_pad ? BASE64_ENCODE_PAD : 0;
            uint32_t decode_flags = 0;
            decode_flags |= do_partial_bytes ? BASE64_DECODE_PARTIAL_BYTES : 0;
            decode_flags |= num_blocks > 1 ? BASE64_DECODE_CONCATENATED : 0;
            if(random_bool()) 
                decode_flags |= do_pad ? BASE64_DECODE_PAD_ALWAYS : BASE64_DECODE_PAD_NEVER;
            
            //test a block concatenation
            builder_clear(&input);
            builder_clear(&encoded);
            builder_clear(&decoded);
            for(isize j = 0; j < num_blocks; j++)
            {
                //Fill the random data block
                isize start = input.count;
                isize block_size = random_range(0, MAX_SIZE + 1);
                builder_resize(&input, start + block_size, 0);
                random_bytes(input.data + start, block_size);

                //Encode the block
                isize encoded_prev_size = encoded.count;
                builder_resize(&encoded, encoded_prev_size + base64_encode_max_size(block_size), 0);
                isize encoded_size = base64_encode(encoded.data + encoded_prev_size, encoded.count - encoded_prev_size, input.data + start, input.count - start, encoding, padding, encode_flags);
                TEST(0 <= encoded_size && encoded_size <= encoded.count - encoded_prev_size);
                TEST(do_pad == false || encoded_size % 4 == 0);
                builder_resize(&encoded, encoded_size + encoded_prev_size, 0);

                //Decode the encoded and test
                isize finished_at = 0;
                builder_resize(&decoded, base64_encode_max_size(encoded.count), 0);
                isize decoded_size = base64_decode(decoded.data, decoded.count, encoded.data, encoded.count, decoding, padding, decode_flags, &finished_at);
                TEST(0 <= decoded_size && decoded_size <= decoded.count);
                TEST(finished_at == encoded.count);
                builder_resize(&decoded, decoded_size, 0);

                TEST(string_is_equal(input.string, decoded.string));
            }
        }
    }
    
    builder_deinit(&input);
    builder_deinit(&encoded);
    builder_deinit(&decoded);
}


INTERNAL void test_base64(double max_seconds)
{
    test_base64_unit();
    test_base64_stress(max_seconds);
}