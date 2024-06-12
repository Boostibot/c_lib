#pragma once
#include "_test.h"
#include "hash_index.h"

#include <string.h>

//@TODO: test multiplicit keys!

INTERNAL isize u64_array_find(u64_Array array, u64 looking_for)
{
	for(isize i = 0; i < array.size; i++)
	{
		if(array.data[i] == looking_for)
			return i;
	}

	return -1;
}

INTERNAL isize u32_array_find(u32_Array array, u32 looking_for)
{
	for(isize i = 0; i < array.size; i++)
	{
		if(array.data[i] == looking_for)
			return i;
	}

	return -1;
}
INTERNAL void test_hash_index_stress(f64 max_seconds)
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
		REMOVE,
		REHASH,
		RESERVE,

		ACTION_ENUM_COUNT
	} Action;

	i32 probabilities[ACTION_ENUM_COUNT] = {0};
	probabilities[INIT]			= 1;
	probabilities[DEINIT]		= 1;
	probabilities[CLEAR]		= 1;
	probabilities[COPY]		    = 10;
	probabilities[INSERT]		= 240;
	probabilities[REMOVE]		= 60;
	probabilities[REHASH]		= 10;
	probabilities[RESERVE]		= 10;

	enum {
		MAX_ITERS = 10*1000*1000,
		MIN_ITERS = 45, //for debugging
		BACKING = 1000,
		MAX_CAPACITY = 1000*10,

		NON_EXISTANT_KEYS_CHECKS = 0,
	};

	Discrete_Distribution dist = random_discrete_make(probabilities, ACTION_ENUM_COUNT);
	
	//for(isize seed_i = 0; seed_i < 10000; seed_i++)
	{
		//We store everything twice to allow us to test copy operation by coping the current state1 into state2 
		// (or vice versa) and continuing working with the copied data (by swapping the structs)
		u64_Array truth_val_array = {0};
		u64_Array truth_key_array = {0};

		u64_Array other_truth_val_array = {0};
		u64_Array other_truth_key_array = {0};
	
		char backing1[BACKING] = {0};
		char backing2[BACKING] = {0};

		char* backing = backing1;
		Hash_Index table = {0};
		Hash_Index other_table = {0};
	

		Array(Action) history = {0};

		//uint64_t random_seed = random_clock_seed();
		uint64_t random_seed = 0x6b3979953b41cf7d;
		*random_state() = random_state_from_seed(random_seed);

		i32 max_size = 0;
		i32 max_capacity = 0;
		f64 start = clock_s();
		for(isize i = 0; i < MAX_ITERS; i++)
		{
			if(i == -1)
				LOG_DEBUG("test", "here");

			if(clock_s() - start >= max_seconds && i >= MIN_ITERS)
				break;

			Action action = (Action) random_discrete(&dist);
			array_push(&history, action);


			switch(action)
			{
				case INIT: {
					hash_index_deinit(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);

					hash_index_init(&table, allocator_get_default());
					break;
				}

				case DEINIT: {
					hash_index_deinit(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);

					break;
				}

				case INSERT: {
					while(true)
					{
						u64 val = hash_index_escape_value(random_u64());
						u64 key = random_u64();
					
						//if we were extra unlucky and geenrated the same key try again
						//(this is statistically extremely unlikely so we might as well not check this
						// at all)
						if(u64_array_find(truth_key_array, key) != -1)
							continue;

						array_push(&truth_key_array, key);
						array_push(&truth_val_array, val);

						if(i == 20)
						{
							int k = 0; (void) k;
						}

						isize inserted = hash_index_insert(&table, key, val);
						isize found = hash_index_find(table, key);
				
						TEST(table.entries != NULL);
						TEST(inserted == found && "The inserted value must be findable");
						break;
					}

					break;
				}

				case REMOVE: {
					if(truth_val_array.size != 0)
					{
						if(i == 31664)
						{
							int k = 0; (void) k;
						}

						u64 removed_index = (u64) random_range(0, truth_val_array.size);
						u64 last_index = (u64) truth_val_array.size - 1;

						CHECK_BOUNDS((isize) removed_index, truth_key_array.size);
						CHECK_BOUNDS((isize) removed_index, truth_val_array.size);
						u64 key = truth_key_array.data[removed_index];
						u64 val = truth_val_array.data[removed_index];

						swap_any(truth_key_array.data + removed_index, truth_key_array.data + last_index, sizeof truth_key_array.data[0]);
						swap_any(truth_val_array.data + removed_index, truth_val_array.data + last_index, sizeof truth_val_array.data[0]);

						array_pop(&truth_key_array);
						array_pop(&truth_val_array);

						isize found = hash_index_find(table, key);

						TEST(found != -1);
						TEST(table.entries[found].value == val);
						hash_index_remove(&table, found);
				
						isize found_after = hash_index_find(table, key);
						TEST(found_after == -1);
						TEST(found_after == -1);

						TEST(hash_index_is_invariant(table, true));
					}

					break;
				}

				case CLEAR: {
					hash_index_clear(&table);
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);
					break;
				}

				case COPY: {
					//copy
					hash_index_copy(&other_table, table);
					array_copy(&other_truth_val_array, truth_val_array);
					array_copy(&other_truth_key_array, truth_key_array);
				
					//swap all acessors
					swap_any(&truth_val_array, &other_truth_val_array, sizeof truth_val_array);
					swap_any(&truth_key_array, &other_truth_key_array, sizeof truth_key_array);
					swap_any(&table, &other_table, sizeof table);
					if(backing == backing1)
						backing = backing2;
					else
						backing = backing1;
					
					break;
				}

				case REHASH: {
					isize rehash_to = random_range(0, MAX_CAPACITY);
					hash_index_rehash(&table, rehash_to);
					break;
				}

				case RESERVE: {
					isize rehash_to = random_range(0, MAX_CAPACITY);
					hash_index_reserve(&table, rehash_to);
					break;
				}

				case ACTION_ENUM_COUNT:
				default: {
					ASSERT(false && "unreachable");
					break;		
				}
			}

			if(max_size < table.size)
				max_size = table.size;
			if(max_capacity < table.entries_count)
				max_capacity = table.entries_count;

			//Test integrity of all current keys
			//for(isize z = 0; z < 2; z++)
			{
				ASSERT(truth_key_array.size == truth_val_array.size);
				for(isize j = 0; j < truth_key_array.size; j++)
				{
					u64 key = truth_key_array.data[j];
					u64 val = truth_val_array.data[j];
					
					if(i == 31664 && j == 31)
					{
						int k = 0; (void) k;
					}

					isize found = hash_index_find(table, key);
					TEST(table.entries != NULL);
					TEST(0 <= found && found < table.entries_count && "The returned index must be valid");
					Hash_Index_Entry entry = table.entries[found];
				
					TEST(entry.hash == key && entry.value == val && "The entry must be inserted properly");
				}

				
				if(i == 31664)
				{
					int k = 0; (void) k;
				}

				//if(z == 0)
					//hash_index_rehash_in_place(&table);
			}

			//Test integrity of some non existant keys
			for(isize k = 0; k < NON_EXISTANT_KEYS_CHECKS; k++)
			{
				u64 key = random_u64();
				//Only if the genrated key is unique 
				//(again extrenely statistically unlikely that it will fail 1 / 10^19 chance)
				if(u64_array_find(truth_key_array, key) == -1)
				{
					isize found = hash_index_find(table, key);
					TEST(found == -1 && "must not be found");
				}
			}
		}


		array_deinit(&truth_key_array);
		array_deinit(&truth_val_array);
		array_deinit(&other_truth_key_array);
		array_deinit(&other_truth_val_array);
		array_deinit(&history);
		hash_index_deinit(&table);
		hash_index_deinit(&other_table);
	}

	random_discrete_deinit(&dist);
	isize mem_after = allocator_get_stats(allocator_get_default()).bytes_allocated;
	TEST(mem_before == mem_after);
}

