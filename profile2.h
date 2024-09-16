#pragma once

#include "platform.h"
#include "defines.h"


typedef enum Profile_Type {
	PROFILE_UNINIT = 0,
	PROFILE_COUNTER = 1,
	PROFILE_AVERAGE = 2,
	PROFILE_MIN_MAX = 4,
	PROFILE_SAMPLES = 8,

	PROFILE_DEFAULT = PROFILE_COUNTER | PROFILE_AVERAGE | PROFILE_MIN_MAX | PROFILE_SAMPLES,
} Profile_Type;

typedef struct Profile_ID {
	Profile_Type type;
	int line;
	const char* file;
	const char* function;
	const char* name;
	const char* comment; //primarily used for explaining non-timing samples
} Profile_ID;

typedef struct Profile_Zone {
	Profile_ID id;

	int64_t sum;
	int64_t min;
	int64_t max;
	int64_t count;

	union {
		uint64_t prev_u64;
		struct {
			uint64_t prev_exp_and_sign : 12;
			uint64_t prev_mantissa : 52;
		};
	};
} Profile_Zone;

typedef enum Profile_Sample_Type {
	PROFILE_SAMPLE_TIMING = 0,
	PROFILE_SAMPLE_U64    = 1,
	PROFILE_SAMPLE_F64    = 2,
	PROFILE_SAMPLE_CUSTOM = 3,
} Profile_Sample_Type;

typedef struct Profile_Sample {
	uint32_t zone_i;
	Profile_Sample_Type type;
	int64_t start;
	union {
		int64_t stop;
		int64_t i64_val;
		double  f64_val;
	};
} Profile_Sample;

#define PROFILE_BUFFER_CAPACITY 1024
typedef struct Profile_Buffer {
	Profile_Sample samples[PROFILE_BUFFER_CAPACITY];
	int64_t sample_count;
	int64_t thread_id;
	struct Profile_Buffer* next;
} Profile_Buffer;

typedef struct Profile_State {
	Profile_Zone* zones;
	int64_t zones_count;
	int64_t zones_capacity;

	Platform_Error file_error;
	Platform_File file;
	int32_t has_stuff_to_write;
	bool is_running;
	bool _[7];

	ATTRIBUTE_ALIGNED(64) Profile_Buffer* free_buffers;
	ATTRIBUTE_ALIGNED(64) Profile_Buffer* in_write_buffers;
} Profile_State;

static inline int64_t fenced_now()
{ 
	_ReadWriteBarrier(); 
    _mm_lfence();
	return (int64_t) __rdtsc();
}

static inline int64_t perf_now()
{
	_ReadWriteBarrier(); 
	return (int64_t) __rdtsc();
}

void zone_init(int* zone_i, const Profile_ID* id)
{
	
}

void zone_deinit(int* zone_i)
{

}

Profile_Zone* zone_get(int zone)
{

}

//Returns [0,7] where:
// 0 means 0 nonzero bytes, 
// 1 means 1 nonzero bytes, 
// ...
// 6 means 6 nonzero bytes, 
// 7 means 8 nonzero bytes!
uint32_t value_to_byte_size_in_3_bits(uint64_t value)
{
	if(value == 0)
		return 0;

	uint32_t found = (uint32_t) platform_find_last_set_bit64((value >> 8) | 1);
	uint32_t out = (found + 7)/8;
	return out;
}

//Adapted from https://cbloomrants.blogspot.com/2014/03/03-14-14-fold-up-negatives.html
u64 fold_up_negatives(i64 i)
{
    u64 two_i = ((u64)i) << 1;
    i64 sign_i = i >> 63;
    return two_i ^ sign_i;
}

i64 unfold_negatives(u64 i)
{
    u64 half_i = i >> 1;
    i64 sign_i = - (i64)( i & 1 );
    return half_i ^ sign_i;
}


void pack4(uint64_t* data, uint64_t nums[4], int32_t bit_counts[4])
{
	data[0] = nums[0];
	int32_t bit_i = bit_counts[0];
	for(int i = 1; i < 4; i++)
	{
		int32_t curr = bit_i / 64;
		int32_t remaining = 64 - bit_i % 64; 
		uint64_t rem_mask = ((1ULL << remaining) - 1);
		data[curr] |= nums[i] & rem_mask;

		int32_t remaining_from_current_iter = MAX(bit_counts[i] - remaining, 0);
		uint64_t rem_mask2 = ((1ULL << remaining_from_current_iter) - 1);
		data[curr + 1] |= nums[i] & rem_mask2;

		bit_i += bit_counts[i];
	}
}

