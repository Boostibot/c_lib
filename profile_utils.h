#ifndef JOT_PROFILE_UTILS
#define JOT_PROFILE_UTILS

#include "profile.h"
#include "array.h"
#include <stdlib.h>

typedef enum Log_Perf_Sort_By{
	PERF_SORT_BY_NAME,
	PERF_SORT_BY_TIME,
	PERF_SORT_BY_RUNS,
} Log_Perf_Sort_By;

EXPORT void log_perf_stats(const char* log_module, Log_Type log_type, const char* prefix, Perf_Stats stats);
EXPORT void log_perf_counters(const char* log_module, Log_Type log_type, Log_Perf_Sort_By sort_by);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PROFILE_UTILS_IMPL)) && !defined(JOT_PROFILE_UTILS_HAS_IMPL)
#define JOT_PROFILE_UTILS_HAS_IMPL
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

	
	EXPORT void log_perf_stats(const char* log_module, Log_Type log_type, const char* prefix, Perf_Stats stats)
	{
		LOG(log_module, log_type, "%s avg: %12.8lf runs: %-10lli σ/μ %5.2lf [%12.8lf %12.6lf] (ms)", 
			prefix,
			stats.average_s*1000,
			(lli) stats.runs,
			stats.normalized_standard_deviation_s,
			stats.min_s*1000,
			stats.max_s*1000
		);
	}

	EXPORT void log_perf_counters(const char* log_module, Log_Type log_type, Log_Perf_Sort_By sort_by)
	{
		DEFINE_ARRAY_TYPE(Global_Perf_Counter, _Global_Perf_Counter_Array);

		String common_prefix = {0};

		_Global_Perf_Counter_Array counters = {0};
		for(Global_Perf_Counter* counter = profile_get_counters(); counter != NULL; counter = counter->next)
		{

			String curent_file = string_make(counter->file);
			if(common_prefix.data == NULL)
				common_prefix = curent_file;
			else
			{
				isize matches_to = 0;
				isize min_size = MIN(common_prefix.size, curent_file.size);
				for(; matches_to < min_size; matches_to++)
				{
					if(common_prefix.data[matches_to] != curent_file.data[matches_to])
						break;
				}

				common_prefix = string_safe_head(common_prefix, matches_to);
			}

			array_push(&counters, *counter);
		}
    
		switch(sort_by)
		{
			default:
			case PERF_SORT_BY_NAME: qsort(counters.data, counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_file_func); break;
			case PERF_SORT_BY_TIME: qsort(counters.data, counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_total_time_func); break;
			case PERF_SORT_BY_RUNS: qsort(counters.data, counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_runs); break;
		}

		LOG(log_module, log_type, "Logging perf counters (still running %lli):", (lli) profile_get_total_running_counters_count());
		log_group_push();
		for(isize i = 0; i < counters.size; i++)
		{
			Global_Perf_Counter counter = counters.data[i];
			Perf_Stats stats = perf_get_stats(counter.counter, 1);

			if(counter.is_detailed)
			{
				LOG(log_module, log_type, "total: %15.7lf avg: %13.6lf runs: %-8lli σ/μ %13.6lf [%13.6lf %13.6lf] (ms) from %20s %-4lli %s \"%s\"", 
					stats.total_s*1000,
					stats.average_s*1000,
					(lli) stats.runs,
					stats.normalized_standard_deviation_s,
					stats.min_s*1000,
					stats.max_s*1000,
					counter.file + common_prefix.size,
					(lli) counter.line,
					counter.function,
					counter.name
				);
			}
			else
			{
				LOG(log_module, log_type, "total: %15.8lf avg: %13.6lf runs: %-8lli (ms) from %20s %-4lli %s \"%s\"", 
					stats.total_s*1000,
					stats.average_s*1000,
					(lli) stats.runs,
					counter.file + common_prefix.size,
					(lli) counter.line,
					counter.function,
					counter.name
				);
			}
			if(counter.concurrent_running_counters > 0)
			{
				log_group_push();
				LOG(log_module, log_type, "COUNTER LEAKS! Still running %lli", (lli) counter.concurrent_running_counters);
				log_group_pop();
			}
		}
		log_group_pop();

		array_deinit(&counters);
	}
#endif