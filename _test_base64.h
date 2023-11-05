#pragma once

#include "_test.h"
#include "format.h"

typedef enum Base64_Encode_State{
    BASE64_ENCODE_EQ,
    BASE64_ENCODE_NEQ,
} Base64_Encode_State;

typedef enum Base64_Decode_State{
    BASE64_DECODE_EQ,
    BASE64_DECODE_ERR,
    BASE64_DECODE_NEQ,
} Base64_Decode_State;

INTERNAL void test_base64_stress(f64 max_seconds, Base64_Encoding encoding, Base64_Decoding decoding);
INTERNAL void test_base64_encode(Base64_Encode_State encode_state, Base64_Encoding encoding, const char* input, const char* expected);
INTERNAL void test_base64_decode(Base64_Decode_State decode_state, Base64_Decoding decoding, const char* input, const char* expected);

INTERNAL void test_base64(f64 max_seconds)
{
    //ENCODE =================
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL, "", "");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_UTF8, "", "");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL_NO_PAD, "", "");
    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "a", "");
    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "", "a");

    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL, "a", "YQ==");
    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "a", "YQ=");
    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "a", "YQ");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL_NO_PAD, "a", "YQ");
    
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL, "aa", "YWE=");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL_NO_PAD, "aa", "YWE");

    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "a", "eQ==");
    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "a", "eQ=");
    test_base64_encode(BASE64_ENCODE_NEQ, BASE64_ENCODING_URL, "a", "eQ");

    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL, "Hello world this is a text 123", "SGVsbG8gd29ybGQgdGhpcyBpcyBhIHRleHQgMTIz");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL, "non printable %^&8(6$", "bm9uIHByaW50YWJsZSAlXiY4KDYk");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL, "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_UTF8, "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL_NO_PAD, "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ");
    
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_UTF8, "čšžýá", "xI3FocW+w73DoQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL,  "čšžýá", "xI3FocW-w73DoQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  BASE64_ENCODING_URL_NO_PAD,  "čšžýá", "xI3FocW-w73DoQ");

    //DECODE =================
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "", "");
    test_base64_decode(BASE64_DECODE_ERR, BASE64_DECODING_UNIVERSAL, "a", "");
    test_base64_decode(BASE64_DECODE_NEQ, BASE64_DECODING_UNIVERSAL, "", "a");

    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "YQ==", "a");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "YQ=", "a");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "YQ", "a");
    
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "YWE=", "aa");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "YWE", "aa");
    
    test_base64_decode(BASE64_DECODE_NEQ, BASE64_DECODING_UNIVERSAL, "eQ==", "a");
    test_base64_decode(BASE64_DECODE_NEQ, BASE64_DECODING_UNIVERSAL, "eQ=", "a");
    test_base64_decode(BASE64_DECODE_NEQ, BASE64_DECODING_UNIVERSAL, "eQ", "a");
    
    test_base64_decode(BASE64_DECODE_EQ, BASE64_DECODING_UNIVERSAL, "YQ==YQ==", "aa"); //Decoding of concatenated blocks!
    test_base64_decode(BASE64_DECODE_NEQ, BASE64_DECODING_UNIVERSAL, "YQYQ", "aa");

    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "SGVsbG8gd29ybGQgdGhpcyBpcyBhIHRleHQgMTIz", "Hello world this is a text 123");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "bm9uIHByaW50YWJsZSAlXiY4KDYk", "non printable %^&8(6$");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==", "non printable %^&8(6$a");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==", "non printable %^&8(6$anon printable %^&8(6$a");

    test_base64_decode(BASE64_DECODE_ERR, BASE64_DECODING_UNIVERSAL, "bm9uIHByaW50YWJs%%ZSAlXiY4KDYkYQ", "");
    test_base64_decode(BASE64_DECODE_ERR, BASE64_DECODING_UNIVERSAL, "bm9uIHByaW50YWJs*ZSAlXiY4KDYkYQ", "");
    
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "X/==", "_");
    test_base64_decode(BASE64_DECODE_EQ,  BASE64_DECODING_UNIVERSAL, "X_==", "_");
    
    //STRESS ROUNDTRIP TESTING =================
    test_base64_stress(max_seconds / 3.0, BASE64_ENCODING_URL, BASE64_DECODING_UNIVERSAL);
    test_base64_stress(max_seconds / 3.0, BASE64_ENCODING_UTF8, BASE64_DECODING_UNIVERSAL);
    test_base64_stress(max_seconds / 3.0, BASE64_ENCODING_URL_NO_PAD, BASE64_DECODING_UNIVERSAL);

}

