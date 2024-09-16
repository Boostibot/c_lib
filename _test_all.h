#ifndef JOT_TEST_ALL_H
#define JOT_TEST_ALL_H

#if defined(TEST_RUNNER)
#define JOT_ALL_IMPL
#endif

#define JOT_ALL_TEST
#include "profile.h"
#include "list.h"
#include "path.h"
#include "allocator_tlsf.h"
#include "slz4.h"
#include "perf.h"
#include "sort.h"

#include "_test_string.h"
#include "_test_random.h"
#include "_test_arena.h"
#include "_test_array.h"
#include "_test_hash.h"
#include "_test_log.h"
#include "_test_math.h"
#include "_test_stable_array.h"
#include "_test_lpf.h"
#include "_test_image.h"
#include "_test_string_map.h"

typedef enum Sample_Type {
    SAMPLE_TYPE_TIME = 0,
    SAMPLE_TYPE_I64 = 1,
    SAMPLE_TYPE_U64 = 2,
    SAMPLE_TYPE_F64 = 3,
    SAMPLE_TYPE_CUSTOM = 4,
} Sample_Type;

typedef union Sample_Header {
    struct {
        u8 index_len: 2;
        u8 start_len: 3;
        u8 value_len: 3;
    };
    u8 compressed;
} Sample_Header;

typedef struct Sample {
    Sample_Type type;
    u32 index;
    u64 start;
    union {
        u64 duration;
        i64 val_i64;
        u64 val_u64;
        f64 val_f64;
    };
} Sample;

void write_value(u8* into, u64 val, u32 size)
{
    u8* source = (u8*) (void*) &val;
    switch(size)
    {
        case 8: *(into++) = *(source++);
        case 7: *(into++) = *(source++);
        case 6: *(into++) = *(source++);
        case 5: *(into++) = *(source++);
        case 4: *(into++) = *(source++);
        case 3: *(into++) = *(source++);
        case 2: *(into++) = *(source++);
        case 1: *(into++) = *(source++);
    }
}

uint32_t get_len1(uint64_t val)
{
    if(val == 0)
        return 0;

    uint32_t len = (uint32_t) platform_find_last_set_bit64(val) + 1;
    uint32_t compressed = (len + 7)/8;
    compressed -= compressed == 8;
    return compressed;
}

uint32_t get_len2(uint64_t val)
{
    uint32_t len = (uint32_t) platform_find_last_set_bit64(val << 1 | val | 1);
    uint32_t compressed = (len + 7)/8;
    compressed -= compressed == 8;
    return compressed;
}

INTERNAL void test_get_len_single(uint64_t val)
{
    uint32_t len1 = get_len1(val);
    uint32_t len2 = get_len2(val);
    TEST(len1 == len2);
}

INTERNAL void test_get_len(uint64_t val)
{
    test_get_len_single(0);
    test_get_len_single(1);
    test_get_len_single(2);
    test_get_len_single(0xFF);
    test_get_len_single(1000);
    test_get_len_single(0x54a5b435f63e4);
    test_get_len_single(1ULL << 63);
    test_get_len_single(1ULL << 63 | 0xFF);
}

//Adapted from https://cbloomrants.blogspot.com/2014/03/03-14-14-fold-up-negatives.html
u64 fold_up_negatives64(i64 i)
{
    u64 two_i = ((u64)i) << 1;
    i64 sign_i = i >> 63;
    return two_i ^ sign_i;
}

i64 unfold_negatives64(u64 i)
{
    u64 half_i = i >> 1;
    i64 sign_i = - (i64)( i & 1 );
    return half_i ^ sign_i;
}

u32 fold_up_negatives32(i32 i)
{
    u32 two_i = ((u32)i) << 1;
    i32 sign_i = i >> 31;
    return two_i ^ sign_i;
}

i32 unfold_negatives32(u32 i)
{
    u32 half_i = i >> 1;
    i32 sign_i = - (i32)( i & 1 );
    return half_i ^ sign_i;
}

