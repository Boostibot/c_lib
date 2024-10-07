#ifndef JOT_PROFILE
#define JOT_PROFILE

#include <stdbool.h>
#include <math.h>
#include <stdint.h>

#ifndef ASSERT
	#include <assert.h>
	#define ASSERT(x) assert(x)
#endif

#ifndef EXTERNAL
	#define EXTERNAL 
#endif

#include <stdint.h>
#include <stdbool.h>

#include "platform.h"
#include "defines.h"

typedef enum Profile_Sample_Type {
	PROFILE_SAMPLE_TIMING,
	PROFILE_SAMPLE_U64,
	PROFILE_SAMPLE_I64,
	PROFILE_SAMPLE_STR,//uncompressed null terminated string
	PROFILE_SAMPLE_BYTES,//uncompressed array of bytes
	PROFILE_SAMPLE_FRAME,
	PROFILE_SAMPLE_INSTANT,
	PROFILE_SAMPLE_THREAD_NAME,
	PROFILE_SAMPLE_PROCESS_NAME,
} Profile_Sample_Type;

typedef struct Profile_Zone_Info {
	Profile_Sample_Type type;
	int line;
	const char* file;
	const char* func;
	const char* time;
	const char* name;
	const char* desc;
} Profile_Zone_Info;

static ATTRIBUTE_INLINE_ALWAYS
void profile_zone_submit(uint32_t type, uint32_t zone, int64_t before, int64_t val);
static ATTRIBUTE_INLINE_NEVER
void profile_zone_init(volatile uint32_t* zone, const Profile_Zone_Info* id);
void profile_flush();
bool profile_init(const char* filename);
void profile_deinit();

isize profile_to_chrome_json_files(const char* output_filename, const char* input_filename, void (*error_log_or_null)(void* context, const char* fmt, ...), void* error_context);

#ifndef PP_UNIQ
	#define _PP_CONCAT(a, b) a ## b
	#define PP_CONCAT(a, b) _PP_CONCAT(a, b)
	#define PP_UNIQ(x) PP_CONCAT(x, __LINE__)
#endif

#define PROFILE_ZONE_DECLARE(TYPE, zone_id, zone_name, desc, ...) \
	static Profile_Zone_Info PP_UNIQ(info) = {TYPE, __LINE__, __FILE__, __func__, __DATE__ " " __TIME__, zone_name, desc}; \
	static volatile uint32_t zone_id = 0; \
	if(zone_id == 0) \
		profile_zone_init(&zone_id, &PP_UNIQ(info)) \

#define PROFILE_COUNTER(name, value, ...) do { \
		PROFILE_ZONE_DECLARE(PROFILE_SAMPLE_I64, PP_UNIQ(id), name, ##__VA_ARGS__, ""); \
		profile_zone_submit(PROFILE_SAMPLE_I64, PP_UNIQ(id), platform_rdtsc(), (value)); \
	} while(0) 

#define PROFILE_INSTANT(name, ...) do { \
		PROFILE_ZONE_DECLARE(PROFILE_SAMPLE_INSTANT, PP_UNIQ(id), name, ##__VA_ARGS__, ""); \
		profile_zone_submit(PROFILE_SAMPLE_INSTANT, PP_UNIQ(id), platform_rdtsc(), 0); \
	} while(0) 
	
#define PROFILE_START_NAMED(zone_id, ...)  \
	PROFILE_ZONE_DECLARE(PROFILE_SAMPLE_TIMING, __zone_##zone_id, ##__VA_ARGS__, "" #zone_id, ""); \
	int64_t __counter_start_##zone_id = platform_rdtsc() \

#define PROFILE_STOP_NAMED(zone_id, ...) \
	profile_zone_submit(PROFILE_SAMPLE_TIMING, __zone_##zone_id, __counter_start_##zone_id, platform_rdtsc() - __counter_start_##zone_id)
	
#define _PROFILE_START(zone_id, ...) PROFILE_START_NAMED(zone_id, ##__VA_ARGS__)
#define _PROFILE_STOP(zone_id, ...) PROFILE_STOP_NAMED(zone_id, ##__VA_ARGS__)

#define PROFILE_START(...) _PROFILE_START(__VA_ARGS__)
#define PROFILE_STOP(...) _PROFILE_STOP(__VA_ARGS__)

#define PROFILE_SCOPE(...) \
	_PROFILE_START(__VA_ARGS__); \
	for(int PP_UNIQ(i) = 1; PP_UNIQ(i); _PROFILE_STOP(__VA_ARGS__), PP_UNIQ(i) = 0) \

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PROFILE_IMPL)) && !defined(JOT_PROFILE_HAS_IMPL)
#define JOT_PROFILE_HAS_IMPL

typedef struct Profile_Zone {
	struct Profile_Zone* next;
	Profile_Zone_Info id;

	uint32_t index;
	uint32_t time;
} Profile_Zone;

typedef struct Profile_Sample {
	uint32_t type;
	uint32_t zone;
	int64_t start;
	union {
		int64_t duration;
		uint64_t value;
	};
} Profile_Sample;

#define PROFILE_BUFFER_CAPACITY 1024
typedef struct Profile_Buffer {
	struct Profile_Buffer* next;
	int32_t sample_count;
	int32_t thread_id;
	int32_t process_id;
	int32_t _;
	Profile_Sample samples[PROFILE_BUFFER_CAPACITY];
} Profile_Buffer;

enum {PROFILE_STOPPED, PROFILE_STARTING, PROFILE_STARTED, PROFILE_STOPPING};
typedef struct Profile_State {
	//State is split into two parts: "local" and "foreign"
	// - local is accessed only from the dedicated writer thread
	// - foreign is accessed by all threads and acts as a sort of inbox
	//		or a notice board. 
	// 
	// For example when a block is filled with samples it is posted into the 
	//  foreign_buffers and wake_calls is incremented. This wakes up the writer
	//  thread which takes all blocks from foreign_buffers and pushes them into local buffers.
	//  Then goes through local buffers in order and writes each to disk. 
	// Same mechanism is employed for zones except they dont wake the writing thread. It makes
	//  little sense writing out a zone which was not yet used.
	
	ATTRIBUTE_ALIGNED(64) 
	Profile_Buffer* local_buffers;
	Profile_Zone* local_zones;
	Profile_Zone* new_zones;
	Platform_Thread thread;
	FILE*	 output_file;
	uint64_t zone_count;
	uint64_t _1[2];
	
	ATTRIBUTE_ALIGNED(64) 
	Profile_Buffer* foreign_buffers;
	Profile_Zone* foreign_zones;
	uint32_t wake_calls;
	uint32_t state;
	int64_t submitted_count;
	int64_t processed_count;
	uint64_t _2[3];
} Profile_State;

