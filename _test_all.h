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
    SAMPLE_TYPE_NONE = 0,
    SAMPLE_TYPE_TIME,
    SAMPLE_TYPE_I64,
    SAMPLE_TYPE_U64,
    SAMPLE_TYPE_F64,
    SAMPLE_TYPE_CUSTOM,
} Sample_Type;

typedef struct Sample_Stream_Info {
    const char* name;
    const char* file;
    const char* func;
    u32 line;
    u32 type;
    u32 index;
    u32 _;
} Sample_Stream_Info;

typedef struct Sample {
    union {
        Sample_Stream_Info* info;
        u32 index;
    };
    u64 start;
    union {
        u64 duration;
        i64 val_i64;
        u64 val_u64;
        //f64 val_f64;
    };
} Sample;

//Adapted from https://cbloomrants.blogspot.com/2014/03/03-14-14-fold-up-negatives.html
u64 fold_negatives64(i64 i)
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

u32 fold_negatives32(i32 i)
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

typedef struct Sample_Stream {
    u64 value;
    u32 next_index_1;
    u32 info_index_1;
    
    Sample_Stream_Info* info; //debug
} Sample_Stream;

typedef Array(Sample_Stream_Info) Sample_Stream_Info_Array; 
typedef Array(Sample) Sample_Array; 
typedef Array(Sample_Stream) Sample_Stream_Array;

typedef struct Sample_Compressor_State{
    Sample_Stream_Array streams;
    u64 last_time;
    u32 last_index;
    u32 _;
} Sample_Compressor_State;

#define MAX_BLOCK_SAMPLE_COUNT 4096

isize sample_block_compress(Sample_Compressor_State* state, const Sample* samples, isize sample_count, isize from, String_Builder* into)
{
    isize curr_from = from;
    for(isize i = 0; i < sample_count; i++)
    {
        Sample sample = samples[i];
        u32 sample_index = sample.info->index;
        if(sample_index >= state->streams.len)
            array_resize(&state->streams, sample_index + 1);

        ASSERT(state->last_index < state->streams.len);
        Sample_Stream* stream = &state->streams.data[sample_index];
        Sample_Stream* last_stream = &state->streams.data[state->last_index];

        u32 used_last_index = last_stream->next_index_1 ? last_stream->next_index_1 - 1 : state->last_index;
        u64 index_delta = fold_negatives32((i64) sample_index - (i64) used_last_index);
        u64 start_delta = fold_negatives64((i64) sample.start - (i64) state->last_time);

        u64 value_delta = 0;
        u32 value_compressed_len = 0;
        u32 value_decompressed_len = 0;

        const Sample_Stream_Info* info = sample.info;
        if(sample.info->type == SAMPLE_TYPE_TIME || sample.info->type == SAMPLE_TYPE_I64)
        {
            i64 signed_value_delta = sample.val_i64 - (i64) stream->value;
            value_delta = fold_negatives64(signed_value_delta);

            u32 value_precise_len = platform_find_last_set_bit64(value_delta << 1 | value_delta | 1);
            value_compressed_len = (value_precise_len + 7)/8;
            value_compressed_len -= value_compressed_len == 8;
            value_decompressed_len = value_compressed_len + (value_compressed_len == 7);

            stream->value = sample.val_u64;
        }
        else
        {
            TODO();
        }

        u32 start_precise_len = (u32) platform_find_last_set_bit64(start_delta | start_delta << 1 | 1);
        u32 start_compressed_len = (start_precise_len + 7)/8;
        start_compressed_len -= start_compressed_len == 8;
        u32 start_decompressed_len = start_compressed_len + (start_compressed_len == 7);
    
        bool added = false;
        index_delta = index_delta << 1;
        if(stream->info == 0)
        {
            added = true;
            stream->info = sample.info;
            index_delta |= 1;
        }

        u32 index_compressed_len = 0;
        index_compressed_len += index_delta > 0;
        index_compressed_len += index_delta > 0xFF;
        index_compressed_len += index_delta > 0xFFFF;

        u32 index_decompressed_len = index_compressed_len;
        index_decompressed_len += index_decompressed_len == 3;

        isize len = curr_from;
        ASSERT(index_decompressed_len < 8 && start_decompressed_len < 8 && value_decompressed_len < 8);
        builder_resize(into, len + 24);

        into->data[len++] = (u8) (index_compressed_len << 6 | start_compressed_len << 3 | value_compressed_len);
        memcpy(into->data + len, &index_delta, 8); len += index_decompressed_len;
        memcpy(into->data + len, &start_delta, 8); len += start_decompressed_len;
        memcpy(into->data + len, &value_delta, 8); len += value_decompressed_len;
        
        if(added)
        {
            u32 name_size = info->name ? (u32) strlen(info->name) : 0; 
            u32 file_size = info->file ? (u32) strlen(info->file) : 0; 
            u32 func_size = info->func ? (u32) strlen(info->func) : 0;
        
            name_size = MIN(name_size, 0xFFFF);
            file_size = MIN(file_size, 0xFFFF);
            func_size = MIN(func_size, 0xFFFF);

            u32 combined_size_header = name_size + file_size + func_size + 12 + 3;
            builder_resize(into, len + combined_size_header);

            memcpy(into->data + len, &name_size, 2); len += 2;
            memcpy(into->data + len, &file_size, 2); len += 2;
            memcpy(into->data + len, &func_size, 2); len += 2;
            memcpy(into->data + len, &info->type, 2); len += 2;
            memcpy(into->data + len, &info->line, 4); len += 4;
        
            memcpy(into->data + len, info->name, name_size); len += name_size;
            into->data[len++] = 0;
            memcpy(into->data + len, info->file, file_size); len += file_size;
            into->data[len++] = 0;
            memcpy(into->data + len, info->func, func_size); len += func_size;
            into->data[len++] = 0;

            builder_resize(into, len + combined_size_header);
        }

        last_stream->next_index_1 = sample_index + 1;
        state->last_index = sample_index;
        state->last_time = sample.start;

        curr_from = len;
    }
    
    return curr_from;
}