INTERNAL void test_base64_encode(Base64_Encode_State encode_state, Base64_Encoding encoding, const char* input, const char* expected)
{
    String input_ = string_make(input);
    String expected_ = string_make(expected);
    String_Builder encoded_buffer = {0};
    array_init_backed(&encoded_buffer, allocator_get_scratch(), 512);

    base64_encode_into(&encoded_buffer, input_.data, input_.size, encoding);
    String ecnoded_result = string_from_builder(encoded_buffer);

    TEST(string_is_equal(ecnoded_result, expected_) == (encode_state == BASE64_ENCODE_EQ));
    
    array_deinit(&encoded_buffer);
}


INTERNAL void test_base64_decode(Base64_Decode_State decode_state, Base64_Decoding decoding, const char* input, const char* expected)
{
    String input_ = string_make(input);
    String expected_ = string_make(expected);
    String_Builder decoded_buffer = {0};
    array_init_backed(&decoded_buffer, allocator_get_scratch(), 512);

    bool decode_ok = base64_decode_into(&decoded_buffer, input_.data, input_.size, decoding);
    String decoded_result = string_from_builder(decoded_buffer);

    TEST(decode_ok == (decode_state != BASE64_DECODE_ERR));
    if(decode_ok)
    {
        TEST(string_is_equal(decoded_result, expected_) == (decode_state == BASE64_DECODE_EQ));
    }
    
    array_deinit(&decoded_buffer);
}

INTERNAL void test_base64_stress(f64 max_seconds, Base64_Encoding encoding, Base64_Decoding decoding)
{
    enum {
        MAX_SIZE = 1024*8, 
        MAX_BLOCKS = 10,
        MAX_ITERS = 1000*1000,
        MIN_ITERS = 10,
    };

    String_Builder random_data = {0};
    String_Builder encoded = {0};
    String_Builder decoded = {0};
    String_Builder decoded_block = {0};
    
    //Try to guess enough space so we never have to reallocate
    array_reserve(&random_data, MAX_SIZE*MAX_BLOCKS);
    array_reserve(&encoded, base64_encode_max_output_length(random_data.capacity) + MAX_BLOCKS*10);
    array_reserve(&decoded, base64_decode_max_output_length(encoded.capacity));
    array_reserve(&decoded_block, base64_decode_max_output_length(encoded.capacity) / MAX_BLOCKS);
    
	f64 start = clock_s();
	for(isize i = 0; i < MAX_ITERS; i++)
	{
		if(clock_s() - start >= max_seconds && i >= MIN_ITERS)
			break;

        array_clear(&random_data);
        array_clear(&encoded);
        isize num_blocks = 1;

        //If we do_pad we also test decodaing up to MAX_BLOCKS concatenated blocks
        //Else we only test the blocks indiviually
        if(encoding.do_pad)
            num_blocks = random_range(1, MAX_BLOCKS + 1);

        for(isize j = 0; j < num_blocks; j++)
        {
            //Fill the random data block
            isize random_data_prev_size = random_data.size;
            isize block_size = random_range(0, MAX_SIZE + 1);
            array_resize(&random_data, random_data_prev_size + block_size);

            random_bytes(random_data.data + random_data_prev_size, block_size);

            //Encode the block
            isize encoded_prev_size = encoded.size;
            base64_encode_append_into(&encoded, random_data.data + random_data_prev_size, block_size, encoding);

            //Decode the encoded and test
            base64_decode_into(&decoded_block, encoded.data + encoded_prev_size, encoded.size - encoded_prev_size, decoding);
            String decoded_block_str = string_from_builder(decoded_block);
            String original_block_str = {random_data.data + random_data_prev_size, block_size};
            
            TEST_MSG(string_is_equal(original_block_str, decoded_block_str), "Every encoded block must match!");
        }

        //Decode the whole concatenation of blocks and test if the data was preserved
        //if do_blocks == false => num_blocks = 1 => this would do the same thing as the per block check 
        if(encoding.do_pad) 
        {
            base64_decode_into(&decoded, encoded.data, encoded.size, decoding);
            TEST_MSG(builder_is_equal(decoded, random_data), "The whole encoded block must match!");
        }
    }
    
    array_deinit(&random_data);
    array_deinit(&encoded);
    array_deinit(&decoded);
    array_deinit(&decoded_block);
}