ATTRIBUTE_INLINE_ALWAYS 
static void atomic_list_push_chain(volatile void** list, void* first, void* last)
{
    for(;;) {
        uint64_t curr = platform_atomic_load64(list);
        platform_atomic_store64(last, curr);
        if(platform_atomic_compare_and_swap_weak64(list, curr, (uint64_t) first))
            break;
    }
}

ATTRIBUTE_INLINE_ALWAYS 
static void atomic_list_push(volatile void** list, void* node)
{
    atomic_list_push_chain(list, node, node);
}

ATTRIBUTE_INLINE_ALWAYS 
static void* atomic_list_pop_all(volatile void** list)
{
    return (void*) platform_atomic_exchange64(list, 0);
}

Profile_State* profile_state()
{
	static Profile_State state = {0};
	return &state;
}

#include <time.h>
time_t parse_macro_time(char const* date, char const* time, bool* state_or_null);

uint16_t _profile_strlen16(const char* str)
{
	size_t out = str ? strlen(str) : 0;
	return out < INT16_MAX ? (uint16_t) out : INT16_MAX;
}

ATTRIBUTE_INLINE_NEVER 
void profile_zone_init(volatile uint32_t* zone, const Profile_Zone_Info* id)
{
	ASSERT(id && zone);
	
	uint32_t NOT_INIT = 0;
	uint32_t INITIALIZING = (uint32_t) -1;

	//Make sure that only one thread is initializing
    if(platform_atomic_compare_and_swap32(zone, NOT_INIT, INITIALIZING))
    {
		//We will allocate all the data relevant to the zone separately. 
		// This makes it possible to safely access zone.id when doing hot reloading
		uint16_t file_size = _profile_strlen16(id->file);
		uint16_t func_size = _profile_strlen16(id->func);
		uint16_t name_size = _profile_strlen16(id->name);
		uint16_t desc_size = _profile_strlen16(id->desc);
		
		bool okay = false;
		time_t time = parse_macro_time(id->time, id->time + sizeof(__DATE__), &okay);
		ASSERT(okay);

		isize alloced_size = sizeof(Profile_Zone) + file_size + func_size + name_size + desc_size + 4;
		char* alloced = (char*) calloc(1, alloced_size);

		Profile_Zone* alloced_zone = (Profile_Zone*) (void*) alloced; 
		
		Profile_State* state = profile_state();
		isize curr = sizeof(Profile_Zone);
		alloced_zone->next = NULL;
		alloced_zone->id = *id;
		alloced_zone->time = (uint32_t) time;
		alloced_zone->id.time = NULL;
		alloced_zone->id.file = alloced + curr; curr += file_size + 1;
		alloced_zone->id.func = alloced + curr; curr += func_size + 1;
		alloced_zone->id.name = alloced + curr; curr += name_size + 1;
		alloced_zone->id.desc = alloced + curr; curr += desc_size + 1;
		alloced_zone->index = (uint32_t) platform_atomic_add64(&state->zone_count, 1) + 1;

		memcpy((void*) alloced_zone->id.file, id->file, file_size);
		memcpy((void*) alloced_zone->id.func, id->func, func_size);
		memcpy((void*) alloced_zone->id.name, id->name, name_size);
		memcpy((void*) alloced_zone->id.desc, id->desc, desc_size);
		ASSERT(curr == alloced_size);

		atomic_list_push((void**) &state->foreign_zones, alloced_zone);

        platform_atomic_store32(zone, alloced_zone->index);
        platform_futex_wake_all((void*) zone);
    }
    else
    {
        while(true)
        {
            uint32_t curr_value = platform_atomic_load32(zone);
            if(curr_value != NOT_INIT && curr_value != INITIALIZING)
                break;
            
            platform_futex_wait(zone, INITIALIZING, -1);
        }
    }
}

static ATTRIBUTE_THREAD_LOCAL struct {
	Profile_Buffer* ptr;
	uint32_t count;
	uint32_t _;
} profile_buffer = {NULL, PROFILE_BUFFER_CAPACITY};

void profile_flush()
{
	Profile_State* state = profile_state();
	if(profile_buffer.ptr)
	{
		if(platform_atomic_load32(&state->state) != PROFILE_STARTED)
			profile_buffer.count = 0;
		else
		{
			ASSERT(profile_buffer.count <= PROFILE_BUFFER_CAPACITY);
			profile_buffer.ptr->sample_count = profile_buffer.count; 
			platform_memory_barrier();
			atomic_list_push((void**) &state->foreign_buffers, profile_buffer.ptr);
			platform_atomic_add32(&state->wake_calls, 1);
			platform_atomic_add64(&state->submitted_count, profile_buffer.count);
			platform_futex_wake(&state->wake_calls);

			profile_buffer.ptr = NULL;
			profile_buffer.count = PROFILE_BUFFER_CAPACITY;
		}
	}
}

static ATTRIBUTE_INLINE_NEVER void _profile_buffer_refill()
{
	ASSERT(profile_buffer.count >= PROFILE_BUFFER_CAPACITY);
	profile_flush();
	if(profile_buffer.ptr == NULL)
	{
		profile_buffer.ptr = (Profile_Buffer*) malloc(sizeof(Profile_Buffer));
		profile_buffer.count = 0;
		profile_buffer.ptr->next = NULL;
		profile_buffer.ptr->sample_count = 0;
		profile_buffer.ptr->process_id = 0;
		profile_buffer.ptr->thread_id = platform_thread_get_current_id();
	}
}


static ATTRIBUTE_INLINE_ALWAYS void profile_zone_submit(uint32_t type, uint32_t zone, int64_t before, int64_t val)
{
	if(profile_buffer.count >= PROFILE_BUFFER_CAPACITY)
		_profile_buffer_refill();

	Profile_Sample sample = {type, zone, before, val};
	ASSERT(profile_buffer.count < PROFILE_BUFFER_CAPACITY);
	profile_buffer.ptr->samples[profile_buffer.count++] = sample;
}