#define MASK(bits) (((uint64_t) ((bits) < 64) << ((bits) & 63)) - 1)


typedef struct Sample_Decompressor_State{
    Sample_Stream_Array streams;
    Sample_Stream_Info_Array infos;

    u64 last_time;
    u32 last_index;
    u32 _;
} Sample_Decompressor_State;

isize sample_decompress(Sample_Decompressor_State* state, Sample* samples, isize sample_count, isize* read_count, isize* error_at, const char* input, isize input_len, isize input_from)
{
    isize curr_from = input_from;
    for(isize i = 0; i < sample_count; i++)
    {
        Sample* sample = &samples[i];
        if(curr_from >= input_len)
            break;

        isize at = curr_from;
        u8 header = input[at++];

        u32 index_compressed_len = header >> 6;
        u32 start_compressed_len = header >> 3 & 0x7;
        u32 value_compressed_len = header >> 0 & 0x7;

        u32 index_decompressed_len = index_compressed_len + (index_compressed_len == 3);
        u32 start_decompressed_len = start_compressed_len + (start_compressed_len == 7);
        u32 value_decompressed_len = value_compressed_len + (value_compressed_len == 7);

        u64 index_delta = 0;
        u64 start_delta = 0;
        u64 value_delta = 0;
        
        if(at + index_decompressed_len + start_decompressed_len + value_decompressed_len > input_len)
        {
            LOG_ERROR("prof", "Past end when reading sample");
            *error_at = at;
            break;
        }

        memcpy(&index_delta, input + at, 8); at += index_decompressed_len;
        memcpy(&start_delta, input + at, 8); at += start_decompressed_len;
        memcpy(&value_delta, input + at, 8); at += value_decompressed_len;
    
        index_delta &= MASK(index_decompressed_len*8);
        start_delta &= MASK(start_decompressed_len*8);
        value_delta &= MASK(value_decompressed_len*8);
    
        u64 added = index_delta & 1;
        index_delta = index_delta >> 1;

        u32 used_last_index = state->last_index;
        if(used_last_index < state->streams.len && state->streams.data[state->last_index].next_index_1)
            used_last_index = state->streams.data[state->last_index].next_index_1 - 1;

        if(i == 10)
            LOG_HERE;

        u32 index = (u32) (unfold_negatives32((u32) index_delta) + (i32) used_last_index);
        u64 start = (u64) (unfold_negatives64(start_delta) + (i64) state->last_time);
        u64 value = 0;
    
        ASSERT(index < 0xFFFF);
        if(state->streams.len <= index)
            array_resize(&state->streams, index + 1);
        
        Sample_Stream* last_stream = &state->streams.data[state->last_index];
        Sample_Stream* stream = &state->streams.data[index];
        if(added)
        {
            u16 name_size = 0;
            u16 file_size = 0;
            u16 func_size = 0;
            u16 type = 0;
            u32 line = 0;

            if(at + 12 > input_len)
            {
                LOG_ERROR("prof", "Past end when reading sample info");
                *error_at = at;
                break;
            }

            memcpy(&name_size, input + at, 2); at += 2;
            memcpy(&file_size, input + at, 2); at += 2;
            memcpy(&func_size, input + at, 2); at += 2;
            memcpy(&type, input + at, 2); at += 2;
            memcpy(&line, input + at, 4); at += 4;
        
            if(at + name_size + file_size + func_size + 3 > input_len)
            {
                LOG_ERROR("prof", "Past end when reading sample info strings");
                *error_at = at;
                break;
            }

            Sample_Stream_Info info = {0};
            info.type = type;
            info.line = line;
            info.index = index;
            info.name = input + at; at += name_size + 1;
            info.file = input + at; at += file_size + 1;
            info.func = input + at; at += func_size + 1;

            array_push(&state->infos, info);
            stream->info_index_1 = (u32) state->infos.len;
        }
    
        u32 info_index = stream->info_index_1 - 1;
        ASSERT(info_index < state->infos.len);
        u32 type = state->infos.data[info_index].type;

        if(type == SAMPLE_TYPE_TIME || type == SAMPLE_TYPE_I64)
            value = (u64) (unfold_negatives64(value_delta) + (i64) stream->value);
        else
            LOG_ERROR("prof", "Unsupported type");

        sample->index = index;
        sample->start = start;
        sample->val_u64 = value;

        state->last_time = start;
        state->last_index = index;
        last_stream->next_index_1 = index + 1;
        stream->value = value;

        if(read_count)
            *read_count += 1;
        curr_from = at;
    }
    *error_at = -1;
    return curr_from;
}

