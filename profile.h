
#if !defined(JOT_profile)
#define JOT_profile

#include "profile_defs.h"

#include "hash.h"
#include "hash_index.h"
#include "array.h"
#include "perf.h"
#include "log.h"
#include "string.h"
#include "vformat.h"
#include "arena_stack.h"
#include <stdlib.h>

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

EXTERNAL bool profile_get_stats(Profile_Zone_Stats_Array* stats);
EXTERNAL void profile_init(Allocator* alloc);
EXTERNAL ATTRIBUTE_INLINE_NEVER void profile_init_thread_zone(Profile_Thread_Zone** handle, const Profile_ID* zone_id, uint64_t mean_estimate);

static _PROFILE_INLINE_ALWAYS int64_t fenced_now();
static _PROFILE_INLINE_ALWAYS int64_t profile_now();
static _PROFILE_INLINE_ALWAYS void profile_submit(Profile_Type type, Profile_Thread_Zone** handle_ptr, const Profile_ID* zone_id, int64_t before, int64_t after);

typedef enum Log_Perf_Sort_By{
	PERF_SORT_BY_NAME,
	PERF_SORT_BY_TIME,
	PERF_SORT_BY_RUNS,
} Log_Perf_Sort_By;

EXTERNAL void profile_log_all(Log log, Log_Perf_Sort_By sort_by);
EXTERNAL void log_perf_stats_hdr(Log log, const char* label);
EXTERNAL void log_perf_stats_row(Log log, const char* label, Perf_Stats stats);

//@TODO: Move into platform layer
#include <stdint.h>
#ifdef _MSC_VER
# include <intrin.h>
#else
# include <x86intrin.h>
#endif

static _PROFILE_INLINE_ALWAYS int64_t fenced_now()
{ 
	_ReadWriteBarrier(); 
    _mm_lfence();
	return (int64_t) __rdtsc();
}

static _PROFILE_INLINE_ALWAYS int64_t profile_now()
{
	_ReadWriteBarrier(); 
	return (int64_t) __rdtsc();
}

static _PROFILE_INLINE_ALWAYS void profile_submit(Profile_Type type, Profile_Thread_Zone** handle, const Profile_ID* zone_id, int64_t before, int64_t after)
{
	ASSERT(zone_id->type == type);
	if(*handle == NULL)
		profile_init_thread_zone(handle, zone_id, type != PROFILE_COUNTER ? after - before : 0);
		
	switch(type)
	{
		case PROFILE_DEFAULT: {
			perf_submit_no_init(&(*handle)->counter, after - before); 
		} break;
		case PROFILE_FAST: {
			(*handle)->counter.counter += after - before;
		}
		case PROFILE_COUNTER: {
			(*handle)->counter.runs += 1;
		}
	}
}
#endif 

#if (defined(JOT_ALL_IMPL) || defined(JOT_profile_IMPL)) && !defined(JOT_profile_HAS_IMPL)
#define JOT_profile_HAS_IMPL

static ATTRIBUTE_THREAD_LOCAL Profile_Thread_Zone gfallback_thread_zones = {0}; 
static Profile_Global_Data gprofile_data = {0};

EXTERNAL void profile_init(Allocator* alloc)
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
	hash_index_insert(&profile_data->zone_hash, hash, profile_data->zones.len);

	Profile_Zone zone = {PROFILE_UNINIT};
	zone.id = zone_id;
	zone.mean_estimate = mean_estimate;

	array_push(&profile_data->zones, zone);
	return profile_data->zones.len - 1;
}

EXTERNAL ATTRIBUTE_INLINE_NEVER void profile_init_thread_zone(Profile_Thread_Zone** handle, const Profile_ID* zone_id, uint64_t mean_estimate)
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
		(*handle)->counter = perf_counter_init(zone->mean_estimate);
		platform_mutex_unlock(&gprofile_data.mutex);
	}
	else
	{
		(*handle) = &gfallback_thread_zones;
	}
}

