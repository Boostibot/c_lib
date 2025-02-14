#pragma once

#include "../array.h"
#include "../random.h"
#include "../time.h"
#include "../allocator_debug.h"

INTERNAL void test_array_stress(f64 max_seconds)
{
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_USE);
	{
		PROFILE_START();
		enum Action 
		{
			INIT,
			CLEAR,
			SET_CAPACITY,
			PUSH,
			POP,
			RESERVE,
			RESIZE,
			APPEND,
			COPY, //when we assign between the two arrays we switch and use the otehr one
		};

		Discrete_Distribution dist[] = {
			{INIT,			1},
			{CLEAR,			2},
			{SET_CAPACITY,	2},
			{PUSH,			50},
			{POP,			10},
			{RESERVE,		5},
			{RESIZE,		5},
			{APPEND,		20},
			{COPY,			5},
		};
		
		enum {
			MAX_ITERS = 1000*1000*10,
			MIN_ITERS = 100,
			MAX_CAPACITY = 1000*10,
		};

		i64_Array array1 = {debug_alloc.alloc};
		i64_Array array2 = {debug_alloc.alloc};

		i64_Array* arr = &array1;
		i64_Array* other_array = &array2;
		
		random_discrete_make(dist, ARRAY_LEN(dist));

		isize max_size = 0;
		isize max_capacity = 0;
		f64 start = clock_sec();
		for(isize i = 0; i < MAX_ITERS; i++)
		{
			PROFILE_START(iter);
			if(clock_sec() - start >= max_seconds && i >= MIN_ITERS)
				break;

			isize action = random_discrete(dist, ARRAY_LEN(dist));
			TEST(generic_array_is_invariant(array_make_generic(arr)));
			
			switch(action)
			{
				case INIT: {
					array_deinit(arr);
					array_init(arr, debug_alloc.alloc);
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
					if(arr->count > 0)
					{
						i64 value = *array_last(*arr);
						TEST(is_power_of_two_or_zero(value));
						array_pop(arr);
					}
					break;
				}
				
				case RESERVE: {
					isize size_before = arr->count;
					isize capacity_before = arr->capacity;
					isize capacity = random_range(0, MAX_CAPACITY);
					array_reserve(arr, capacity);

					TEST(size_before == arr->count);
					TEST(capacity_before <= arr->capacity);
					break;
				}

				case RESIZE: {
					isize size = random_range(0, MAX_CAPACITY);
					array_resize(arr, size);
					TEST(arr->count == size);
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
					TEST(other_array->count == arr->count);
					TEST(other_array->capacity >= other_array->count);

					swap_any(&other_array, &arr, sizeof(arr));

					break;
				}
			}
			
			if(max_size < arr->count)
				max_size = arr->count;
			if(max_capacity < arr->capacity)
				max_capacity = arr->capacity;

			for(isize k = 0; k < arr->count; k++)
				TEST(arr->data != NULL && is_power_of_two_or_zero(arr->data[k]));
				
			TEST(generic_array_is_invariant(array_make_generic(arr)));
			PROFILE_STOP(iter);
		}

		array_deinit(&array1);
		array_deinit(&array2);
		PROFILE_STOP();
	}
	debug_allocator_deinit(&debug_alloc);
}

INTERNAL void test_array(f64 max_seconds)
{
	test_array_stress(max_seconds);
}