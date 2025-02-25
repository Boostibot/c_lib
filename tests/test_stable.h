#pragma once

#include "../stable.h"
#include "../allocator_debug.h"

static void test_stable_unit()
{
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_USE);
    {
        Stable stable = {0};
        stable_init(&stable, debug_alloc.alloc, sizeof(int32_t));

        int32_t* val = NULL;
        isize i1 = stable_insert(&stable, (void**) &val);

        int32_t* val_get = (int32_t*) stable_at(&stable, i1);
        TEST(val == val_get);
        *val = 32;

        TEST(stable_at_or(&stable, -2, NULL) == NULL);
        TEST(stable_at_or(&stable, -1, NULL) == NULL);
        TEST(stable_at_or(&stable, 0, NULL) != NULL);
        TEST(stable_at_or(&stable, 1, NULL) == NULL);
        TEST(stable_at_or(&stable, 2, NULL) == NULL);
        stable_remove(&stable, 0);

        enum {INSERT_COUNT = 129};
        for(isize i = 0; i < INSERT_COUNT; i++)
        {
            int32_t* at = NULL;
            isize index = stable_insert(&stable, (void**) &at);
            *at = (int32_t) i;
            TEST(index == i);
        }
        
        STABLE_FOR(&stable, it, int32_t, value) 
            TEST(*value == it.index);

        for(isize i = 0; i < INSERT_COUNT; i++)
        {
            int32_t* at = (int32_t*) stable_at(&stable, i);
            TEST(*at == i);
            stable_remove(&stable, i);
        }

        stable_deinit(&stable);
    }

    debug_allocator_deinit(&debug_alloc);
}

#include "../random.h"
#include "../array.h"

typedef struct {
    int64_t index;
    uint64_t value;
} Test_Stable_Truth;

INTERNAL int _test_stable_compare(const void* a, const void* b)
{   
    int64_t ai = ((Test_Stable_Truth*) a)->index;
    int64_t bi = ((Test_Stable_Truth*) b)->index;
    return (ai > bi) - (ai < bi);
}

INTERNAL void test_stable_stress(double max_seconds)
{
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_USE | DEBUG_ALLOC_CAPTURE_CALLSTACK);
	{
		enum Action {
			INIT,
			CLEAR,
			INSERT,
			REMOVE,
			RESERVE,
		};

		Discrete_Distribution dist[] = {
			{INIT,			1},
			{CLEAR,			1},
			{INSERT,		5000},
			{REMOVE,	    100},
			{RESERVE,		10},
		};
		
		enum {
			MIN_ITERS = 100,
			MAX_CAPACITY = 1000*10,
		};

		random_discrete_make(dist, ARRAY_COUNT(dist));

		Array(Test_Stable_Truth) truth_array = {debug_alloc.alloc};
        Stable stable = {0};
        stable_init(&stable, debug_alloc.alloc, sizeof(uint64_t));

		isize max_size = 0;
		isize max_capacity = 0;
		double start = clock_sec();
		for(isize i = 0;; i++)
		{
			if(clock_sec() - start >= max_seconds && i >= MIN_ITERS)
				break;

			isize action = random_discrete(dist, ARRAY_COUNT(dist));
			switch(action) {
				case INIT: {
                    stable_deinit(&stable);
                    stable_init(&stable, debug_alloc.alloc, sizeof(uint64_t));
					array_clear(&truth_array);
				} break;
				
				case CLEAR: {
                    stable_clear(&stable);
					array_clear(&truth_array);
				} break;
				
				case INSERT: {
					uint64_t value = random_u64();
                    isize index = stable_insert_value(&stable, &value);

                    Test_Stable_Truth truth = {index, value};
					array_push(&truth_array, truth);
                    TEST(stable_at(&stable, index));
				} break;
				
				case REMOVE: {
					if(truth_array.count > 0) {
                        isize rand_index = random_range(0, truth_array.count);
                        Test_Stable_Truth truth = truth_array.data[rand_index];

                        array_remove_unordered(&truth_array, rand_index);
                        stable_remove(&stable, truth.index);
                        TEST(stable_at_or(&stable, truth.index, NULL) == NULL);
					}
				} break;
				
				case RESERVE: {
					isize capacity = random_range(0, MAX_CAPACITY);
					array_reserve(&truth_array, capacity);
                    stable_reserve(&stable, capacity);
				} break;
			}
			
			if(max_size < truth_array.count)
				max_size = truth_array.count;
			if(max_capacity < truth_array.capacity)
				max_capacity = truth_array.capacity;

			TEST(generic_array_is_consistent(array_make_generic(&truth_array)));
            TEST(truth_array.count == stable.count);

            //All items must be found
			for(isize k = 0; k < truth_array.count; k++) {
                Test_Stable_Truth* truth = &truth_array.data[k];
                TEST(truth->value == *(uint64_t*) stable_at(&stable, truth->index));
                TEST(truth->value == *(uint64_t*) stable_at_or(&stable, truth->index, NULL));
            }

            //iteration must iterate over all entries and must iterate in ascending order
            qsort(truth_array.data, truth_array.count, sizeof(Test_Stable_Truth), _test_stable_compare);
            isize iterated_count = 0;
            STABLE_FOR(&stable, it, uint64_t, item) {
                TEST(iterated_count < truth_array.count);
                TEST(truth_array.data[iterated_count].index == it.index); 
                TEST(truth_array.data[iterated_count].value == *item); 
                iterated_count += 1;
            }

            //keys outside the valid range must not be found
            for(isize j = 0; j < 10; j++) {
                isize min = truth_array.count ? truth_array.data[0].index : INT64_MAX;
                isize max = truth_array.count ? truth_array.data[truth_array.count - 1].index : INT64_MIN;

                isize before = random_range(INT64_MIN, min);
                isize after = random_range(max + 1, INT64_MIN);
                TEST(stable_at_or(&stable, before, NULL) == NULL);
                TEST(stable_at_or(&stable, after, NULL) == NULL);
            }

            //some random keys inside the range but not within one of the truth indices must not be found
            for(isize j = 0; j < 10; j++) {
                isize not_found = random_range(0, truth_array.count);
                for(isize k = 0; k < truth_array.count; k++) 
                    if(truth_array.data[k].index == not_found) {
                        not_found = -1;
                        break;
                    }

                if(not_found >= 0) 
                    TEST(stable_at_or(&stable, not_found, NULL) == NULL);
            }
		}

		array_deinit(&truth_array);
        stable_deinit(&stable);
	}
	debug_allocator_deinit(&debug_alloc);
}

INTERNAL void test_stable(f64 max_seconds)
{
	test_stable_stress(max_seconds);
}