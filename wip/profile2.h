#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

typedef struct Profile_Zone {
    const char* name;
    const char* info;
    const char* file;
    const char* func;
    uint32_t line;
    uint32_t id;
    uint32_t type;
    struct Profile_Zone* next;
    struct Profile_Zone* prev;
} Profile_Zone;

#define PROFILE_ZONE_TYPE_TIMER		1
#define PROFILE_ZONE_TYPE_INSTANT	2
#define PROFILE_ZONE_TYPE_I64		3
#define PROFILE_ZONE_TYPE_I32		4
#define PROFILE_ZONE_TYPE_F64		5
#define PROFILE_ZONE_TYPE_F32		6
#define PROFILE_ZONE_TYPE_VEC3		7

void profile_init_custom(const char* filename, isize filename_size, double flush_every, isize block_capacity, bool enabled);
void profile_init(const char* filename);
void profile_enable(bool to);
void profile_flush_thread(bool wait);
void profile_flush(bool wait);
void profile_deinit();

void profile_instant(Profile_Zone* zone);
void profile_start(Profile_Zone* zone);
void profile_stop(Profile_Zone* zone);
void profile_i64(Profile_Zone* zone, int64_t val);
void profile_f64(Profile_Zone* zone, double val);
void profile_f32(Profile_Zone* zone, float val);
void profile_vec3(Profile_Zone* zone, float x, float y, float z);
void profile_string(Profile_Zone* zone, const char* str, int64_t size);
void profile_cstring(Profile_Zone* zone, const char* str);
void profile_vfstring(Profile_Zone* zone, const char* fmt, va_list args);
void profile_fstring(Profile_Zone* zone, const char* fmt, ...);

typedef int64_t isize;
isize profile_to_chrome_json_file_cstr(const char* output_filename, const char* input_filename, void (*error_log_or_null)(void* context, const char* fmt, ...), void* error_context);
isize profile_to_chrome_json_file(const char* to, isize to_size, const char* from, isize from_size, void (*error_log_or_null)(void* context, const char* fmt, ...), void* error_context);

void test()
{
    static Profile_Zone zone = {"name", "info", __FILE__, __func__, __LINE__};
    profile_start(&zone);
	
    profile_end(&zone);
}

//Inside implementation
#include <stdatomic.h>
#include <stdalign.h>
#include <stdio.h>
#include <assert.h>


#define THREAD_LOCAL
#define ATOMIC(T) T
#define ATTRIBUTE_INLINE_NEVER
#define ATTRIBUTE_INLINE_ALWAYS
#define ASSERT(x) assert(x)

typedef struct Profile_Buffer Profile_Buffer;

typedef struct Profile_Buffer_Side {
	ATOMIC(uint8_t*) tail; //written_to
	ATOMIC(uint8_t*) head; //flushed_to

	uint8_t* begin;
	uint8_t* end;
	
    int64_t start_qpc;
    int64_t start_tsc;
    int64_t end_qpc;
    int64_t end_tsc;
} Profile_Buffer_Side;

typedef struct Profile_Buffer {
	ATOMIC(Profile_Buffer)* next;
	ATOMIC(Profile_Buffer)* prev;

	Profile_Buffer_Side sides[2];
	ATOMIC(uint32_t) abandoned;
	ATOMIC(uint32_t) active_side; 

	int32_t capacity;
	int32_t thread_id;
	int32_t process_id;
} Profile_Buffer;

typedef struct Profile_Thread_State Profile_Thread_State;


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
	
	alignas(64) 
	Profile_Buffer* local_buffers_first;
	Profile_Buffer* local_buffers_last;
	Profile_Zone* local_zones;
	Profile_Zone* new_zones;
	FILE*	 output_file;
	uint64_t zone_count;
	uint64_t _1[1];
	
	alignas(64) 
	ATOMIC(Profile_Buffer*) foreign_buffers;
	ATOMIC(Profile_Zone*) foreign_zones;
	ATOMIC(uint64_t) flushes_requested;
	ATOMIC(uint64_t) flushes_completed;
	ATOMIC(double)   flush_every;
	ATOMIC(uint32_t) state;

    ATOMIC(bool) enabled;
    ATOMIC(isize) default_block_size;
} Profile_State;


