#pragma once
#include "_test.h"
#include "hash.h"

#include <string.h>

int u64_comp_func(const void* a_, const void* b_)
{
	u64 a = *(u64*) a_;
	u64 b = *(u64*) b_;
	return (a < b) - (a > b);
}

INTERNAL u64 random_hash_value()
{
	while(true)
	{
		u64 val = random_u64();
		if(hash_is_valid_value(val))
			return val;
	}
}

INTERNAL void test_hash_stress(f64 max_seconds)
{
	//max_seconds = 0;
	isize mem_before = allocator_get_stats(allocator_get_default()).bytes_allocated;

	typedef enum {
		INIT,
		DEINIT,
		CLEAR,
		COPY,
		//FIND, //We test find after every iteration
		INSERT,
		INSERT_DUPLICIT,
		REMOVE,
		REHASH,

		ACTION_ENUM_COUNT
	} Action;

	i32 probabilities[ACTION_ENUM_COUNT] = {0};
	probabilities[INIT]			= 1;
	probabilities[DEINIT]		= 1;
	probabilities[CLEAR]		= 1;
	probabilities[COPY]		    = 10;
	probabilities[INSERT]		= 240;
	probabilities[INSERT_DUPLICIT] = 100;
	probabilities[REMOVE]		= 60;
	probabilities[REHASH]		= 10;

	enum {
		MAX_ITERS = 10*1000*1000,
		MIN_ITERS = 50, //for debugging

		//After each iteration gets EXISTANT_KEYS_CHECKS keys from the truth arrays
		// and find them in the hash. Checks whether they were find and the stored values match.
		//EXISTANT_KEYS_CHECKS = 10,
		
		//After each iteration generates NON_EXISTANT_KEYS_CHECKS keys not found in the truth array
		// and checks they cannot be found in the hash. These checks are not very necessary but alas we perform 
		// a few.
		NON_EXISTANT_KEYS_CHECKS = 2,
	};

	Discrete_Distribution dist = random_discrete_make(probabilities, ACTION_ENUM_COUNT);
	{
		//We store everything twice to allow us to test copy operation by coping the current state1 into state2 
		// (or vice versa) and continuing working with the copied data (by swapping the structs)
		u64_Array truth_val_array = {0};
		u64_Array truth_key_array = {0};

		u64_Array other_truth_val_array = {0};
		u64_Array other_truth_key_array = {0};
	
		Hash table = {0};
		Hash other_table = {0};

		Array(Action) history = {0};

		//uint64_t random_seed = random_clock_seed();
		//uint64_t random_seed = 0x6b3979953b41cf7d;
		//*random_state() = random_state_from_seed(random_seed);

		i32 max_size = 0;
		i32 max_capacity = 0;
		f64 start = clock_s();
		for(isize i = 0; i < MAX_ITERS; i++)
		{
			if(clock_s() - start >= max_seconds && i >= MIN_ITERS)
				break;

			Action action = (Action) random_discrete(&dist);
			array_push(&history, action);

			switch(action)
			{
				case INIT: {
					hash_deinit(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);

					hash_init(&table, allocator_get_default());
					
				} break;

				case DEINIT: {
					hash_deinit(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);
				} break;

				case INSERT: {
					u64 val = random_hash_value();
					u64 key = random_u64();

					array_push(&truth_key_array, key);
					array_push(&truth_val_array, val);

					isize inserted = hash_insert(&table, key, val).index;
					isize found = hash_find(table, key).index;
				
					TEST(table.entries != NULL);
					TEST(found != -1 && inserted == found && "The inserted value must be findable");
				} break;

				case INSERT_DUPLICIT: {
					if(truth_key_array.len > 0)
					{
						u64 val = random_hash_value();
						u64 key = truth_key_array.data[random_range(0, truth_key_array.len)];

						array_push(&truth_key_array, key);
						array_push(&truth_val_array, val);

						isize inserted = hash_insert(&table, key, val).index;
						TEST(inserted != -1);
						TEST(table.entries != NULL);
					}
				} break;

				case REMOVE: {
					if(truth_val_array.len > 0)
					{
						u64 removed_key = truth_key_array.data[random_range(0, truth_key_array.len)];
						i32 removed_truth_count = 0;
						for(isize j = 0; j < truth_key_array.len; j++)
							if(truth_key_array.data[j] == removed_key)
							{
								SWAP(&truth_key_array.data[j], array_last(truth_key_array), u64);
								SWAP(&truth_val_array.data[j], array_last(truth_val_array), u64);
								array_pop(&truth_key_array);
								array_pop(&truth_val_array);
								j -= 1;
								removed_truth_count += 1;
							}

						i32 removed_hash_count = hash_remove_all(&table, removed_key);
						TEST(removed_truth_count == removed_hash_count);

						isize found_after = hash_find(table, removed_key).index;
						TEST(found_after == -1);
					}
				} break;

				case CLEAR: {
					hash_clear(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);
				} break;

				case COPY: {
					//copy
					hash_copy(&other_table, table);
					array_copy(&other_truth_val_array, truth_val_array);
					array_copy(&other_truth_key_array, truth_key_array);
				
					//swap all acessors
					swap_any(&truth_val_array, &other_truth_val_array, sizeof truth_val_array);
					swap_any(&truth_key_array, &other_truth_key_array, sizeof truth_key_array);
					swap_any(&table, &other_table, sizeof table);
				} break;

				case REHASH: {
					hash_rehash(&table, table.len);
				} break;

				case ACTION_ENUM_COUNT:
				default: {
					UNREACHABLE();
				} break;
			}

			if(max_size < table.len)
				max_size = table.len;
			if(max_capacity < table.entries_count)
				max_capacity = table.entries_count;
				
			TEST(hash_is_invariant(table, true));
			{
				ASSERT(truth_key_array.len == truth_val_array.len);
				TEST(truth_key_array.len == table.len);

				for(isize k = 0; k < truth_key_array.len; k++)
				{
					u64 key = truth_key_array.data[k];
					SCRATCH_ARENA(arena)
					{
						u64_Array truth_found = {0};
						u64_Array hash_found = {0};
						array_init_with_capacity(&truth_found, arena.alloc, 8);
						array_init_with_capacity(&hash_found, arena.alloc, 8);
						
						for(isize j = 0; j < truth_key_array.len; j++)
							if(truth_key_array.data[j] == key)
								array_push(&truth_found, truth_val_array.data[j]);

						for(Hash_Found found = hash_find(table, key); found.index != -1; found = hash_find_next(table, found))
							array_push(&hash_found, found.value);
							
						TEST(hash_found.len == truth_found.len);

						if(hash_found.len > 1)
						{
							qsort(hash_found.data, hash_found.len, sizeof *hash_found.data, u64_comp_func);
							qsort(truth_found.data, truth_found.len, sizeof *truth_found.data, u64_comp_func);
						}

						for(isize l = 0; l < hash_found.len; l++)
							TEST(hash_found.data[l] == truth_found.data[l]);
					}
				}
			}

			//Test integrity of some non existant keys
			for(isize k = 0; k < NON_EXISTANT_KEYS_CHECKS; k++)
			{
				u64 key = random_u64();
				
				//Only if the genrated key is unique 
				//(again extrenely statistically unlikely that it will fail truth_key_array.len/10^19 chance)
				bool key_found = false;
				for(isize j = 0; j < truth_key_array.len; j++)
					if(truth_key_array.data[j] == key)
					{
						key_found = true;
						break;
					}

				if(key_found == false)
				{
					isize found = hash_find(table, key).index;
					TEST(found == -1 && "must not be found");
				}
			}
		}

		array_deinit(&truth_key_array);
		array_deinit(&truth_val_array);
		array_deinit(&other_truth_key_array);
		array_deinit(&other_truth_val_array);
		array_deinit(&history);
		hash_deinit(&table);
		hash_deinit(&other_table);
	}

	random_discrete_deinit(&dist);
	isize mem_after = allocator_get_stats(allocator_get_default()).bytes_allocated;
	TEST(mem_before == mem_after);
}

INTERNAL void test_hash(f64 max_seconds)
{
	test_hash_stress(max_seconds/2);
}