isize profile_format_buffer(Profile_Buffer* block, uint64_t* last_values, isize last_values_capacity, char** malloced_space, isize* capacity);

#define PROFILE_TIMEOUT_MS 1000
int profile_writer_func(void* context)
{
	isize format_buffer_capacity = 0;
	char* format_buffer = NULL; 
	isize last_values_capacity = 4096;
	uint64_t* last_values = (uint64_t*) calloc(last_values_capacity, sizeof(uint64_t));

	Profile_State* state = *(Profile_State**) context;
	for(;;) {
		PROFILE_START();
		if(state->local_buffers == NULL)
		{
			uint32_t run_state = platform_atomic_load32(&state->state);
			uint32_t wake_calls = platform_atomic_load32(&state->wake_calls);

			Profile_Buffer* buffers = (Profile_Buffer*) atomic_list_pop_all((void**) &state->foreign_buffers);
			Profile_Zone* zones = (Profile_Zone*) atomic_list_pop_all((void**) &state->foreign_zones);
			
			for(Profile_Buffer* curr = buffers; curr;)
			{
				Profile_Buffer* next = curr->next;
				curr->next = state->local_buffers;
				state->local_buffers = curr;
				curr = next;
			}
			for(Profile_Zone* curr = zones; curr;)
			{
				Profile_Zone* next = curr->next;
				curr->next = state->new_zones;
				state->new_zones = curr;
				curr = next;
			}

			if(state->local_buffers == NULL)
			{
				if(run_state != PROFILE_STARTED)
					break;

				while(wake_calls == platform_atomic_load32(&state->wake_calls))
					platform_futex_wait(&state->wake_calls, wake_calls, -1);
				continue;
			}
		}
			
		Profile_Buffer* popped = state->local_buffers;
		state->local_buffers = popped->next;
		
		isize formated_size = profile_format_buffer(popped, last_values, last_values_capacity, &format_buffer, &format_buffer_capacity);
		isize wrote = fwrite(format_buffer, 1, formated_size, state->output_file);
		ASSERT(formated_size == wrote);
		fflush(state->output_file);

		platform_atomic_add64(&state->processed_count, popped->sample_count);
		free(popped);
		
		PROFILE_STOP();
	}

	free(format_buffer);
	free(last_values);
	return 0;
}



bool profile_init(const char* filename)
{
	Profile_State* state = profile_state();
	bool had_error = false;
	for(;;) { 
		uint32_t curr_state = platform_atomic_load32(&state->state); 
		if(curr_state != PROFILE_STOPPING && curr_state !=  PROFILE_STARTING) 
			if(platform_atomic_compare_and_swap32(&state->state, curr_state, PROFILE_STARTING)) 
			{
				if(curr_state == PROFILE_STOPPED)
				{
					FILE* file = fopen(filename, "wb");
					if(file == NULL)
						had_error = true;
					else
					{
						state->output_file = file;
						platform_thread_launch(&state->thread, 0, profile_writer_func, &state, sizeof &state);
					}
				}

				platform_atomic_store32(&state->state, had_error ? PROFILE_STOPPED : PROFILE_STARTED);
				platform_futex_wake_all(&state->state);
				break;
			}
		platform_futex_wait(&state->state, curr_state, PROFILE_TIMEOUT_MS);
	}

	return had_error == false;
}

void _profile_free_chain(void* first)
{
	for(void* curr = first; curr; )
	{
		void* next = *(void**) curr;
		free(curr);
		curr = next;
	}
}

void profile_deinit()
{
	Profile_State* state = profile_state();
	for(;;) { 
		uint32_t curr_state = platform_atomic_load32(&state->state); 
		if(curr_state != PROFILE_STOPPING && curr_state !=  PROFILE_STARTING) 
			if(platform_atomic_compare_and_swap32(&state->state, curr_state, PROFILE_STOPPING)) 
			{
				if(curr_state == PROFILE_STARTED)
				{
					platform_atomic_add32(&state->wake_calls, 1);
					platform_futex_wake(&state->wake_calls);

					platform_thread_join(&state->thread, 1, -1);
					platform_thread_detach(&state->thread);
					Profile_Buffer* popped_blocks_f = (Profile_Buffer*) atomic_list_pop_all((void**) &state->foreign_buffers);
					Profile_Buffer* popped_blocks_l = (Profile_Buffer*) atomic_list_pop_all((void**) &state->local_buffers);
					Profile_Zone* popped_zones_f = (Profile_Zone*) atomic_list_pop_all((void**) &state->foreign_zones);
					Profile_Zone* popped_zones_l = (Profile_Zone*) atomic_list_pop_all((void**) &state->local_zones);

					_profile_free_chain(popped_blocks_f);
					_profile_free_chain(popped_blocks_l);
					_profile_free_chain(popped_zones_f);
					_profile_free_chain(popped_zones_l);

					fclose(state->output_file);
					memset(state, 0, sizeof state);
					platform_memory_barrier();
				}
		
				platform_atomic_store32(&state->state, PROFILE_STOPPED);
				platform_futex_wake_all(&state->state);
				break;
			}
		platform_futex_wait(&state->state, curr_state, PROFILE_TIMEOUT_MS);
	}
}

void _profile_buffer_reserve(char** buffer, isize* capacity, isize to_size)
{
	if(*capacity < to_size)
	{
		isize new_capacity = *capacity*2;
		if(new_capacity == 0)
			new_capacity = 4096;

		if(new_capacity < to_size)
			new_capacity = to_size;

		*buffer = (char*) realloc(*buffer, new_capacity);
		*capacity = new_capacity;
	}
}

#define PROFILE_FILE_MAGIC "JProfFi"
#define PROFILE_BLOCK_MAGIC "JProfBl"
#define PROFILE_ZONE_MAGIC "JZo"

typedef struct Profile_Block_Header {
	char magic[8];
	int64_t from_time;
	int64_t to_time;
	uint16_t new_zone_count;
	uint16_t sample_count;
	uint32_t samples_to;
	uint32_t block_size;
	uint32_t frequency;

	uint32_t thread_id;
	uint32_t process_id;
} Profile_Block_Header;
		