#if 0
typedef struct Benchmark_Hash_Index_Context {
    Allocator* arena;
    u64_Array keys;
    u64_Array vals;
    u64_Array lookup;

    f64 percentage_of_non_existant;
    isize load_factor;
    isize size;
    isize capacity;
    isize max_entries;

    Hash_Index index;
    f64 average_probe_length;
    
    isize insert_sum_probe_length;

    isize fifo_num_lookups;
    isize fifo_sum_probe_length;
    isize fifo_sum_item_count;
    isize fifo_sum_removed_count;
    isize fifo_iterations;
    f64 remove_insert_lookup_fraction;
} Benchmark_Hash_Index_Context;

Benchmark_Hash_Index_Context benchmark_hash_index_prepare(Allocator* arena, isize capacity, f64 percentage_of_non_existant, isize load_factor)
{
    isize size = capacity * load_factor/100 - 1;

    //Prepare the keys and values to be inserted to all hashes
    u64_Array keys = {arena};
    u64_Array vals = {arena};
    for(isize i = 0; i < size; i ++)
    {
        array_push(&keys, random_u64());
        array_push(&vals, random_u64());
    }
        
    //Prepare the lookup sequence
    isize non_existant_lookups = (isize) (keys.size * percentage_of_non_existant);
    isize existant_lookups = keys.size - non_existant_lookups;

    u64_Array lookup = {arena};
    array_append(&lookup, keys.data, existant_lookups);

    for(isize i = 0; i < non_existant_lookups; i++)
        array_push(&lookup, random_u64());

    random_shuffle(lookup.data, lookup.size, sizeof(u64));
    
    Benchmark_Hash_Index_Context out = {0};
    out.keys = keys;
    out.vals = vals;
    out.lookup = lookup;
    out.percentage_of_non_existant = percentage_of_non_existant;
    out.load_factor = load_factor;
    out.size = size;
    out.capacity = capacity;

    return out;
}

