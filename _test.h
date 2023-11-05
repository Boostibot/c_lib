#pragma once

#include "defines.h"
#include "array.h"
#include "time.h"
#include "random.h"



typedef struct Discrete_Distribution
{
	i32_Array prob_table;
	i32 prob_sum;
} Discrete_Distribution;

INTERNAL Discrete_Distribution random_discrete_make(const i32 probabilities[], isize probabilities_size)
{
	i32 prob_sum = 0;
	for(isize i = 0; i < probabilities_size; i++)
		prob_sum += probabilities[i];

	i32_Array prob_table = {0};
	array_resize(&prob_table, prob_sum);
	
	i32 k = 0;
	for(i32 i = 0; i < probabilities_size; i++)
	{
		i32 end = k + probabilities[i];
		for(; k < end; k++)
		{
			CHECK_BOUNDS(k, prob_table.size);
			prob_table.data[k] = i;
		}
	}

	Discrete_Distribution out = {prob_table, prob_sum};
	return out;
}

INTERNAL i32 random_discrete(Discrete_Distribution distribution)
{
	i64 random = random_range(0, distribution.prob_sum);
	CHECK_BOUNDS(random, distribution.prob_table.size);
	i32 index = distribution.prob_table.data[random];
	return index;
}

INTERNAL void random_discrete_deinit(Discrete_Distribution* dist)
{
	array_deinit(&dist->prob_table);
	dist->prob_sum = 0;
}