typedef struct Profile_Zone_Info_Header {
	char magic[4];
	uint32_t index;
	uint32_t line;
	uint32_t time;
	uint16_t zone_type;
	uint16_t file_size;
	uint16_t func_size;
	uint16_t name_size;
	uint16_t desc_size;
	uint16_t _;
} Profile_Zone_Info_Header;

#include <time.h>
#include <string.h>
time_t parse_macro_time(char const* date, char const* time, bool* state_or_null) 
{ 
    const char* NAMES = "JanFebMarAprMayJunJulAugSepOctNovDec";

    char s_month[32] = {0};
    int month = 0;
	int day = 0; 
	int year = 0;
	int hour = 0; 
    int minute = 0;
	int second = 0;

	bool state = true;
    if(date && sscanf(date, "%31s %d %d", s_month, &day, &year) != 3)
		state = false;

    if(time && sscanf(time, "%d:%d:%d", &hour, &minute, &second) != 3)
		state = false;

	ptrdiff_t match_index = strstr(NAMES, s_month)-NAMES;
	if(match_index != -1)
		month = (int) (match_index/3);
	
    struct tm t = {0};
    t.tm_mon = month;
    t.tm_mday = day;
    t.tm_year = year - 1900;
    t.tm_isdst = -1;

	if(state_or_null)
		*state_or_null = state;

    time_t out = mktime(&t);
	return out;
}

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

isize profile_compress_samples_max_size(isize sample_count)
{
	return sample_count*(sizeof(uint8_t) //header
		+ sizeof(uint32_t) //zone i
		+ sizeof(uint64_t) //start
		+ sizeof(uint64_t) //duration
	) + sizeof(uint64_t); //overwrite (so that we can copy 8 bytes and not use memcpy)
}

isize profile_compress_samples(uint32_t* last_zone, uint64_t* last_time, uint64_t* last_values, isize zone_capacity, const Profile_Sample* samples, isize sample_count, isize from, uint8_t* into, isize into_capacity)
{	
    PROFILE_START();
	isize pos = from;
    for(isize i = 0; i < sample_count; i++)
    {
        Profile_Sample sample = samples[i];

		if(pos + 21 > into_capacity)
		{
			printf("@TODO: Error: Not enough output buffer capacity! Requiring %lli got %lli\n", pos + 21, into_capacity);
			break;
		}

		if(0 < sample.zone && sample.zone < zone_capacity)
		{
			u64* last_value = &last_values[sample.zone - 1];
			u64 index_delta = fold_negatives64((i64) sample.zone - (i64) *last_zone);
			u64 start_delta = fold_negatives64((i64) sample.start - (i64) *last_time);
			u64 value_delta = fold_negatives64((i64) sample.value - (i64) *last_value);

			u32 value_precise_len = platform_find_last_set_bit64(value_delta << 1 | value_delta | 1);
			u32 value_compressed_len = (value_precise_len + 7)/8;
			value_compressed_len -= value_compressed_len == 8;
			u32 value_decompressed_len = value_compressed_len + (value_compressed_len == 7);

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

			ASSERT(index_decompressed_len <= 4 && start_decompressed_len <= 8 && value_decompressed_len <= 8);

			into[pos++] = (u8) (index_compressed_len << 6 | start_compressed_len << 3 | value_compressed_len);
			memcpy(into + pos, &index_delta, 4); pos += index_decompressed_len;
			memcpy(into + pos, &start_delta, 8); pos += start_decompressed_len;
			memcpy(into + pos, &value_delta, 8); pos += value_decompressed_len;
			
			*last_value = sample.value;
			*last_zone = sample.zone;
			*last_time = sample.start;
		}
		else
			printf("@TODO Error: Not enough capacity! Requiring %u got %lli\n", sample.zone, zone_capacity);
    }
    
	PROFILE_STOP();
    return pos;
}

isize profile_decompress_samples(uint32_t* last_zone, uint64_t* last_time, uint64_t* values, isize zone_capacity, Profile_Sample* samples, isize* sample_count, isize sample_capacity, isize input_from, const uint8_t* input, isize input_len)
{	
    PROFILE_START();
	isize pos = input_from;
    for(; pos < input_len && *sample_count < sample_capacity; )
    {
        u8 header = input[pos++];

        u32 index_compressed_len = header >> 6;
        u32 start_compressed_len = header >> 3 & 0x7;
        u32 value_compressed_len = header >> 0 & 0x7;

        u32 index_decompressed_len = index_compressed_len + (index_compressed_len == 3);
        u32 start_decompressed_len = start_compressed_len + (start_compressed_len == 7);
        u32 value_decompressed_len = value_compressed_len + (value_compressed_len == 7);

        u32 index_delta = 0;
        u64 start_delta = 0;
        u64 value_delta = 0;
        
		//Error!
        if(pos + index_decompressed_len + start_decompressed_len + value_decompressed_len > input_len)
            break;

        memcpy(&index_delta, input + pos, 4); pos += index_decompressed_len;
        memcpy(&start_delta, input + pos, 8); pos += start_decompressed_len;
        memcpy(&value_delta, input + pos, 8); pos += value_decompressed_len;
    
		#define MASK(bits) (((uint64_t) ((bits) < 64) << ((bits) & 63)) - 1)

        index_delta &= MASK(index_decompressed_len*8);
        start_delta &= MASK(start_decompressed_len*8);
        value_delta &= MASK(value_decompressed_len*8);
		
        u32 index = (u32) (unfold_negatives32((u32) index_delta) + (i32) *last_zone);
        u64 start = (u64) (unfold_negatives64(start_delta) + (i64) *last_time);
		
		if(index - 1 >= zone_capacity)
			break;

		u64* last_value = &values[index - 1];
		u64 value = (u64) (unfold_negatives64(value_delta) + (i64) *last_value);

		Profile_Sample sample = {0};
		sample.zone = index;
		sample.start = start;
		sample.value = value;

		samples[(*sample_count)++] = sample;
		*last_value = sample.value;
		*last_zone = sample.zone;
		*last_time = sample.start;
    }
	
	PROFILE_STOP();
    return pos;
}

