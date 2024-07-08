#ifndef JOT_PROFILE_UTILS
#define JOT_PROFILE_UTILS

#include "profile.h"
#include "array.h"
#include "log.h"
#include "string.h"
#include "vformat.h"
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
	
	EXPORT void log_perf_stats_hdr(const char* log_module, Log_Type log_type, const char* label)
	{
		LOG(log_module, log_type, "%s     time |        runs |   σ/μ", label);
	}
	EXPORT void log_perf_stats_row(const char* log_module, Log_Type log_type, const char* label, Perf_Stats stats)
	{
		LOG(log_module, log_type, "%s%.2es | %11lli | %5.2lf", label, stats.average_s, stats.runs, stats.normalized_standard_deviation_s);
	}

	EXPORT void log_perf_counters(const char* log_module, Log_Type log_type, Log_Perf_Sort_By sort_by)
	{
		String common_prefix = {0};

		Array(Global_Perf_Counter) counters = {allocator_get_default()};
		for(Global_Perf_Counter* counter = profile_get_counters(); counter != NULL; counter = counter->next)
		{

			String curent_file = string_of(counter->file);
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
			case PERF_SORT_BY_NAME: qsort(counters.data, (size_t) counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_file_func); break;
			case PERF_SORT_BY_TIME: qsort(counters.data, (size_t) counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_total_time_func); break;
			case PERF_SORT_BY_RUNS: qsort(counters.data, (size_t) counters.size, sizeof(Global_Perf_Counter), perf_counter_compare_runs); break;
		}

		LOG(log_module, log_type, "Logging perf counters (still running %lli):", (lli) profile_get_total_running_counters_count());
		log_group();
			LOG(log_module, log_type, "    total ms | average ms |  runs  |  σ/μ  | [min max] ms        | source");
			for(isize i = 0; i < counters.size; i++)
			{
				Global_Perf_Counter counter = counters.data[i];
				Perf_Stats stats = perf_get_stats(counter.counter, 1);

				const char* name = "";
				if(string_of(counter.name).size > 0)
					name = format_ephemeral("'%s'", counter.name).data;

				LOG(log_module, log_type, "%13.4lf %.3e %8lli %7.2lf [%9.4lf %9.2lf] %25s %-4lli %s %s", 
					stats.total_s*1000,
					stats.average_s*1000,
					(lli) stats.runs,
					stats.normalized_standard_deviation_s,
					stats.min_s*1000,
					stats.max_s*1000,
					counter.file + common_prefix.size,
					(lli) counter.line,
					counter.function,
					name
				);
				if(counter.concurrent_running_counters > 0)
				{
					log_group();
					LOG(log_module, log_type, "COUNTER LEAKS! Still running %lli", (lli) counter.concurrent_running_counters);
					log_ungroup();
				}
			}
		log_ungroup();

		array_deinit(&counters);
	}
#endif