#define BENCH_HASH_INDEX_LOOKUP_BATCH 256
#define BENCH_HASH_INDEX_REMOVE_BATCH 32
#define BENCH_HASH_INDEX_INSERT_BATCH 32
#define BENCH_HASH_INDEX_FIFO_BATCH 32


bool benchmark_hash_index_fifo_bench(isize iter, void* _context)
{
    Benchmark_Hash_Index_Context* context = (Benchmark_Hash_Index_Context*) _context;
    
    Hash_Index* index = &context->index;
    u64_Array* keys = &context->keys;
    u64_Array* vals = &context->vals;
    isize to_size = keys->size / 2;

    //u64_Array* lookup = &context->lookup;
    if(iter > 0 && index->size + index->gravestone_count + BENCH_HASH_INDEX_FIFO_BATCH <= keys->size)
    {
        for(isize i = 0; i < BENCH_HASH_INDEX_FIFO_BATCH; i++)
        {
            isize it = iter*BENCH_HASH_INDEX_FIFO_BATCH + i;
            isize curr_insert = MOD(it, to_size);
            isize curr_remove = MOD(it - to_size + 1, to_size);

            u64 removed_key = keys->data[curr_remove];
            isize found = hash_index_find(*index, removed_key);
            if(found != -1)
            {
                hash_index_remove(index, found);
                hash_index_insert(index, keys->data[curr_insert], keys->data[curr_insert]);
            }
            else
                ASSERT(false);

            //perf_do_not_optimize(&found);

            for(isize k = 0; k < context->fifo_num_lookups; k++)
            {
                isize curr_lookup = MOD(it - k, to_size);
                u64 key = keys->data[curr_lookup];
                isize found2 = hash_index_find(*index, key);
                perf_do_not_optimize(&found2);
            }

            ASSERT(index->entries_count <= context->capacity);
            context->fifo_sum_probe_length += hash_index_get_hash_collision_count(*index);
            context->fifo_sum_item_count += index->size;
            context->fifo_sum_removed_count += index->gravestone_count;
            context->fifo_iterations += 1;
        }


        return true;
    }
    else
    {
        if(iter == 0)
        {
            context->fifo_sum_probe_length = 0;
            context->fifo_sum_item_count = 0;
            context->fifo_sum_removed_count = 0;
            context->fifo_iterations = 0;
        }

        hash_index_deinit(index);
        arena_frame_release(&context->arena);
        context->arena = scratch_arena_acquire();

        hash_index_init_load_factor(index, context->arena, context->load_factor, context->load_factor);
        hash_index_reserve(index, keys->size);

        isize before = index->entries_count;
        for(isize i = 0; i < to_size; i++)
            hash_index_insert(index, keys->data[i], vals->data[i]);
            
        context->average_probe_length = (f64) hash_index_get_hash_collision_count(*index) / index->size;
        ASSERT(before == index->entries_count);
        return false;

        return false;
    }
}
INTERNAL void benchmark_hash_index()
{
	isize sizes[] = {7, 10, 13, 16, 19, 22, 25};  //in powers of two from 128 to 33554432 
    i32 load_factors[] = {70}; //You can try other values. the reuslts are similar
    f64 non_existances[] = {0.3}; //This doesnt bring us mutch info. Even for robin hood it doesnt really change much
    f64 warmup = 0.3;
    f64 time = 1;
    
    const char* name = "LINEAR";
    //I am lazy so to get stats for different Hash_Indeces you have to change the implementation.
    //Its a lot easier in the short term than having 5 different versions of the same file flying about
    for(isize j = 0; j < STATIC_ARRAY_SIZE(load_factors); j++)
    {
        for(isize non_existance_i = 0; non_existance_i < STATIC_ARRAY_SIZE(non_existances); non_existance_i++)
        {
			#if 0
            printf("%s lookup \n", name);
            for(isize k = 0; k < STATIC_ARRAY_SIZE(sizes); k++)
            {
                Allocator* arena = allocator_acquire_arena();
                Benchmark_Hash_Index_Context context = benchmark_hash_index_prepare(arena, (isize) 1 << sizes[k], non_existances[non_existance_i], load_factors[j]);
                Perf_Stats stats = perf_benchmark(warmup, time, BENCH_HASH_INDEX_LOOKUP_BATCH, benchmark_hash_index_lookup_bench, &context);
                print_bench_stats(stats, context);
                allocator_release_arena(arena);
            }

            printf("\n");
            printf("%s dirty lookup \n", name);
            for(isize k = 0; k < STATIC_ARRAY_SIZE(sizes); k++)
            {
                Allocator* arena = allocator_acquire_arena();
                Benchmark_Hash_Index_Context context = benchmark_hash_index_prepare(arena, (isize) 1 << sizes[k], non_existances[non_existance_i], load_factors[j]);
                Perf_Stats stats = perf_benchmark(warmup, time, BENCH_HASH_INDEX_LOOKUP_BATCH, benchmark_hash_index_dirty_lookup_bench, &context);
                print_bench_stats(stats, context);
                allocator_release_arena(arena);
            }

            printf("\n");
            printf("%s insert \n", name);
            for(isize k = 0; k < STATIC_ARRAY_SIZE(sizes); k++)
            {
                Allocator* arena = allocator_acquire_arena();
                Benchmark_Hash_Index_Context context = benchmark_hash_index_prepare(arena, (isize) 1 << sizes[k], non_existances[non_existance_i], load_factors[j]);
                Perf_Stats stats = perf_benchmark(warmup, time, BENCH_HASH_INDEX_INSERT_BATCH, benchmark_hash_index_insert_bench, &context);
                print_bench_stats(stats, context);
                allocator_release_arena(arena);
            }

            printf("\n");
            printf("%s remove \n", name);
            for(isize k = 0; k < STATIC_ARRAY_SIZE(sizes); k++)
            {
                Allocator* arena = allocator_acquire_arena();
                Benchmark_Hash_Index_Context context = benchmark_hash_index_prepare(arena, (isize) 1 << sizes[k], non_existances[non_existance_i], load_factors[j]);
                Perf_Stats stats = perf_benchmark(warmup, time, BENCH_HASH_INDEX_REMOVE_BATCH, benchmark_hash_index_remove_bench, &context);
                print_bench_stats(stats, context);
                allocator_release_arena(arena);
            }
			#endif

            printf("\n");
            printf("%s fifo \n", name);
            for(isize k = 0; k < STATIC_ARRAY_SIZE(sizes); k++)
            {
                Allocator* arena = allocator_acquire_arena();
                Benchmark_Hash_Index_Context context = benchmark_hash_index_prepare(arena, (isize) 1 << sizes[k], non_existances[non_existance_i], load_factors[j]);
                Perf_Stats stats = perf_benchmark(warmup, time, BENCH_HASH_INDEX_FIFO_BATCH, benchmark_hash_index_fifo_bench, &context);
                print_fifo_stats(stats, context);
                allocator_release_arena(arena);
            }
            
            printf("\n");
            printf("%s fifo + 32 lookups \n", name);
            for(isize k = 0; k < STATIC_ARRAY_SIZE(sizes); k++)
            {
                Allocator* arena = allocator_acquire_arena();
                Benchmark_Hash_Index_Context context = benchmark_hash_index_prepare(arena, (isize) 1 << sizes[k], non_existances[non_existance_i], load_factors[j]);
                context.fifo_num_lookups = 32;
                Perf_Stats stats = perf_benchmark(warmup, time, BENCH_HASH_INDEX_FIFO_BATCH, benchmark_hash_index_fifo_bench, &context);
                print_fifo_stats(stats, context);
                allocator_release_arena(arena);
            }
        }
    }
}
#endif

INTERNAL void test_hash_index(f64 max_seconds)
{
	test_hash_index_stress(max_seconds/2);
}