isize _profile_find_first(const char* in_str, isize in_str_len, const char* search_for, isize search_for_len, isize from)
{
    ASSERT(from >= 0 && in_str_len >= 0 && search_for_len >= 0);
    if(from + search_for_len > in_str_len)
        return -1;
        
    if(search_for_len == 0)
        return from;

    const char* found = in_str + from;
    char last_char = search_for[search_for_len - 1];
    char first_char = search_for[0];

    while (true)
    {
        isize remaining_length = in_str_len - (found - in_str) - search_for_len + 1;
        ASSERT(remaining_length >= 0);

        found = (const char*) memchr(found, first_char, remaining_length);
        if(found == NULL)
            return -1;
                
        char last_char_of_found = found[search_for_len - 1];
        if (last_char_of_found == last_char)
            if (memcmp(found + 1, search_for + 1, search_for_len - 2) == 0)
                return found - in_str;

        found += 1;
    }

    return -1;
}

#ifdef _MSC_VER
    #define PACK_START __pragma(pack(push, 1))
    #define PACK_END __pragma(pack(pop))
#else
    #define PACK_START 
    #define PACK_END __attribute__((__packed__))
#endif

typedef struct Profile_Decode_Zone {
	uint32_t line;
	uint32_t time;
	uint16_t zone_type;
	uint16_t file_size;
	uint16_t func_size;
	uint16_t name_size;
	uint16_t desc_size;
	uint16_t _;
	uint32_t index;
	isize data_offset;
} Profile_Decode_Zone;

#include <stdarg.h>
#include <stdio.h>
void printf_error_log(void* context, const char* format, ...)
{
	(void) context;
	va_list args;
    va_start(args, format);
	vfprintf(stderr, format, args);
    va_end(args);
}

void _profile_fprintf_wrapper(void* context, const char* format, ...)
{
	va_list args;
    va_start(args, format);
	vfprintf((FILE*) context, format, args);
    va_end(args);
}

#define FORMATED_WRITE(error_log, error_context, ...)  (sizeof(printf(__VA_ARGS__)), ((error_log) ? (error_log) : printf_error_log)(error_context, __VA_ARGS__))

isize profile_decompress_block(
	Profile_Block_Header* out_header, int block_id,
	Profile_Sample* samples, isize* sample_count, isize sample_capacity, 
	Profile_Decode_Zone* zones, isize* zone_count, isize zone_capacity, 
	uint64_t* last_values, isize last_values_capacity,
	const uint8_t* buffer, isize* buffer_pos, isize buffer_size, 
	void (*error_log)(void* context, const char* fmt, ...), void* error_context)
{
	typedef long long lli;
	
    PROFILE_START();
	isize error_count = 0;
	isize pos = *buffer_pos;
	for(; pos < buffer_size;)
	{
		if(pos + (isize) sizeof(Profile_Block_Header) > buffer_size)
		{
			error_count += 1;
			FORMATED_WRITE(error_log, error_context,
				"Error: Buffer of size %lli not big enough for block header at %lli. Aborting. Block id:%i\n", 
				buffer_size, pos, block_id);
			pos = buffer_size;
			break;
		}

		isize block_offset = pos;
		Profile_Block_Header block_header = {0};
		memcpy(&block_header, buffer + pos, sizeof block_header);
		pos += sizeof block_header;

		if(memcmp(block_header.magic, PROFILE_BLOCK_MAGIC, sizeof PROFILE_BLOCK_MAGIC) != 0)
		{
			isize found = _profile_find_first((char*) (void*) buffer, buffer_size, PROFILE_BLOCK_MAGIC, sizeof PROFILE_BLOCK_MAGIC, block_offset);
			error_count += 1;
			FORMATED_WRITE(error_log, error_context, 
				"Error: Block magic number not matching at %lli. Skipping and attempting to recover. Block id:%i\n", 
				block_offset, block_id);
			
			pos = found == -1 ? buffer_size : found;
			continue;
		}
		
		isize block_end_offset = block_offset + block_header.block_size;
		if(block_end_offset > buffer_size)
		{
			error_count += 1;
			FORMATED_WRITE(error_log, error_context,
				"Error: Block has invalid size %u and extends past buffer size %lli. Capping at buffer size. Block id:%i thread_id:%u process_id:%u offset:%lli\n", 
				block_header.block_size, buffer_size, block_id, block_header.thread_id, block_header.process_id, block_offset);

			block_end_offset = buffer_size;
		}

		isize new_zones_from = block_offset + block_header.samples_to;
		if(new_zones_from > block_end_offset)
		{
			error_count += 1;
			FORMATED_WRITE(error_log, error_context, "Error: TODO");
			//TODO
			//FORMATED_WRITE(error_log, error_context,  
				//"Error: Block size %lli not enough to store array of samples of length %i. Shrinking sample array to %lli. Block id:%i thread_id:%u process_id:%u offset:%lli\n", 
				//block_end_offset - block_offset, (int) block_header.sample_count, new_sample_count, block_id, block_header.thread_id, block_header.process_id, block_offset);
			new_zones_from = block_end_offset;
		}
		
		isize new_sample_count = block_header.sample_count;
		if(*sample_count + new_sample_count >= sample_capacity)
		{
			error_count += 1;
			FORMATED_WRITE(error_log, error_context, 
				"Error: Sample array of length %lli not enough to store %lli new samples. Capping at sample capacity. Currently loaded %lli samples. Block id:%i thread_id:%u process_id:%u offset:%lli\n", 
				sample_capacity, new_sample_count, *sample_count, block_id, block_header.thread_id, block_header.process_id, block_offset);

			new_sample_count = sample_capacity - *sample_count;
		}

		isize new_zone_count = block_header.new_zone_count;
		if(*zone_count + new_zone_count >= zone_capacity)
		{
			error_count += 1;
			FORMATED_WRITE(error_log, error_context, 
				"Error: Zone array of length %lli not enough to store %lli new zones. Capping at zone capacity. Currently loaded %lli zones. Block id:%i thread_id:%u process_id:%u offset:%lli\n", 
				zone_capacity, new_zone_count, *zone_count, block_id, block_header.thread_id, block_header.process_id, block_offset);

			new_zone_count = zone_capacity - *zone_count;
		}

		//Load new zones
		isize zone_pos = new_zones_from;
		for(isize i = 0; i < block_header.new_zone_count; i++)
		{
			Profile_Zone_Info_Header zone_header = {0};
			if(block_offset > zone_pos || zone_pos + (isize) sizeof zone_header > block_end_offset)
			{
				error_count += 1;
				FORMATED_WRITE(error_log, error_context,
					"Error: Zone %lli extends past end of block of size %lli. Aborting new zone loading. Block id:%i thread_id:%u process_id:%u at %lli\n", 
					i, block_end_offset - block_offset, block_id, block_header.thread_id, block_header.process_id, block_offset);
				break;
			}

			memcpy(&zone_header, buffer + zone_pos, sizeof zone_header);
			if(memcmp(zone_header.magic, PROFILE_ZONE_MAGIC, sizeof PROFILE_ZONE_MAGIC) != 0)
			{
				error_count += 1;
				FORMATED_WRITE(error_log, error_context,  
					"Error: Zone %lli magic number not matching at offset %lli. Skipping and attempting to recover. Block id:%i thread_id:%u process_id:%u offset:%lli\n", 
					i, zone_pos, block_id, block_header.thread_id, block_header.process_id, block_offset);
				isize found = _profile_find_first((char*) (void*) buffer, block_end_offset, PROFILE_ZONE_MAGIC, sizeof PROFILE_ZONE_MAGIC, zone_pos);
				if(found == -1)
					found = block_end_offset;

				zone_pos = found;
			}
			else
			{
				uint32_t advance = sizeof(Profile_Zone_Info_Header) + zone_header.file_size + zone_header.func_size + zone_header.name_size + zone_header.desc_size + 4;
				if(zone_pos + advance > block_end_offset)
				{
					isize found = _profile_find_first((char*) (void*) buffer, block_end_offset, PROFILE_ZONE_MAGIC, sizeof PROFILE_ZONE_MAGIC, zone_pos + 1);
					error_count += 1;
					FORMATED_WRITE(error_log, error_context,
						"Error: Zone %lli is too big for block of size %lli. file_size:%i func_size:%i name_size:%i desc_size:%i. Skipping and recovering. Block id:%i thread_id:%u process_id:%u offset:%lli\n", 
						i, block_end_offset - block_offset, (int) zone_header.file_size, (int) zone_header.func_size, (int) zone_header.name_size, (int) zone_header.desc_size, block_id, block_header.thread_id, block_header.process_id, block_offset);
					if(found == -1)
						break;
				}
				else
				{
					Profile_Decode_Zone* zone = &zones[(*zone_count)++];
					zone->data_offset = zone_pos + (isize) sizeof(Profile_Zone_Info_Header);
					zone->line = zone_header.line;
					zone->time = zone_header.time;
					zone->zone_type = zone_header.zone_type;
					zone->file_size = zone_header.file_size;
					zone->func_size = zone_header.func_size;
					zone->name_size = zone_header.name_size;
					zone->desc_size = zone_header.desc_size;
					zone->index = zone_header.index;
					zone_pos += advance;
				}
			}
		}
		
		//Read all samples
		isize sample_pos = pos;

		uint32_t last_zone = 0;
		uint64_t last_time = 0;
		isize finished_at = profile_decompress_samples(&last_zone, &last_time, last_values, last_values_capacity, samples, sample_count, sample_capacity, sample_pos, buffer, new_zones_from);

		if(finished_at != new_zones_from)
		{
			error_count += 1;
			FORMATED_WRITE(error_log, error_context, "Error: TODO");
		}
		*out_header = block_header;
		pos = block_end_offset;
		break;
	}
	
	PROFILE_STOP();
	*buffer_pos = pos;
	return error_count;
}