void profile_writer(void* context)
{
	Profile_State* state = (Profile_State*) context;
	while(state->is_running)
	{
		platform_futex_wait(&state->has_stuff_to_write, 0, -1);
        //if quit was called in the meantime
		if (!state->is_running) 
			break; 

		//Get exclusive access to the write list
		Profile_Buffer* write_list = (Profile_Buffer*) (void*) platform_atomic_excahnge64((int64_t*) (void*) state->in_write_buffers, 0);

		for(Profile_Buffer* curr = write_list; curr != NULL; curr = curr->next)
		{
			i64 compressed_size = 0;
			enum {MAX_SIZE = PROFILE_BUFFER_CAPACITY*(sizeof(Profile_Sample) + 1) + 8};
			uint8_t compressed_buffer[MAX_SIZE] = {0};

			u32 prev_zone = 0;
			i64 prev_end = 0;
			for(i64 i = 0; i < curr->sample_count; i++)
			{
				Profile_Sample sample = curr->samples[i];
				
				u32 zone_delta = (u32) sample.zone_i ^ prev_zone;
				prev_zone = sample.zone_i;

				i64 start_delta = sample.start - prev_end;
				prev_end = sample.start;

				i32 zone_bits = zone_delta == 0 ? 0 : platform_find_last_set_bit32(zone_delta);
				i32 start_bits = start_delta == 0 ? 0 : platform_find_last_set_bit64(start_delta);

				if(sample.type == PROFILE_SAMPLE_TIMING)
				{
					u64 value_delta = (u64) (sample.stop - prev_end);
					prev_end = sample.stop;
					
					i32 value_bits = value_delta == 0 ? 0 : platform_find_last_set_bit64(value_delta);
				}
				else
				{
					Profile_Zone* zone = zone_get(sample.zone_i);
					if(sample.type == PROFILE_SAMPLE_U64)
					{
						u64 value = fold_up_negatives(sample.i64_val);
						u64 value_delta = value ^ zone->prev_u64;
						zone->prev_u64 = value;

						i32 value_bits = value_delta == 0 ? 0 : platform_find_last_set_bit64(value_delta);
					}
					else if(sample.type == PROFILE_SAMPLE_F64)
					{
						u64 reinterpret = *(u64*) (void*) &sample.f64_val;

						u64 exp = (reinterpret >> 52) & ((1ULL << 11) - 1);
						u64 mantissa = reinterpret & ((1ULL << 52) - 1);
						u64 exp_and_sign = (exp << 1) | (reinterpret >> 63);

						u64 exp_delta = exp_and_sign ^ zone->prev_exp_and_sign;
						u64 mantissa_delta = mantissa ^ zone->prev_mantissa;

						zone->prev_exp_and_sign = exp_and_sign;
						zone->prev_mantissa = mantissa;

						u32 exp_and_sign_bits = exp_delta == 0 ? 0 : platform_find_last_set_bit64(exp_delta);
						u32 mantissa_chunks = mantissa_delta == 0 ? 0 : ((u32) platform_find_first_set_bit64(mantissa_delta) + 2)/3;

						u64 out_type = PROFILE_SAMPLE_F64_0 + mantissa_chunks;
						u64 header = out_type 
							| zone_bits << 3 
							| start_bits << 8
							| exp_and_sign_bits << 14;

						u64 data[4] = {0};
						u64* curr = data;

						if(zone_bits + start_bits + exp_and_sign_bits + mantissa_chunks*3 <= 64 - 20)
						{
							
						}
						else
						{
						
						}
					}
				}
				
				u32 start_bytes = value_to_byte_size_in_3_bits((u64) start_delta);
				u32 value_bytes = value_to_byte_size_in_3_bits((u64) value_delta);


				memcpy(compressed_buffer + compressed_size, &start_bytes, start_bytes);
				memcpy(compressed_buffer + compressed_size, &value_bytes, value_bytes);
			}

			state->file_error = platform_file_write(&state->file, compressed_buffer, compressed_size);
		}

        void *buffer_ptr = (void *)atomic_load(&buffer->writer.ptr);
        if (buffer_ptr == 0) { continue; }

        size_t size = (size_t)atomic_load(&buffer->writer.size);
        atomic_store(&buffer->writer.ptr, 0);

        fwrite(buffer_ptr, size, 1, spall_ctx.file);
	}
}

Profile_Buffer* buffer_get(int zone)
{
	static thread_local Profile_Buffer* buffer = NULL;
	if(buffer == NULL)
		
}

static ATTRIBUTE_INLINE_ALWAYS void zone_submit(int zone_i, Profile_Type type, int64_t before, int64_t after)
{
	Profile_Zone* zone = zone_get(zone_i);
	int64_t delta = after - before;
	if(type & (PROFILE_COUNTER | PROFILE_AVERAGE))
		zone->count += 1;
	if(type & PROFILE_AVERAGE)
		zone->sum += delta;
	if(type & PROFILE_MIN_MAX)
	{
		zone->min = MIN(zone->min, delta);
		zone->max = MAX(zone->max, delta);
	}
	
	if(type & PROFILE_SAMPLES)
	{
		
	}
}

void test()
{
    static ATTRIBUTE_THREAD_LOCAL int zone_i = 0;
	static Profile_ID id = {PROFILE_DEFAULT, __LINE__, __FILE__, __func__, "name", "comment"};
	if(zone_i == 0)
		zone_init(&zone_i, &id);

	int64_t before = perf_now();

	int64_t after = perf_now();
	zone_submit(zone_i, PROFILE_DEFAULT, );
}