isize sample_compress(u32* last_index, u64* last_time, u64* last_value, Sample sample, u8* stream)
{
    u32 index_delta = fold_up_negatives32((i64) sample.index - (i64) *last_index);
    u64 start_delta = fold_up_negatives64((i64) sample.start - (i64) *last_time);

    u64 value_delta = 0;
    u32 value_compressed_len = 0;
    u32 value_decompressed_len = 0;
    if(sample.type == SAMPLE_TYPE_TIME || sample.type == SAMPLE_TYPE_I64)
    {
        i64 signed_value_delta = sample.val_i64 - (i64) *last_value;
        value_delta = fold_up_negatives64(signed_value_delta);

        u32 value_precise_len = platform_find_last_set_bit64(value_delta << 1 | value_delta | 1);
        value_compressed_len = (value_precise_len + 7)/8;
        value_compressed_len -= value_compressed_len == 8;
        value_decompressed_len = value_compressed_len + (value_compressed_len == 7);

        *last_value = sample.val_u64;
    }
    else if(sample.type == SAMPLE_TYPE_F64)
    {
        #define MASK(x) ((1ULL << x) - 1)
        u64 curr = sample.val_u64;
        u64 prev = *last_value;
        
        if(curr != prev)
        {
            u64 curr_sign = curr >> 63;
            u64 curr_exp = curr >> 52 & MASK(11);
		    u64 curr_mantissa = curr & MASK(52);
        
            u64 prev_sign = prev >> 63;
            u64 prev_exp = prev >> 52 & MASK(11);
		    u64 prev_mantissa = prev & MASK(52);

		    u64 exp_delta = fold_up_negatives64((i64) curr_exp - (i64) prev_exp);
		    u64 mantissa_delta = fold_up_negatives64((i64) curr_mantissa - (i64) prev_mantissa);
		    //u64 mantissa_delta = curr_mantissa ^ prev_mantissa;
            u64 exp_sign_delta = (exp_delta << 1) | (curr_sign ^ prev_sign);

            //we do exponent sizes: [0, 3, 6, 8, 9, 10, 11, 12] and calculate mantissa sizes to fill remaining bits
            // to full bytes. 6 is for half floats, 8 is for AMD float24, 9 is for float, 12 ios for double.
            // By doing this we ensure that half float will always be represented in max 2 bytes, float24 in max 3
            // and float in max 4 bytes.

            //(A)
            //expo: [0, 3,  6,  8,  9, 12, 12, 12] 
            //mant: [0, 5, 10, 16, 23, 28, 36, 52]
            
            //(B)
            //expo: [0, 3,  6,  8,  9, 10, 11, 12] 
            //mant: [0, 5, 10, 16, 23, 30, 37, 52]
            static const u8 exp_compressed_len_to_size[]      = {0, 3,  6,  8,  9, 12, 12, 12};
            static const u8 mantissa_compressed_len_to_size[] = {0, 5, 10, 16, 23, 28, 36, 52};

            static const u8 exp_size_to_compressed_len[] = {0, 1,1,1, 2,2,2, 3,3, 4, 5,5,5}; 
            static const u8 mantissa_size_to_compressed_len[] = {
                0,                              //
                1,1,1,1,1,                      //+5 = 5
                2,2,2,2,2,                      //+5 = 10
                3,3,3,3,3,3,                    //+6 = 16
                4,4,4,4,4,4,4,                  //+7 = 23
                5,5,5,5,5,                      //+5 = 28
                6,6,6,6,6,6,6,6,                //+8 = 36
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,//+16 = 52
            }; 

            u32 exp_compressed_len = 0;
            if(exp_sign_delta > 0)
            {
                u32 exp_precise_len = (u32) platform_find_last_set_bit64(exp_sign_delta) + 1;
                exp_compressed_len = exp_size_to_compressed_len[exp_precise_len];
            }

            u32 mantissa_compressed_len = 0;
            if(mantissa_delta > 0)
            {
                //u32 mantissa_precise_len = 52 - (u32) platform_find_first_set_bit64(mantissa_delta);
                u32 mantissa_precise_len = (u32) platform_find_last_set_bit64(mantissa_delta) + 1;
                mantissa_compressed_len = mantissa_size_to_compressed_len[mantissa_precise_len];
            }
            
            ASSERT(0 <= exp_compressed_len && exp_compressed_len <= 7);
            ASSERT(0 <= mantissa_compressed_len && mantissa_compressed_len <= 7);

            value_compressed_len = MAX(exp_compressed_len, mantissa_compressed_len);
            value_decompressed_len = value_compressed_len + (value_compressed_len == 7);

            u32 mantissa_size = mantissa_compressed_len_to_size[value_compressed_len];
            u32 value_from = 52 - mantissa_size;
            u64 value_mask = ~(u64) 0 >> (64 - 8*value_decompressed_len);
            u64 value = exp_sign_delta << 52 | mantissa_delta;

            value_delta = (value >> value_from) & value_mask;
            *last_value = curr;
        }
    }
    else
    {
        UNREACHABLE();
    }

    u32 start_precise_len = (u32) platform_find_last_set_bit64(start_delta | start_delta << 1 | 1);
    u32 start_compressed_len = (start_precise_len + 7)/8;
    start_compressed_len -= start_compressed_len == 8;
    u32 start_decompressed_len = start_compressed_len + (start_compressed_len == 7);

    u32 index_compressed_len = 0;
    index_compressed_len += index_delta > 0;
    index_compressed_len += index_delta > 0xFF;
    index_compressed_len += index_delta > 0xFFFF;

    u32 index_decompressed_len = index_compressed_len;
    index_decompressed_len += index_decompressed_len == 3;

    isize len = 0;
    stream[len++] = (u8) (index_compressed_len << 6 | start_compressed_len << 3 | value_compressed_len);
    write_value(stream + len, index_delta, index_decompressed_len); len += index_decompressed_len;
    write_value(stream + len, start_delta, start_decompressed_len); len += start_decompressed_len;
    write_value(stream + len, value_delta, value_decompressed_len); len += value_decompressed_len;

    *last_index = sample.index;
    *last_time = sample.start;
    return len;
}