isize sample_decompress_all(Sample_Decompressor_State* state, Sample_Array* samples, isize max_count_or_minus_one, isize* error_at, const char* input, isize input_len)
{
    isize curr_pos = 0;
    for(isize read = 0; read < max_count_or_minus_one || max_count_or_minus_one == -1; read++)
    {
        Sample sample = {0};
        curr_pos = sample_decompress(state, &sample, 1, NULL, error_at, input, input_len, curr_pos);
        if(*error_at != -1)
            break;

        array_push(samples, sample);
    }

    return curr_pos;
}

void test_compress_samples()
{
    Sample_Stream_Info infos[10] = {0};
    for(u32 i = 0; i < ARRAY_LEN(infos); i++)
    {
        infos[i].name = "name!";
        infos[i].file = __FILE__;
        infos[i].func = __func__;
        infos[i].line = __LINE__;
        infos[i].type = SAMPLE_TYPE_TIME;
        infos[i].index = i;
    }

    Sample samples[100] = {0};
    for(isize i = 0; i < ARRAY_LEN(samples); i++)
    {   
        samples[i].start = __rdtsc();
        samples[i].info = &infos[i % 10];
        samples[i].duration = __rdtsc() - samples[i].start;
    }

    String_Builder compressed = {0};
    Sample_Compressor_State comp = {0};
    sample_block_compress(&comp, samples, ARRAY_LEN(samples), 0, &compressed);

    Sample recontructed_samples[100] = {0};
    Sample_Decompressor_State decomp = {0};
    isize read = 0;
    isize error_at = 0;
    sample_decompress(&decomp, recontructed_samples, ARRAY_LEN(recontructed_samples), &read, &error_at, compressed.data, compressed.len, 0);

    LOG_HERE;
}

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

u64 read_value(const u8* from, u32 size)
{
    u64 output = 0;
    u8* source = (u8*) (void*) &output;
    switch(size)
    {
        case 8: *(source++) = *(from++);
        case 7: *(source++) = *(from++);
        case 6: *(source++) = *(from++);
        case 5: *(source++) = *(from++);
        case 4: *(source++) = *(from++);
        case 3: *(source++) = *(from++);
        case 2: *(source++) = *(from++);
        case 1: *(source++) = *(from++);
    }

    return output;
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

INTERNAL void test_get_len()
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

INTERNAL void test_all(f64 total_time)
{
    //test_compress_samples();

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