static Profile_State g_profile_state = {0};
static Profile_Buffer g_nil_profile_buffer = {NULL, NULL, INT32_MAX};
#define NIL_PROFILE_BUFFER (&g_nil_profile_buffer)

static THREAD_LOCAL Profile_Buffer* t_profile_buffer = &g_nil_profile_buffer;
static THREAD_LOCAL Profile_Buffer_Side* t_profile_buffer_side = &g_nil_profile_buffer.sides[0];

isize tsc_now();
isize qpc_now();
uint32_t current_thread_id();
uint32_t current_process_id();

#define _profile_atomic_list_push(head_ptr_ptr, node_ptr) _profile_atomic_list_push_chain(head_ptr_ptr, node_ptr, node_ptr)
#define _profile_atomic_list_pop_all(head_ptr_ptr) atomic_exchange((head_ptr_ptr), NULL)
#define _profile_atomic_list_push_chain(head_ptr_ptr, first_node_ptr, last_node_ptr)                   \
    for(;;) {                                                                               \
        ATOMIC(void*)* __head = (void*) (head_ptr_ptr);                                \
        ATOMIC(void*)* __last_next = (void*) &(last_node_ptr)->next;                   \
                                                                                            \
        void* __curr = atomic_load(__head);                                                 \
        atomic_store(__last_next, __curr);                                                  \
        if(atomic_compare_exchange_weak(__head, &__curr, (void*) (first_node_ptr)))         \
            break;                                                                          \
    }    

ATTRIBUTE_INLINE_NEVER
static uint8_t* _profile_buffer_refill();

ATTRIBUTE_INLINE_ALWAYS
static void profile_submit_generic_inline(Profile_Zone* zone, uint64_t tag, void* data, isize size)
{
	if(atomic_load_explicit(&g_profile_state.enabled, memory_order_relaxed))
	{
		isize now = tsc_now();
		isize needed_size = sizeof(Profile_Zone*) + sizeof(int64_t) + size;
		
		//if out of mem (or uninitialized), refill
		uint8_t* tail = atomic_load_explicit(&t_profile_buffer_side->tail, memory_order_relaxed);
		if(tail + needed_size > t_profile_buffer_side->end) 
			tail = _profile_buffer_refill(needed_size);

		//store everything
		uint64_t tagged_zone = (uint64_t) zone | tag;
		memcpy(tail, &tagged_zone, sizeof tagged_zone); tail += sizeof tagged_zone;
		memcpy(tail, &now, sizeof now); tail += sizeof now;
		memcpy(tail, data, size); tail += size;

		//publish changes
		atomic_store_explicit(&t_profile_buffer_side->tail, tail, memory_order_release);
	}
}

ATTRIBUTE_INLINE_ALWAYS
static void profile_submit_string_generic(Profile_Zone* zone, const char* ptr, isize size, isize custom_tsc, bool calc_size, bool use_tsc);

#if 0
{
	if(atomic_load_explicit(&g_profile_state.enabled, memory_order_relaxed))
	{
		isize now = use_tsc ? custom_tsc : tsc_now();
		isize len = size;
		if(calc_size)
			len = ptr ? strlen(ptr) : 0; 

		const isize needed_size = sizeof(Profile_Zone*) + sizeof(int64_t) + len;
		isize written = atomic_load_explicit(t_profile_buffer->written_count, memory_order_relaxed);
		if(written + needed_size >= t_profile_buffer->capacity) {
			_profile_buffer_refill(needed_size);
			written = 0;
		}

		uint8_t* buffer = (uint8_t*) (t_profile_buffer + 1) + written;
		memcpy(buffer, &zone, sizeof zone); buffer += sizeof zone;
		memcpy(buffer, &now, sizeof now); buffer += sizeof now;
		memcpy(buffer, &len, sizeof len); buffer += sizeof len;
		memcpy(buffer, ptr, len); buffer += len;

		atomic_store_explicit(&t_profile_buffer->written_count, written + needed_size, memory_order_relaxed);
	}
}
#endif