EXTERNAL bool profile_get_stats(Profile_Zone_Stats_Array* stats)
{
	if(gprofile_data.is_init)
	{
		platform_mutex_lock(&gprofile_data.mutex);
		array_resize(stats, gprofile_data.zones.len);
		
		for(isize i = 0; i < gprofile_data.zones.len; i++)
		{
			Profile_Zone* zone = &gprofile_data.zones.data[i];
			Perf_Counter combined = {0};
			for(Profile_Thread_Zone* thread_zone = zone->first; thread_zone != NULL; thread_zone = thread_zone->next)
			{
				//if(thread_zone == zone->first)
				//	combined = thread_zone->counter;
				//else
				//	combined = perf_counter_merge(combined, thread_zone->counter, NULL);
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

	EXTERNAL void log_perf_stats_hdr(Log log, const char* label)
	{
		LOG(log, "%s     time |        runs |   σ/μ", label);
	}
	EXTERNAL void log_perf_stats_row(Log log, const char* label, Perf_Stats stats)
	{
		LOG(log, "%s%s | %11lli | %5.2lf", label, format_seconds(stats.average_s, 5).data, stats.runs, stats.normalized_standard_deviation_s);
	}
	
	INTERNAL int _profile_compare_runs(const void* a_, const void* b_)
	{
		Profile_Zone_Stats* a = (Profile_Zone_Stats*) a_;
		Profile_Zone_Stats* b = (Profile_Zone_Stats*) b_;
    
		if(a->stats.runs > b->stats.runs)
			return -1;
		else 
			return 1;
	}
	
	INTERNAL int _profile_compare_total_time_func(const void* a_, const void* b_)
	{
		Profile_Zone_Stats* a = (Profile_Zone_Stats*) a_;
		Profile_Zone_Stats* b = (Profile_Zone_Stats*) b_;
    
		if(a->stats.total_s > b->stats.total_s)
			return -1;
		else 
			return 1;
	}

	INTERNAL int _profile_compare_file_func(const void* a_, const void* b_)
	{
		Profile_Zone_Stats* a = (Profile_Zone_Stats*) a_;
		Profile_Zone_Stats* b = (Profile_Zone_Stats*) b_;
    
		int res = strcmp(a->id.file, b->id.file);
		if(res == 0)
			res = strcmp(a->id.function, b->id.function);
		if(res == 0)
			res = strcmp(a->id.name, b->id.name);

		return res;
	}

	EXTERNAL void profile_log_all(Log stream, Log_Perf_Sort_By sort_by)
	{
		(void) sort_by;
		SCRATCH_ARENA(arena)
		{
			Profile_Zone_Stats_Array all_stats = {arena.alloc};
			profile_get_stats(&all_stats);
			
			String common_prefix = {0};
			for(isize i = 0; i < all_stats.len; i++)
			{
				String file = string_of(all_stats.data[i].id.file);
				if(i == 0)
					common_prefix = file;
				else
				{
					isize k = 0;
					for(; k < MIN(common_prefix.len, file.len); k++)
						if(common_prefix.data[k] != file.data[k])
							break;

					common_prefix = string_head(common_prefix, k);
				}
			}

			switch(sort_by)
			{
				default:
				case PERF_SORT_BY_NAME: qsort(all_stats.data, (size_t) all_stats.len, sizeof *all_stats.data, _profile_compare_file_func); break;
				case PERF_SORT_BY_TIME: qsort(all_stats.data, (size_t) all_stats.len, sizeof *all_stats.data, _profile_compare_total_time_func); break;
				case PERF_SORT_BY_RUNS: qsort(all_stats.data, (size_t) all_stats.len, sizeof *all_stats.data, _profile_compare_runs); break;
			}

			LOG(stream, "Logging perf counters (still running %lli):", (lli) 0);
				LOG(stream, "    total ms | average ms |  runs  |  σ/μ  | [min max] ms        | source");
				for(isize i = 0; i < all_stats.len; i++)
				{
					Profile_Zone_Stats single = all_stats.data[i];

					const char* name = "";
					if(string_of(single.id.name).len > 0)
						name = format(arena.alloc, "'%s'", single.id.name).data;

					if(single.id.type == PROFILE_DEFAULT)
					{
						LOG(stream, "%s %s %12lli %5.2lf [%s %s] %25s %-4lli %s %s", 
							format_seconds(single.stats.total_s,9).data,
							format_seconds(single.stats.average_s, 7).data,
							(lli) single.stats.runs,
							single.stats.normalized_standard_deviation_s,
							format_seconds(single.stats.min_s, 7).data,
							format_seconds(single.stats.max_s, 7).data,
							single.id.file + common_prefix.len,
							(lli) single.id.line,
							single.id.function,
							name
						);
					}
					if(single.id.type == PROFILE_FAST)
					{
						LOG(stream, "%s %s %8lli %25s %-4lli %s %s", 
							format_seconds(single.stats.total_s, 9).data,
							format_seconds(single.stats.average_s, 7).data,
							(lli) single.stats.runs,
							single.id.file + common_prefix.len,
							(lli) single.id.line,
							single.id.function,
							name
						);
					}
					
					if(single.id.type == PROFILE_COUNTER)
					{
						LOG(stream, "%8lli %25s %-4lli %s %s", 
							(lli) single.stats.runs,
							single.id.file + common_prefix.len,
							(lli) single.id.line,
							single.id.function,
							name
						);
					}
				}

		}
	}
#endif