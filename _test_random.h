#pragma once
#include "_test.h"
#include "random.h"

#define RANDOM_TEST_ITERS		(1000*1000*200)
#define RANDOM_TEST_EPSILON		(2e-4)
#define RANDOM_HIST_SIZE		(10)
#define RANDOM_TEST_RANGE_FROM  (-513)
#define RANDOM_TEST_RANGE_TO	(487)

INTERNAL void test_swap_any()
{
	{
		int64_t before[10]	 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
		int64_t expected[10] = {1, 4, 3, 2, 5, 6, 7, 8, 9, 10};

		swap_any(&before[1], &before[3], sizeof(before[0]));
		for(int64_t i = 0; i < 10; i++)
			TEST(before[i] == expected[i]);
	}

	{
		int32_t before[10]   = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
		int32_t expected[10] = {1, 4, 3, 2, 5, 6, 7, 8, 9, 10};
		swap_any(&before[1], &before[3], sizeof(before[0]));

		for(int32_t i = 0; i < 10; i++)
			TEST(before[i] == expected[i]);
	}
	
	{
		typedef struct { int8_t x[179]; } Big;

		Big before[10]   = {{1}, {2}, {3}, {4}, {5, 3, 2, 3, -1, 3}, {6}, {7}, {8, 1, 13}, {9}, {10, 11}};
		Big expected[10] = {{1}, {2}, {3}, {4}, {10, 11}, {6}, {7}, {8, 1, 13}, {9}, {5, 3, 2, 3, -1, 3}};
		swap_any(&before[4], &before[9], sizeof(before[0]));

		for(int32_t i = 0; i < 10; i++)
			TEST(memcmp(&before[i], &expected[i], sizeof(before[0])) == 0);
	}
}

INTERNAL void process_histogram(const int64_t* histogram, int64_t hist_size, double* norm_avg_diff, double* norm_max_diff)
{
	int64_t min_val = RANDOM_TEST_ITERS;
	for(int64_t i = 0; i < hist_size; i++)
	{
		if(min_val > histogram[i])
			min_val = histogram[i];
	}

	int64_t max_diff = 0;
	int64_t total_diff = 0;
	for(int64_t i = 0; i < hist_size; i++)
	{
		int64_t diff = histogram[i] - min_val;
		total_diff += diff;
		if(max_diff < diff)
			max_diff = diff;
	}
	
	int64_t avg_diff = total_diff / hist_size;
	*norm_avg_diff = (double) avg_diff / (double) RANDOM_TEST_ITERS;
	*norm_max_diff = (double) max_diff / (double) RANDOM_TEST_ITERS;
}

INTERNAL void test_random_range()
{
	int64_t histogram[RANDOM_HIST_SIZE] = {0};
	int64_t BAR_SIZE = (RANDOM_TEST_RANGE_TO - RANDOM_TEST_RANGE_FROM)/RANDOM_HIST_SIZE;

	for(int64_t i = 0; i < RANDOM_TEST_ITERS; i++)
	{
		int64_t random = random_range(RANDOM_TEST_RANGE_FROM, RANDOM_TEST_RANGE_TO);
		TEST(RANDOM_TEST_RANGE_FROM <= random && random < RANDOM_TEST_RANGE_TO);
		int64_t abs_index = random - RANDOM_TEST_RANGE_FROM;
		int64_t index = abs_index / BAR_SIZE;

		ASSERT(0 <= index && index < RANDOM_HIST_SIZE);
		histogram[index] += 1;
	}
	
	double norm_avg_diff = 0;
	double norm_max_diff = 0;

	process_histogram(histogram, RANDOM_HIST_SIZE, &norm_avg_diff, &norm_max_diff);

	TEST(norm_avg_diff < RANDOM_TEST_EPSILON);
	TEST(norm_max_diff < RANDOM_TEST_EPSILON);
}


INTERNAL void test_random_f64()
{
	int64_t histogram[RANDOM_HIST_SIZE] = {0};
	double BAR_SIZE = 1.0 / RANDOM_HIST_SIZE;

	for(int64_t i = 0; i < RANDOM_TEST_ITERS; i++)
	{
		double random = random_f64();
		TEST(0 <= random && random < 1);
		int64_t index = (int64_t) (random / BAR_SIZE);
		ASSERT(0 <= index && index < RANDOM_HIST_SIZE);
		histogram[index] += 1;
	}
	
	double norm_avg_diff = 0;
	double norm_max_diff = 0;

	process_histogram(histogram, RANDOM_HIST_SIZE, &norm_avg_diff, &norm_max_diff);

	TEST(norm_avg_diff < RANDOM_TEST_EPSILON);
	TEST(norm_max_diff < RANDOM_TEST_EPSILON);
}


INTERNAL void test_random_f32()
{
	int64_t histogram[RANDOM_HIST_SIZE] = {0};
	float BAR_SIZE = 1.0f / RANDOM_HIST_SIZE;

	for(int64_t i = 0; i < RANDOM_TEST_ITERS; i++)
	{
		float random = random_f32();
		TEST(0 <= random && random < 1);
		int64_t index = (int64_t) (random / BAR_SIZE);
		ASSERT(0 <= index && index < RANDOM_HIST_SIZE);
		histogram[index] += 1;
	}
	
	double norm_avg_diff = 0;
	double norm_max_diff = 0;

	process_histogram(histogram, RANDOM_HIST_SIZE, &norm_avg_diff, &norm_max_diff);

	TEST(norm_avg_diff < RANDOM_TEST_EPSILON);
	TEST(norm_max_diff < RANDOM_TEST_EPSILON);
}

INTERNAL void test_random_bool()
{
	int64_t histogram[2] = {0};

	for(int64_t i = 0; i < RANDOM_TEST_ITERS; i++)
	{
		bool random = random_bool();
		histogram[random] += 1;
	}
	
	double norm_avg_diff = 0;
	double norm_max_diff = 0;

	process_histogram(histogram, 2, &norm_avg_diff, &norm_max_diff);

	TEST(norm_avg_diff < RANDOM_TEST_EPSILON);
	TEST(norm_max_diff < RANDOM_TEST_EPSILON);
}

INTERNAL void test_random()
{
	test_random_f32();
	test_swap_any();
	test_random_range();
	test_random_f64();
	test_random_bool();
}