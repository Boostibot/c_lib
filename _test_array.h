#pragma once

#include "_test.h"
#include "array.h"

INTERNAL void test_array_stress(f64 max_seconds)
{
	isize mem_before = allocator_get_stats(allocator_get_default()).bytes_allocated;

	enum Action 
	{
		INIT,
		INIT_BACKED,
		DEINIT,
		CLEAR,
		SET_CAPACITY,
		PUSH,
		POP,
		RESERVE,
		RESIZE,
		APPEND,
		COPY, //when we assign between the two arrays we switch and use the otehr one

		ACTION_ENUM_COUNT,
	};

	i32 probabilities[ACTION_ENUM_COUNT] = {0};
	probabilities[INIT]				= 1;
	probabilities[INIT_BACKED]		= 1;
	probabilities[DEINIT]           = 1;
	probabilities[CLEAR]			= 2;
	probabilities[SET_CAPACITY]		= 2;
	probabilities[PUSH]				= 50;
	probabilities[POP]				= 10;
	probabilities[RESERVE]			= 5;
	probabilities[RESIZE]			= 5;
	probabilities[APPEND]			= 20;
	probabilities[COPY]			    = 5;
	
	enum {
		MAX_ITERS = 1000*1000*10,
		MIN_ITERS = 100,
		BACKING = 125,
		MAX_CAPACITY = 1000*10,
	};

	i64 buffer1[BACKING] = {0};
	i64 buffer2[BACKING] = {0};
	i64_Array array1 = {0};
	i64_Array array2 = {0};

	i64* buffer = buffer1;
	i64_Array* arr = &array1;
	
	i64* other_buffer = buffer2;
	i64_Array* other_array = &array2;
	
	Discrete_Distribution dist = random_discrete_make(probabilities, ACTION_ENUM_COUNT);

	isize max_size = 0;
	isize max_capacity = 0;
	f64 start = clock_s();
	for(isize i = 0; i < MAX_ITERS; i++)
	{
		if(clock_s() - start >= max_seconds && i >= MIN_ITERS)
			break;

		i32 action = random_discrete(&dist);
		TEST(_array_is_invariant(arr, sizeof *arr->data));
		
		switch(action)
		{
			case INIT: {
				array_deinit(arr);
				array_init(arr, allocator_get_default());
				break;
			}
			
			case INIT_BACKED: {
				array_deinit(arr);
				array_init_backed_from_memory(arr, allocator_get_default(), buffer, BACKING);
				break;
			}
			
			case DEINIT: {
				array_deinit(arr);
				break;
			}
			
			case CLEAR: {
				array_clear(arr);
				break;
			}
			
			case SET_CAPACITY: {
				isize capacity = random_range(0, MAX_CAPACITY);
				array_set_capacity(arr, capacity);
				break;
			}
			
			case PUSH: {
				i64 offset = random_range(0, 64);
				CHECK_BOUNDS(offset, 64);

				i64 value = (i64) 1 << offset;
				ASSERT(arr != NULL);
				array_push(arr, value);
				TEST(value);
				TEST(arr->data != NULL);
				break;
			}
			
			case POP: {
				if(arr->size > 0)
				{
					i64 value = *array_last(*arr);
					TEST(is_power_of_two_or_zero(value));
					array_pop(arr);
				}
				break;
			}
			
			case RESERVE: {
				isize size_before = arr->size;
				isize capacity_before = arr->capacity;
				isize capacity = random_range(0, MAX_CAPACITY);
				array_reserve(arr, capacity);

				TEST(size_before == arr->size);
				TEST(capacity_before <= arr->capacity);
				break;
			}

			case RESIZE: {
				isize size = random_range(0, MAX_CAPACITY);
				array_resize(arr, size);
				TEST(arr->size == size);
				TEST(arr->capacity >= size);
				break;
			}
			
			case APPEND: {
				i64 appended[64] = {0};
				isize append_count = random_range(0, 64);
				
				CHECK_BOUNDS(append_count, 64);
				for(isize k = 0; k < append_count; k++)
				{
					i64 value = (i64) 1 << random_range(0, 64);
					appended[k] = value;
				}
				
				array_append(arr, appended, append_count);
				break;
			}
			
			case COPY: {
				array_copy(other_array, *arr);
				TEST(other_array->size == arr->size);
				TEST(other_array->capacity >= other_array->size);

				swap_any(&other_array, &arr, sizeof(arr));
				swap_any(&other_buffer, &buffer, sizeof(buffer));

				break;
			}
		}
		
		if(max_size < arr->size)
			max_size = arr->size;
		if(max_capacity < arr->capacity)
			max_capacity = arr->capacity;

		for(isize k = 0; k < arr->size; k++)
			TEST(arr->data != NULL && is_power_of_two_or_zero(arr->data[k]));

		TEST(_array_is_invariant(arr, sizeof *arr->data));
	}
	
	random_discrete_deinit(&dist);
	array_deinit(&array1);
	array_deinit(&array2);

	isize mem_after = allocator_get_stats(allocator_get_default()).bytes_allocated;
	TEST(mem_before == mem_after);
}

INTERNAL void test_array(f64 max_seconds)
{
	test_array_stress(max_seconds);
}