void profile_instant(Profile_Zone* zone) 			{ profile_submit_generic_inline(zone, 0, NULL, 0); }
void profile_start(Profile_Zone* zone)				{ profile_submit_generic_inline(zone, 0, NULL, 0); }
void profile_stop(Profile_Zone* zone) 				{ profile_submit_generic_inline(zone, 1, NULL, 0); }
void profile_i32(Profile_Zone* zone, int32_t val) 	{ profile_submit_generic_inline(zone, 0, &val, sizeof val); }
void profile_i64(Profile_Zone* zone, int64_t val) 	{ profile_submit_generic_inline(zone, 0, &val, sizeof val); }
void profile_f64(Profile_Zone* zone, double val) 	{ profile_submit_generic_inline(zone, 0, &val, sizeof val); }
void profile_f32(Profile_Zone* zone, float val)		{ profile_submit_generic_inline(zone, 0, &val, sizeof val); }
void profile_vec3(Profile_Zone* zone, float x, float y, float z) 
{ 
	float xyz[3] = {x, y, z};
	profile_submit_generic_inline(zone, 0, xyz, sizeof xyz); 
}

void profile_string(Profile_Zone* zone, const char* str, isize size)
{
	profile_submit_string_generic(zone, str, size, 0, false, false);
}

void profile_cstring(Profile_Zone* zone, const char* str)
{
	profile_submit_string_generic(zone, str, 0, 0, true, false);
}

#include <stdarg.h>
void profile_vfstring(Profile_Zone* zone, const char* fmt, va_list args)
{
	if(atomic_load_explicit(&g_profile_state.enabled, memory_order_relaxed))
	{
		isize now = tsc_now();
		char buffer[2048];
		isize size = vsnprintf(buffer, sizeof buffer, fmt, args);
		profile_submit_string_generic(zone, buffer, size, now, false, true);
	}
}
void profile_fstring(Profile_Zone* zone, const char* fmt, ...)
{
	if(atomic_load_explicit(&g_profile_state.enabled, memory_order_relaxed))
	{
		va_list args;
		va_start(args, fmt);
		profile_submit_vfstring(zone, fmt, args);
		va_end(args);
	}
}


static void _profile_thread_register_auto_deinit();

bool profile_thread_init(void* memory_or_null, isize size_or_negative_one)
{
	//prevent double init
	if(t_profile_buffer != NIL_PROFILE_BUFFER)
		return false;

	isize size = size_or_negative_one;
	if(size == size_or_negative_one)
		size = g_profile_state.default_block_size;

	//too small, get a bigger buffer.
	if(size < sizeof(Profile_Buffer) + 256)
		return false;

	void* memory = memory_or_null ? memory_or_null : malloc(size);
	memset(memory, 0, size);

	Profile_Buffer* new_buffer = (Profile_Buffer*) memory;
	new_buffer->thread_id = current_thread_id();
	new_buffer->process_id = current_process_id();
	new_buffer->capacity = size - sizeof(Profile_Buffer);

	//init sides
	uint8_t* side_from = (uint8_t*) (void*) (new_buffer + 1);
	for(int i = 0; i < 2; i++)
	{
		new_buffer->sides[i].head = side_from;
		new_buffer->sides[i].tail = side_from;
		new_buffer->sides[i].begin = side_from;
		side_from += new_buffer->capacity / 2;
		new_buffer->sides[i].end = side_from;
	}

	//register self TODO
	_profile_atomic_list_push(&g_profile_state.foreign_buffers, new_buffer);

	//assign self
	t_profile_buffer = new_buffer;
	t_profile_buffer_side = &new_buffer->sides[0];

	_profile_thread_register_auto_deinit();
	return true;
}