isize profile_format_buffer(Profile_Buffer* block, uint64_t* last_values, isize last_values_capacity, char** malloced_space, isize* capacity)
{
    PROFILE_START();
	Profile_State* state = profile_state();
	char* space = *malloced_space;

	isize pos = sizeof(Profile_Block_Header);
	//int64_t startup_counter = platform_rdtsc_startup();

	isize max_needed_size = profile_compress_samples_max_size(block->sample_count);
	_profile_buffer_reserve(&space, capacity, pos + max_needed_size);
	memset(last_values, 0, last_values_capacity*sizeof *last_values);

	uint32_t last_zone = 0;
	uint64_t last_time = 0;
	isize samples_to = profile_compress_samples(&last_zone, &last_time, last_values, last_values_capacity, block->samples, block->sample_count, pos, (uint8_t*) (void*) space, pos + max_needed_size);
	pos = samples_to;

	uint16_t new_zones_count = 0;
	Profile_Zone* first_new_zone = state->new_zones;
	Profile_Zone* last_new_zone = state->new_zones;
	for(Profile_Zone* curr = state->new_zones; curr && new_zones_count < UINT16_MAX;)
	{
		Profile_Zone_Info id = curr->id;
		
		Profile_Zone_Info_Header zone_header = {0};
		memcpy(zone_header.magic, PROFILE_ZONE_MAGIC, sizeof PROFILE_ZONE_MAGIC);
		zone_header.index = curr->index;
		zone_header.line = id.line;
		zone_header.time = curr->time;
		zone_header.zone_type = id.type;
		zone_header.func_size = _profile_strlen16(id.func);
		zone_header.name_size = _profile_strlen16(id.name);
		zone_header.file_size = _profile_strlen16(id.file);
		zone_header.desc_size = _profile_strlen16(id.desc);

		isize combined_size = sizeof(Profile_Zone_Info_Header) + zone_header.file_size + zone_header.func_size + zone_header.name_size + zone_header.desc_size + 4;
		isize should_finish_at = pos + combined_size;
		_profile_buffer_reserve(&space, capacity, pos + combined_size);

		memcpy(space + pos, &zone_header, sizeof zone_header); pos += sizeof zone_header;
		memcpy(space + pos, id.func, zone_header.func_size); pos += zone_header.func_size;
		space[pos++] = '\0';
		memcpy(space + pos, id.name, zone_header.name_size); pos += zone_header.name_size;
		space[pos++] = '\0';
		memcpy(space + pos, id.file, zone_header.file_size); pos += zone_header.file_size;
		space[pos++] = '\0';
		memcpy(space + pos, id.desc, zone_header.desc_size); pos += zone_header.desc_size;
		space[pos++] = '\0';

		ASSERT(pos == should_finish_at);
		new_zones_count += 1;
		last_new_zone = curr;
		curr = curr->next;
	}

	if(first_new_zone)
	{
		last_new_zone->next = state->local_zones;
		state->local_zones = first_new_zone;
		state->new_zones = NULL;
	}
	
	_profile_buffer_reserve(&space, capacity, pos + sizeof(uint32_t));
	uint32_t cast_pos = (uint32_t) pos + sizeof cast_pos;
	memcpy(space + pos, &cast_pos, sizeof cast_pos); pos += sizeof cast_pos;

	Profile_Block_Header block_header = {0};
	memcpy(block_header.magic, PROFILE_BLOCK_MAGIC, sizeof PROFILE_BLOCK_MAGIC);
	block_header.block_size = (uint32_t) pos;
	ASSERT(block_header.block_size < UINT16_MAX);

	block_header.samples_to = (uint32_t) samples_to;
	block_header.frequency = (uint32_t) platform_rdtsc_frequency();
	block_header.new_zone_count = new_zones_count;
	block_header.sample_count = (uint16_t) block->sample_count;
	block_header.thread_id = block->thread_id;
	block_header.process_id = block->process_id;
	if(block->sample_count > 0)
	{
		block_header.from_time = block->samples[0].start;
		block_header.to_time = block->samples[block->sample_count - 1].start + block->samples[block->sample_count - 1].duration;
	}

	memcpy(space, &block_header, sizeof block_header);
	
	*malloced_space = space;
	PROFILE_STOP();
	return pos;
}

