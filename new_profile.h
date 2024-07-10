
#if !defined(JOT_NEW_PROFILE)
#define JOT_NEW_PROFILE

#include "new_profile_preinclude.h"

#include "hash.h"
#include "hash_index.h"
#include "array.h"
#include "perf.h"

typedef struct Profile_Thread_Zone {
	Platform_Thread thread;
	
	struct Profile_Thread_Zone* next;
	struct Profile_Thread_Zone* prev;

	Perf_Counter counter;
} Profile_Thread_Zone;

typedef struct Profile_Zone {
	Profile_ID id;
	uint64_t mean_estimate;
	uint64_t thread_zone_count;

	Profile_Thread_Zone* first;
	Profile_Thread_Zone* last;
} Profile_Zone;

typedef struct Profile_Zone_Stats {
	Perf_Stats stats;
	Profile_ID id;
} Profile_Zone_Stats;

typedef Array(Profile_Zone) Profile_Zone_Array;
typedef Array(Profile_Zone_Stats) Profile_Zone_Stats_Array;

typedef struct Profile_Global_Data {
	Platform_Mutex mutex;
	
	Hash_Index zone_hash;
	Profile_Zone_Array zones;

	uint64_t is_init;
	uint64_t init_time;
	int32_t max_threads;
	int32_t pad;
} Profile_Global_Data;

EXPORT bool profile_get_stats(Profile_Zone_Stats_Array* stats);
EXPORT void profile_init(Allocator* alloc);
EXPORT ATTRIBUTE_INLINE_NEVER void profile_init_thread_zone(Profile_Thread_Zone** handle, const Profile_ID* zone_id, uint64_t mean_estimate);

static inline int64_t fenced_now();
static inline int64_t profile_now();
static inline void profile_submit(Profile_Type type, Profile_Thread_Zone** handle, const Profile_ID* zone_id, int64_t before, int64_t after);

#include <stdint.h>
#ifdef _MSC_VER
# include <intrin.h>
#else
# include <x86intrin.h>
#endif

static inline int64_t fenced_now()
{ 
	_ReadWriteBarrier(); 
    _mm_lfence();
	return (int64_t) __rdtsc();
}

static inline int64_t profile_now()
{
	_ReadWriteBarrier(); 
	return (int64_t) __rdtsc();
}

static inline void profile_submit(Profile_Type type, Profile_Thread_Zone** handle, const Profile_ID* zone_id, int64_t before, int64_t after)
{
	ASSERT(zone_id->type == type);
	if((*handle) == NULL)
		profile_init_thread_zone(handle, zone_id, after - before);
		
	switch(type)
	{
		case PROFILE_DEFAULT: {
			int64_t delta = after - before;
			int64_t offset_delta = delta - (*handle)->counter.mean_estimate;
			(*handle)->counter.sum_of_squared_offset_counters += offset_delta*offset_delta;
			(*handle)->counter.min_counter = MIN((*handle)->counter.min_counter, delta);
			(*handle)->counter.max_counter = MAX((*handle)->counter.max_counter, delta);
		}
		case PROFILE_FAST: {
			(*handle)->counter.counter += after - before;
		}
		case PROFILE_COUNTER: {
			(*handle)->counter.runs += 1;
		}
	}
}
#endif 

#if (defined(JOT_ALL_IMPL) || defined(JOT_NEW_PROFILE_IMPL)) && !defined(JOT_NEW_PROFILE_HAS_IMPL)
#define JOT_NEW_PROFILE_HAS_IMPL

static ATTRIBUTE_THREAD_LOCAL Profile_Thread_Zone gfallback_thread_zones = {0}; 
static Profile_Global_Data gprofile_data = {0};

EXPORT void profile_init(Allocator* alloc)
{
	platform_mutex_init(&gprofile_data.mutex);
	
	hash_index_init(&gprofile_data.zone_hash, alloc);
	array_init(&gprofile_data.zones, alloc);

	gprofile_data.init_time = profile_now();
	gprofile_data.is_init = true;
}