bool profile_thread_deinit()
{
	//todo: do deinit

	t_profile_buffer = NIL_PROFILE_BUFFER;
	t_profile_buffer_side = &NIL_PROFILE_BUFFER->sides[0];
}

void profile_thread_init_auto()
{
	profile_thread_init(NULL, -1);
}

bool futex_wait(void* futex, uint32_t bad_value, double timeout_after_or_negative);
void futex_wake(void* futex);
void futex_wake_all(void* futex);

ATTRIBUTE_INLINE_NEVER
static uint8_t* _profile_buffer_refill()
{
	ASSERT(t_profile_buffer);
	if(t_profile_buffer == NIL_PROFILE_BUFFER)
		profile_thread_init_auto();
	else
	{
		isize tsc = tsc_now();
		isize qpc = qpc_now();
		
		Profile_Buffer_Side* curr_side = &t_profile_buffer->sides[t_profile_buffer->active_side % 2];
		Profile_Buffer_Side* next_side = &t_profile_buffer->sides[(t_profile_buffer->active_side + 1) % 2];

		//wait for other side to finish.
		//this will not wait unless the profiling thread cant keep up,
		// in which case we need to wait at some point because we cant keep on buffering till infinity
		for(;;) {
			uint8_t* head = atomic_load_explicit(&next_side->head, memory_order_relaxed);
			uint8_t* tail = atomic_load_explicit(&next_side->tail, memory_order_relaxed);

			if(head == tail)
				break;

			futex_wait(&next_side->head, (uint32_t) head, -1);
		} 
		
		curr_side->end_qpc = qpc;
		curr_side->end_tsc = tsc;
		next_side->start_qpc = qpc;
		next_side->start_tsc = tsc;

		//reset
		atomic_store_explicit(&next_side->tail, next_side->begin, memory_order_relaxed);
		atomic_store_explicit(&next_side->head, next_side->begin, memory_order_relaxed);

		//change sides
		atomic_store_explicit(&t_profile_buffer->active_side, t_profile_buffer->active_side + 1, memory_order_seq_cst);

		//flush
		atomic_fetch_add_explicit(&g_profile_state.flushes_requested, 1, memory_order_relaxed);
	}

	return atomic_load_explicit(&t_profile_buffer_side->tail, memory_order_relaxed);
}

enum {
	PROFILE_RUN,
	PROFILE_STOP,
};

static void _profiler_thread_proc()
{
	Profile_State* state = &g_profile_state;
	for(;;) {
		uint64_t flushes_requested = atomic_load_explicit(&state->flushes_requested, memory_order_relaxed);
		uint64_t flushes_completed = atomic_load_explicit(&state->flushes_completed, memory_order_relaxed);
		double   flush_every = atomic_load_explicit(&state->flush_every, memory_order_relaxed);
		uint32_t run_state = atomic_load_explicit(&state->state, memory_order_relaxed);

		//Add all new thread buffers (rare)
		Profile_Buffer* new_buffers = atomic_exchange(&state->foreign_buffers, NULL);
		for(Profile_Buffer* curr = new_buffers; curr;)
		{
			Profile_Buffer* next = curr->next;

			//TODO: doubly linked list
			curr->next = state->local_buffers_first;
			state->local_buffers_first = curr;
			curr = next;
		}
		
		isize total_formatted_size = 0;
		for(uint32_t side_i = 1; side_i <= 2; side_i++)
		{
			for(Profile_Buffer* curr = state->local_buffers_first; curr; curr = curr->next;)
			{
				//always start from the inactive side
				Profile_Buffer_Side* side = &curr->sides[(curr->active_side + side_i) % 2];

				uint8_t* head = atomic_load_explicit(&side->head, memory_order_relaxed);
				uint8_t* tail = atomic_load_explicit(&side->tail, memory_order_acquire);

				if(head < tail) {
					total_formatted_size += head - tail;

					//todo format side
					atomic_store_explicit(&side->head, tail, memory_order_relaxed);
				}
			}
		}

		if(total_formatted_size > 0)
			fflush(state->output_file);
		
		atomic_store_explicit(&state->flushes_completed, flushes_requested, memory_order_relaxed);
		futex_wake_all(&state->flushes_completed);

		if(run_state != PROFILE_RUN)
		{
			//If we want to exit we need to ensure everything was written.
			//So we simply go around every thread buffer and check if it is empty.
			//We however need to perform this twice (reps), because it might be the
			// case that just after we checked the first time someone pushed. 
			// If we go around two times then we are sure the buffers were really all empty at some 
			// point in time.
			bool is_empty = true;
			for(uint32_t reps = 0; reps < 2; reps++)
				for(Profile_Buffer* curr = state->local_buffers_first; curr; curr = curr->next;)
					for(uint32_t side_i = 0; side_i < 2; side_i++)
					{
						Profile_Buffer_Side* side = &curr->sides[side_i];
						uint8_t* head = atomic_load_explicit(&side->head, memory_order_relaxed);
						uint8_t* tail = atomic_load_explicit(&side->tail, memory_order_relaxed);

						if(head != tail) {
							is_empty = false;
							goto break_out;
						}
					}

			break_out:
			if(is_empty)
				break;
		}

		//wait for next flush request or sleep for flush_every seconds.
		if(total_formatted_size == 0)
			for(;;) {
				uint64_t curr_requested = atomic_load_explicit(&state->flushes_requested, memory_order_relaxed);
				if(curr_requested != flushes_requested)
					break;

				bool timed_out = futex_wait(&state->flushes_requested, (uint32_t) curr_requested, flush_every) == false; 
				if(timed_out)
					break;
			}
	}

	printf("thread exiting\n");
	return 0;
}

