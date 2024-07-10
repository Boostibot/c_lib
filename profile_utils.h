#ifndef JOT_PROFILE_UTILS
#define JOT_PROFILE_UTILS

#include "new_profile.h"
#include "array.h"
#include "log.h"
#include "string.h"
#include "vformat.h"
#include "arena.h"
#include <stdlib.h>

typedef enum Log_Perf_Sort_By{
	PERF_SORT_BY_NAME,
	PERF_SORT_BY_TIME,
	PERF_SORT_BY_RUNS,
} Log_Perf_Sort_By;

EXPORT void log_perf_counters(const char* log_module, Log_Type log_type, Log_Perf_Sort_By sort_by);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PROFILE_UTILS_IMPL)) && !defined(JOT_PROFILE_UTILS_HAS_IMPL)
#define JOT_PROFILE_UTILS_HAS_IMPL
	
	EXPORT void log_perf_stats_hdr(const char* log_module, Log_Type log_type, const char* label)
	{
		LOG(log_module, log_type, "%s     time |        runs |   σ/μ", label);
	}
	EXPORT void log_perf_stats_row(const char* log_module, Log_Type log_type, const char* label, Perf_Stats stats)
	{
		LOG(log_module, log_type, "%s%s | %11lli | %5.2lf", label, format_seconds(stats.average_s, 5).data, stats.runs, stats.normalized_standard_deviation_s);
	}
	#if 0
		EXPORT void log_perf_counters(const char* log_module, Log_Type log_type, Log_Perf_Sort_By sort_by)
	{
		(void) log_module;
		(void) log_type;
		(void) sort_by;
	}
	
	INTERNAL int perf_counter_compare_runs(const void* a_, const void* b_)
	{
		Global_Perf_Counter* a = (Global_Perf_Counter*) a_;
		Global_Perf_Counter* b = (Global_Perf_Counter*) b_;
    
		if(a->counter.runs > b->counter.runs)
			return -1;
		else 
			return 1;
	}
	
	INTERNAL int perf_counter_compare_total_time_func(const void* a_, const void* b_)
	{
		Global_Perf_Counter* a = (Global_Perf_Counter*) a_;
		Global_Perf_Counter* b = (Global_Perf_Counter*) b_;
    
		if(profile_get_counter_total_running_time_s(*a) > profile_get_counter_total_running_time_s(*b))
			return -1;
		else 
			return 1;
	}

	INTERNAL int perf_counter_compare_file_func(const void* a_, const void* b_)
	{
		Global_Perf_Counter* a = (Global_Perf_Counter*) a_;
		Global_Perf_Counter* b = (Global_Perf_Counter*) b_;
    
		int res = strcmp(a->file, b->file);
		if(res == 0)
			res = strcmp(a->function, b->function);
		if(res == 0)
			res = strcmp(a->name, b->name);

		return res;
	}
	#endif

	EXPORT void log_perf_counters(const char* log_module, Log_Type log_type, Log_Perf_Sort_By sort_by)
	{
		(void) sort_by;
		Arena_Frame arena = scratch_arena_acquire();
		Profile_Zone_Stats_Array all_stats = {&arena.allocator};
		profile_get_stats(&all_stats);
		
		String common_prefix = {0};
		for(isize i = 0; i < all_stats.size; i++)
		{
			String file = string_of(all_stats.data[i].id.file);
			isize k = 0;
			for(; k < MIN(common_prefix.size, file.size); k++)
				if(file.data[k] != file.data[k])
					break;

			common_prefix = string_head(common_prefix, k);
		}


		#if 0
		switch(sort_by)
		{
			default:
			case PERF_SORT_BY_NAME: qsort(counters.data, (size_t) counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_file_func); break;
			case PERF_SORT_BY_TIME: qsort(counters.data, (size_t) counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_total_time_func); break;
			case PERF_SORT_BY_RUNS: qsort(counters.data, (size_t) counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_runs); break;
		}
		#endif

		LOG(log_module, log_type, "Logging perf counters (still running %lli):", (lli) 0);
		log_group();
			LOG(log_module, log_type, "    total ms | average ms |  runs  |  σ/μ  | [min max] ms        | source");
			for(isize i = 0; i < all_stats.size; i++)
			{
				Profile_Zone_Stats single = all_stats.data[i];

				const char* name = "";
				if(string_of(single.id.name).size > 0)
					name = format_ephemeral("'%s'", single.id.name).data;

				if(single.id.type == PROFILE_DEFAULT)
				{
					LOG(log_module, log_type, "%s %s %8lli %5.2lf [%s %s] %25s %-4lli %s %s", 
						format_seconds(single.stats.total_s,9).data,
						format_seconds(single.stats.average_s, 7).data,
						(lli) single.stats.runs,
						single.stats.normalized_standard_deviation_s,
						format_seconds(single.stats.min_s, 7).data,
						format_seconds(single.stats.max_s, 7).data,
						single.id.file + common_prefix.size,
						(lli) single.id.line,
						single.id.function,
						name
					);
				}
				if(single.id.type == PROFILE_FAST)
				{
					LOG(log_module, log_type, "%s %s %8lli %25s %-4lli %s %s", 
						format_seconds(single.stats.total_s, 9).data,
						format_seconds(single.stats.average_s, 7).data,
						(lli) single.stats.runs,
						single.id.file + common_prefix.size,
						(lli) single.id.line,
						single.id.function,
						name
					);
				}
				
				if(single.id.type == PROFILE_COUNTER)
				{
					LOG(log_module, log_type, "%8lli %25s %-4lli %s %s", 
						(lli) single.stats.runs,
						single.id.file + common_prefix.size,
						(lli) single.id.line,
						single.id.function,
						name
					);
				}
			}
		log_ungroup();

		arena_frame_release(&arena);
	}
#endif