void test_compress_samples()
{
    
    u32 last_index = 0;
    u64 last_start = 0;
    u64 last_value = 0;
    u8* stream = malloc(2*MB);
    Sample samples[100] = {0};
    f64 mulval = 1.25464;
    for(isize i = 0; i < ARRAY_LEN(samples); i++)
    {   
        samples[i].type = SAMPLE_TYPE_F64;
        samples[i].index = 128;
        samples[i].start = __rdtsc();
        samples[i].val_f64 = mulval*i/8;
        //samples[i].val_f64 = sin((f64) i/8);
    }

    isize len = 0;
    for(isize i = 0; i < ARRAY_LEN(samples); i++)
        len += sample_compress(&last_index, &last_start, &last_value, samples[i], stream + len);
    
    len += sample_compress(&last_index, &last_start, &last_value, samples[1], stream + len);
    free(stream);
}


INTERNAL void test_all(f64 total_time)
{


    LOG_INFO("TEST", "RUNNING ALL TESTS");
    int total_count = 0;
    int passed_count = 0;

    #define INCR total_count += 1, passed_count += (int)
    
    INCR RUN_TEST(test_string_map);
    INCR RUN_TEST(platform_test_all);
    INCR RUN_TEST(test_list);
    INCR RUN_TEST(test_image);
    INCR RUN_TEST(test_lpf);
    INCR RUN_TEST(test_stable_array);
    INCR RUN_TEST(test_log);
    //INCR RUN_TEST(test_random);
    INCR RUN_TEST(test_path);

    INCR RUN_TEST_TIMED(test_sort, total_time/8);
    INCR RUN_TEST_TIMED(test_hash, total_time/8);
    INCR RUN_TEST_TIMED(test_arena, total_time/8);
    INCR RUN_TEST_TIMED(test_array, total_time/8);
    INCR RUN_TEST_TIMED(test_math, total_time/8);
    INCR RUN_TEST_TIMED(test_string, total_time/8);
    INCR RUN_TEST_TIMED(test_allocator_tlsf, total_time/8);
    INCR RUN_TEST_TIMED(slz4_test, total_time/8);
    
    #undef INCR

    if(passed_count == total_count)
        LOG_OKAY("TEST", "TESTING FINISHED! passed %i of %i test uwu", total_count, passed_count);
    else
        LOG_WARN("TEST", "TESTING FINISHED! passed %i of %i tests", total_count, passed_count);

    profile_log_all(log_info("TEST"), PERF_SORT_BY_NAME);
}

#if defined(TEST_RUNNER)

    #include "allocator_malloc.h"
    #include "log_file.h"
    int main()
    {
        platform_init();
        
        Arena_Stack* global_stack = scratch_arena_stack();
        arena_init(global_stack, 64*GB, 8*MB, "scratch_arena_stack");

        File_Logger logger = {0};
        file_logger_init_use(&logger, allocator_get_malloc(), "logs");

        test_all(30);

        //no deinit code!
        return 0;
    }

    #if PLATFORM_OS == PLATFORM_OS_UNIX
        #include "platform_linux.c"
    #elif PLATFORM_OS == PLATFORM_OS_WINDOWS
        #include "platform_windows.c"
    #else
        #error Unsupported OS! Add implementation
    #endif

#endif

#endif