isize profile_to_chrome_json(const uint8_t* buffer, isize buffer_size, 
	void (*write_function)(void* context, const char* fmt, ...), void* write_context,
	void (*error_log_or_null)(void* context, const char* fmt, ...), void* error_context)
{
    PROFILE_START();
	isize sample_capacity = 4096;
	isize zone_capacity = 4096;

	isize zone_count = 0;

	Profile_Sample* samples = (Profile_Sample*) calloc(sizeof(Profile_Sample), sample_capacity);
	Profile_Decode_Zone* zones = (Profile_Decode_Zone*) calloc(sizeof(Profile_Decode_Zone), zone_capacity);
	uint64_t* last_values = (uint64_t*) calloc(sizeof(uint64_t), zone_capacity);
	
	FORMATED_WRITE(write_function, write_context,
		"{"
		"\n  \"displayTimeUnit\": \"ns\","
		"\n  \"samples\": [],"
		"\n  \"traceEvents\": ["
	);
	
	char name_buffer[512] = {0};
	char desc_buffer[512] = {0};

	isize error_count = 0;
	isize buffer_pos = 0;
	for(int block_id = 0; ;block_id++)
	{
		PROFILE_START(block);

		PROFILE_START(parsing);
			Profile_Block_Header block_header = {0};
			isize sample_count = 0;
			memset(last_values, 0, sizeof(uint64_t)*zone_capacity);
			isize current_errors = profile_decompress_block(&block_header, block_id,
				samples, &sample_count, sample_capacity, 
				zones, &zone_count, zone_capacity,
				last_values, zone_capacity,
				buffer, &buffer_pos, buffer_size,
				error_log_or_null, error_context
			);
		PROFILE_STOP(parsing);
		
		PROFILE_START(writing);
		double sample_rate_to_micro = block_header.frequency ? 1e6/block_header.frequency : 1;
		error_count += current_errors;
		for(isize i = 0; i < sample_count; i++)
		{
			//TODO: smarter lookup!
			Profile_Decode_Zone* zone = NULL;
			Profile_Sample sample = samples[i];
			for(isize k = 0; k < zone_count; k++)
			{
				if(zones[k].index == sample.zone)
					zone = &zones[k];
			}
			
			if(zone == NULL)
			{
				FORMATED_WRITE(error_log_or_null, error_context,
					"Error: Sample %lli references zone %u which was not yet loaded. Skipping. Block id:%i thread_id:%u process_id:%u\n", 
					i, sample.zone, block_id, block_header.thread_id, block_header.process_id);
			}
			else
			{
				double microsec_start = (double)sample.start*sample_rate_to_micro;

				const char* func = (char*) (void*) buffer + zone->data_offset;
				const char* name = func + zone->func_size + 1;
				const char* file = name + zone->name_size + 1;
				const char* desc = file + zone->file_size + 1;
				const char* comma = i > 0 || block_id > 0 ? "," : "";

				const char* name_formatted = func;
				if(zone->name_size)
				{
					snprintf(name_buffer, sizeof name_buffer, "%s:%s", func, name);
					name_formatted = name_buffer;
				}
			
				if(zone->zone_type == PROFILE_SAMPLE_TIMING)
				{
					double microsec_duration = (double)sample.duration*sample_rate_to_micro;
					const char* desc_formatted = "";
					if(zone->desc_size)
					{
						snprintf(desc_buffer, sizeof desc_buffer, ",\"args\":{\"desc\":\"%s\"}", desc);
						desc_formatted = desc_buffer;
					}
				
					FORMATED_WRITE(write_function, write_context, "%s\n{\"ph\":\"X\",\"pid\":%u,\"tid\":%u,\"ts\":%.2lf,\"name\":\"%s\",\"dur\":%.2lf%s}",
						comma, block_header.process_id, block_header.thread_id, microsec_start, name_formatted, microsec_duration, desc_formatted
					);
				}
				else if(zone->zone_type == PROFILE_SAMPLE_I64 || zone->zone_type == PROFILE_SAMPLE_U64)
				{
					const char* desc_formatted = "";
					if(zone->desc_size)
					{
						snprintf(desc_buffer, sizeof desc_buffer, ",\"desc\":\"%s\"", desc);
						desc_formatted = desc_buffer;
					}

					FORMATED_WRITE(write_function, write_context, "%s\n{\"ph\":\"C\",\"pid\":%u,\"tid\":%u,\"ts\":%.2lf,\"name\":\"%s\",\"args\":{\"0\":%lli%s}}",
						comma, block_header.process_id, block_header.thread_id, microsec_start, name_formatted, sample.value, desc_formatted
					);
				}
				else if(zone->zone_type == PROFILE_SAMPLE_INSTANT)
				{
					const char* desc_formatted = "";
					if(zone->desc_size)
					{
						snprintf(desc_buffer, sizeof desc_buffer, ",\"desc\":\"%s\"", desc);
						desc_formatted = desc_buffer;
					}

					FORMATED_WRITE(write_function, write_context, "%s\n{\"ph\":\"i\",\"pid\":%u,\"tid\":%u,\"ts\":%.2lf,\"name\":\"%s\",\"args\":{\"g\":\"t\",\"complete_name\":\"%s\"%s}}",
						comma, block_header.process_id, block_header.thread_id, microsec_start, name, name_formatted, desc_formatted 
					);
				}
			}
		}
		PROFILE_STOP(writing);
		
		PROFILE_STOP(block);
		if(buffer_pos >= buffer_size)
			break;
	}
	
	FORMATED_WRITE(write_function, write_context,
		"\n  ]"
		"\n}"
	);

	free(samples);
	free(zones);
	free(last_values);
	PROFILE_STOP();
	return error_count;
}

