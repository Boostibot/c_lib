#pragma once

#include "_test.h"
#include "base64.h"
#include "vformat.h"

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
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(), "", "");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_utf8(), "", "");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_no_pad(), "", "");
    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "a", "");
    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "", "a");

    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(), "a", "YQ==");
    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "a", "YQ=");
    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "a", "YQ");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_no_pad(), "a", "YQ");
    
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(), "aa", "YWE=");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_no_pad(), "aa", "YWE");

    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "a", "eQ==");
    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "a", "eQ=");
    test_base64_encode(BASE64_ENCODE_NEQ, base64_encoding_url(), "a", "eQ");

    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(), "Hello world this is a text 123", "SGVsbG8gd29ybGQgdGhpcyBpcyBhIHRleHQgMTIz");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(), "non printable %^&8(6$", "bm9uIHByaW50YWJsZSAlXiY4KDYk");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(), "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_utf8(), "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_no_pad(), "non printable %^&8(6$a", "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ");
    
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_utf8(), "čšžýá", "xI3FocW+w73DoQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url(),  "čšžýá", "xI3FocW-w73DoQ==");
    test_base64_encode(BASE64_ENCODE_EQ,  base64_encoding_url_no_pad(),  "čšžýá", "xI3FocW-w73DoQ");

    //DECODE =================
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "", "");
    test_base64_decode(BASE64_DECODE_ERR, base64_decoding_universal(), "a", "");
    test_base64_decode(BASE64_DECODE_NEQ, base64_decoding_universal(), "", "a");

    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "YQ==", "a");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "YQ=", "a");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "YQ", "a");
    
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "YWE=", "aa");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "YWE", "aa");
    
    test_base64_decode(BASE64_DECODE_NEQ, base64_decoding_universal(), "eQ==", "a");
    test_base64_decode(BASE64_DECODE_NEQ, base64_decoding_universal(), "eQ=", "a");
    test_base64_decode(BASE64_DECODE_NEQ, base64_decoding_universal(), "eQ", "a");
    
    test_base64_decode(BASE64_DECODE_EQ, base64_decoding_universal(), "YQ==YQ==", "aa"); //Decoding of concatenated blocks!
    test_base64_decode(BASE64_DECODE_NEQ, base64_decoding_universal(), "YQYQ", "aa");

    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "SGVsbG8gd29ybGQgdGhpcyBpcyBhIHRleHQgMTIz", "Hello world this is a text 123");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "bm9uIHByaW50YWJsZSAlXiY4KDYk", "non printable %^&8(6$");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==", "non printable %^&8(6$a");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==bm9uIHByaW50YWJsZSAlXiY4KDYkYQ==", "non printable %^&8(6$anon printable %^&8(6$a");

    test_base64_decode(BASE64_DECODE_ERR, base64_decoding_universal(), "bm9uIHByaW50YWJs%%ZSAlXiY4KDYkYQ", "");
    test_base64_decode(BASE64_DECODE_ERR, base64_decoding_universal(), "bm9uIHByaW50YWJs*ZSAlXiY4KDYkYQ", "");
    
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "X/==", "_");
    test_base64_decode(BASE64_DECODE_EQ,  base64_decoding_universal(), "X_==", "_");
    
    //STRESS ROUNDTRIP TESTING =================
    test_base64_stress(max_seconds / 3.0, base64_encoding_url(), base64_decoding_universal());
    test_base64_stress(max_seconds / 3.0, base64_encoding_url_utf8(), base64_decoding_universal());
    test_base64_stress(max_seconds / 3.0, base64_encoding_url_no_pad(), base64_decoding_universal());

}

INTERNAL void test_base64_encode(Base64_Encode_State encode_state, Base64_Encoding encoding, const char* input, const char* expected)
{
    String input_ = string_make(input);
    String expected_ = string_make(expected);
    String_Builder encoded_buffer = builder_make(NULL, 512);

    base64_encode_into(&encoded_buffer, input_.data, input_.size, encoding);
    String ecnoded_result = encoded_buffer.string;

    TEST(string_is_equal(ecnoded_result, expected_) == (encode_state == BASE64_ENCODE_EQ));
    
    builder_deinit(&encoded_buffer);
}


INTERNAL void test_base64_decode(Base64_Decode_State decode_state, Base64_Decoding decoding, const char* input, const char* expected)
{
    String input_ = string_make(input);
    String expected_ = string_make(expected);
    String_Builder decoded_buffer = builder_make(NULL, 512);

    bool decode_ok = base64_decode_into(&decoded_buffer, input_.data, input_.size, decoding);
    String decoded_result = decoded_buffer.string;

    TEST(decode_ok == (decode_state != BASE64_DECODE_ERR));
    if(decode_ok)
    {
        TEST(string_is_equal(decoded_result, expected_) == (decode_state == BASE64_DECODE_EQ));
    }
    
    builder_deinit(&decoded_buffer);
}

INTERNAL void test_base64_stress(f64 max_seconds, Base64_Encoding encoding, Base64_Decoding decoding)
{
    enum {
        MAX_SIZE = 1024*8, 
        MAX_BLOCKS = 10,
        MAX_ITERS = 1000*1000,
        MIN_ITERS = 10,
    };

    //Try to guess enough space so we never have to reallocate
    String_Builder random_data = builder_make(NULL, MAX_SIZE*MAX_BLOCKS);
    String_Builder encoded = builder_make(NULL, base64_encode_max_output_length(random_data.capacity) + MAX_BLOCKS*10);
    String_Builder decoded = builder_make(NULL, base64_decode_max_output_length(encoded.capacity));
    String_Builder decoded_block = builder_make(NULL, base64_decode_max_output_length(encoded.capacity) / MAX_BLOCKS);
    
	f64 start = clock_s();
	for(isize i = 0; i < MAX_ITERS; i++)
	{
		if(clock_s() - start >= max_seconds && i >= MIN_ITERS)
			break;

        builder_clear(&random_data);
        builder_clear(&encoded);
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
            builder_resize(&random_data, random_data_prev_size + block_size);

            random_bytes(random_data.data + random_data_prev_size, block_size);

            //Encode the block
            isize encoded_prev_size = encoded.size;
            base64_encode_append_into(&encoded, random_data.data + random_data_prev_size, block_size, encoding);

            //Decode the encoded and test
            base64_decode_into(&decoded_block, encoded.data + encoded_prev_size, encoded.size - encoded_prev_size, decoding);
            String decoded_block_str = decoded_block.string;
            String original_block_str = {random_data.data + random_data_prev_size, block_size};
            
            TEST(string_is_equal(original_block_str, decoded_block_str), "Every encoded block must match!");
        }

        //Decode the whole concatenation of blocks and test if the data was preserved
        //if do_blocks == false => num_blocks = 1 => this would do the same thing as the per block check 
        if(encoding.do_pad) 
        {
            base64_decode_into(&decoded, encoded.data, encoded.size, decoding);
            TEST(builder_is_equal(decoded, random_data), "The whole encoded block must match!");
        }
    }
    
    builder_deinit(&random_data);
    builder_deinit(&encoded);
    builder_deinit(&decoded);
    builder_deinit(&decoded_block);
}