typedef struct Profile_Compress_State {
	uint64_t prev_time; 
	uint32_t prev_zone_id; 
	uint32_t prev_i32_val; 
	uint64_t prev_i64_val; 

	Profile_Zone* first;
	uint32_t zone_i;
} Profile_Compress_State;

ATTRIBUTE_INLINE_NEVER
static uint32_t _profile_add_zone(Profile_Compress_State* state, Profile_Zone* zone)
{
	//check if this zone is already added only with different pointer 
	// (this can be caused by the same code being compiled in two different compilation units)
	for(Profile_Zone* curr = state->first; curr; curr->next)
	{
		if(zone->line == curr->line 
			&& strcmp(zone->func, curr->func)
			&& strcmp(zone->file, curr->file)
			&& strcmp(zone->name, curr->name)
			&& strcmp(zone->info, curr->info))
		{
			zone->id = curr->id;
			break;
		}
	}

	if(zone->id == 0)
	{
		state->zone_i += 1;
		zone->id = state->zone_i;
	}

	zone->next = state->first;
	state->first = zone;
	return state->zone_i;
}

static bool profile_format_block(Profile_Compress_State* state, void* block, isize block_size, bool compress)
{
	uint8_t* data = (uint8_t*) block; 
	for(isize iter = 0; iter + 7 < block_size;) {

		isize i = iter;
		uint64_t tagged_zone = 0; memcpy(&tagged_zone, data + i, 8);
		uint64_t time = 0; memcpy(&time, data + i, 8);
		bool is_start = !!(tagged_zone & 1);

		Profile_Zone* zone = (Profile_Zone*) (tagged_zone & ~7ull);
		uint32_t zone_id = zone->id;
		if(zone_id == 0) 
			zone_id = profile_add_zone(state, zone);

		uint32_t len = 0;
		if(zone->type != PROFILE_ZONE_TYPE_INSTANT && zone->type != PROFILE_ZONE_TYPE_TIMER)
			switch(zone->type)
			{
				case PROFILE_ZONE_TYPE_I32: { len = 4; } break;  
				case PROFILE_ZONE_TYPE_I64: { len = 8; } break;  
				case PROFILE_ZONE_TYPE_F32: { len = 4; } break;  
				case PROFILE_ZONE_TYPE_F64: { len = 8; } break;  
			}

		
	}
}