INTERNAL uint64_t profile_hash_zone(Profile_ID zone_id)
{
	uint64_t file_hash = xxhash64(zone_id.file, strlen(zone_id.file), 0);
	uint64_t func_hash = xxhash64(zone_id.function, strlen(zone_id.function), 0);
	uint64_t name_hash = xxhash64(zone_id.name, strlen(zone_id.name), 0);

	uint64_t hash = file_hash ^ func_hash ^ name_hash;
	return hash;
}

INTERNAL bool profile_id_compare(Profile_ID id1, Profile_ID id2)
{
	return strcmp(id1.function, id2.function) == 0
			&& strcmp(id1.file, id2.file) == 0
			&& strcmp(id1.name, id2.name) == 0;
}

INTERNAL isize profile_find_zone(Profile_Global_Data* profile_data, uint64_t hash, Profile_ID zone_id)
{
	isize found = hash_index_find(profile_data->zone_hash, hash);
	while(found != -1)
	{
		isize index = profile_data->zone_hash.entries[found].value;
		Profile_Zone* zone = &profile_data->zones.data[index];
		if(profile_id_compare(zone->id, zone_id))
			return index;

		found = hash_index_find_next(profile_data->zone_hash, hash, found);
	}

	return -1;
}

INTERNAL isize profile_add_zone(Profile_Global_Data* profile_data, uint64_t hash, Profile_ID zone_id, uint64_t mean_estimate)
{
	hash_index_insert(&profile_data->zone_hash, hash, profile_data->zones.size);

	Profile_Zone zone = {PROFILE_UNINIT};
	zone.id = zone_id;
	zone.mean_estimate = mean_estimate;

	array_push(&profile_data->zones, zone);
	return profile_data->zones.size - 1;
}

EXPORT ATTRIBUTE_INLINE_NEVER void profile_init_thread_zone(Profile_Thread_Zone** handle, const Profile_ID* zone_id, uint64_t mean_estimate)
{
	if(gprofile_data.is_init)
	{
		platform_mutex_lock(&gprofile_data.mutex);
		uint64_t hash = profile_hash_zone(*zone_id);

		//thats right. we malloc each thread zone individually :)
		(*handle) = (Profile_Thread_Zone*) platform_heap_reallocate(sizeof(Profile_Thread_Zone), NULL, 64);
		memset((*handle), 0, sizeof *(*handle));

		isize zone_i = profile_find_zone(&gprofile_data, hash, *zone_id);
		if(zone_i == -1)
			zone_i = profile_add_zone(&gprofile_data, hash, *zone_id, mean_estimate);

		Profile_Zone* zone = &gprofile_data.zones.data[zone_i];
		zone->thread_zone_count += 1;
		if(zone->first == NULL)
		{
			zone->first = (*handle);
			zone->last = (*handle);
		}
		else
		{
			(*handle)->prev = zone->last;
			zone->last->next = (*handle);
			zone->last = (*handle);
		}

		(*handle)->thread = platform_thread_get_current();
		perf_init(&(*handle)->counter, zone->mean_estimate);
		platform_mutex_unlock(&gprofile_data.mutex);
	}
	else
	{
		(*handle) = &gfallback_thread_zones;
	}
}

EXPORT bool profile_get_stats(Profile_Zone_Stats_Array* stats)
{
	if(gprofile_data.is_init)
	{
		platform_mutex_lock(&gprofile_data.mutex);
		array_resize(stats, gprofile_data.zones.size);
		
		for(isize i = 0; i < gprofile_data.zones.size; i++)
		{
			Profile_Zone* zone = &gprofile_data.zones.data[i];
			Perf_Counter combined = {0};
			for(Profile_Thread_Zone* thread_zone = zone->first; thread_zone != NULL; thread_zone = thread_zone->next)
			{
				if(thread_zone == zone->first)
					combined = thread_zone->counter;
				else
					combined = perf_counter_merge(combined, thread_zone->counter, NULL);
			}
			
			Profile_Zone_Stats* out_stats = &stats->data[i];
			out_stats->id = zone->id;
			out_stats->stats = perf_get_stats(combined, 1);
		}

		platform_mutex_unlock(&gprofile_data.mutex);
		return true;
	}
	else
	{
		array_resize(stats, 0);
		return false;
	}
}
#endif