isize profile_to_chrome_json_files(const char* output_filename, const char* input_filename, void (*error_log_or_null)(void* context, const char* fmt, ...), void* error_context)
{
    PROFILE_START();
	FILE* in_file = fopen(input_filename, "rb");
	FILE* out_file = fopen(output_filename, "wb");

	isize error_count = 0;

	if(in_file && out_file)
	{
		isize chunk = 4*4096;
		isize buffer_capacity = 0;
		isize buffer_size = 0;
		uint8_t* buffer = NULL;

		PROFILE_SCOPE(reading)
		{
			for(;;) {
				if(buffer_size + chunk > buffer_capacity)
				{
					buffer_capacity = buffer_capacity*2 + chunk;
					buffer = (uint8_t*) realloc(buffer, buffer_capacity);
				}
			
				isize read = fread(buffer + buffer_size, 1, chunk, in_file);
				buffer_size += read;
				if(read < chunk)
					break;
			}
		}

		if(ferror(in_file))
		{
			error_count += 1;
			FORMATED_WRITE(error_log_or_null, error_context, "Error: Error reading input file '%s'. Continuing with partial file.", input_filename);
		}
		
		error_count += profile_to_chrome_json(buffer, buffer_size, _profile_fprintf_wrapper, out_file, error_log_or_null, error_context);
		if(ferror(in_file))
		{
			error_count += 1;
			FORMATED_WRITE(error_log_or_null, error_context, "Error: Error writing to output file '%s'. Continuing with partial file.", output_filename);
		}

		free(buffer);
	}

	if(in_file == NULL)
	{
		error_count += 1;
		FORMATED_WRITE(error_log_or_null, error_context, "Error: Cannot open input file '%s'. Aborting.", input_filename);
	}
	else
		fclose(in_file);
		
	if(out_file == NULL)
	{
		error_count += 1;
		FORMATED_WRITE(error_log_or_null, error_context, "Error: Cannot open output file '%s'. Aborting.", output_filename);
	}
	else
		fclose(out_file);
		
    PROFILE_STOP();
	return error_count;
}


void profile_test()
{
	profile_deinit();
	bool check = profile_init("test.jprof");
	ASSERT(check);
	{
		PROFILE_START(compression);
		Profile_Sample samples[100] = {0};
		Profile_Sample decompressed_samples[100] = {0};
	
		PROFILE_START(sample_generation);
		for(isize i = 0; i < ARRAY_LEN(samples); i++)
		{
			samples[i].zone = i % 10 + 1;
			samples[i].type = PROFILE_SAMPLE_TIMING;
			samples[i].start = platform_rdtsc();
			if(i > 0)
				samples[i].duration = platform_rdtsc() - samples[i - 1].start;
			else
				samples[i].duration = 200;
		}
		PROFILE_STOP(sample_generation);

		isize zone_capacity = 4096;
		isize buffer_capacity = UINT16_MAX;

		uint64_t* zones = (uint64_t*) calloc(sizeof(uint64_t), zone_capacity);
		uint8_t* buffer = (uint8_t*) calloc(sizeof(uint8_t), buffer_capacity);
		
		memset(zones, 0, sizeof(uint64_t)*zone_capacity);
		u32 compress_last_zone = 0;
		u64 compress_last_time = 0;
		
		PROFILE_START(compress);
		isize compress_ended_at = profile_compress_samples(&compress_last_zone, &compress_last_time, zones, zone_capacity, samples, ARRAY_LEN(samples), 0, buffer, buffer_capacity);
		PROFILE_STOP(compress);
		
		memset(zones, 0, sizeof(uint64_t)*zone_capacity);
		u32 decompress_last_zone = 0;
		u64 decompress_last_time = 0;

		PROFILE_START(decompress);
		isize sample_count = 0;
		isize decompress_ended_at = profile_decompress_samples(&decompress_last_zone, &decompress_last_time, zones, zone_capacity, decompressed_samples, &sample_count, ARRAY_LEN(decompressed_samples), 0, buffer, compress_ended_at);
		PROFILE_STOP(decompress);
		
		ASSERT(compress_ended_at == decompress_ended_at);
		for(isize i = 0; i < ARRAY_LEN(samples); i++)
		{
			Profile_Sample original = samples[i];
			Profile_Sample restored = decompressed_samples[i];

			ASSERT(original.zone == restored.zone);
			ASSERT(original.start == restored.start);
			ASSERT(original.duration == restored.duration);
		}

		PROFILE_STOP(compression);
	}

	if(0)
	for(int i = 0; i < 10; i++)
	{
		static Profile_Zone_Info info = {PROFILE_SAMPLE_TIMING, __LINE__, __FILE__, __func__, __DATE__ " " __TIME__, "first"};
		static uint32_t zone_id = 0;
		if(platform_atomic_load32(&zone_id) == 0)
			profile_zone_init(&zone_id, &info);

		int64_t before = platform_rdtsc();
		platform_thread_sleep(2);
		int64_t after = platform_rdtsc();
		profile_zone_submit(PROFILE_SAMPLE_TIMING, zone_id, before, after - before);
	}
	
	#if 1
	PROFILE_INSTANT("flush of block", "desc1");
	
	PROFILE_START();
	for(int i = 0; i < 10; i++)
	{
		PROFILE_START(third, "name", "desc2");
		platform_thread_sleep(2);
		PROFILE_STOP(third);

	}

	for(int i = 0; i < 10; i++)
		PROFILE_COUNTER("hello", i, "desc3");

	PROFILE_STOP();
	#endif
	
	profile_flush();
	profile_to_chrome_json_files("test.json", "test.jprof", NULL, NULL);

	printf("done!");
	profile_deinit();
}

#endif

