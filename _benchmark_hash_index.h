#pragma once

#include "_test.h"
#include "array.h"
#include "perf.h"

#include "hash_index.h"
#include "profile_utils.h"

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

bool benchmark_hash_index_lookup_bench(isize iter, void* _context)
{
    Benchmark_Hash_Index_Context* context = (Benchmark_Hash_Index_Context*) _context;
    
    Hash_Index* index = &context->index;
    u64_Array* keys = &context->keys;
    u64_Array* vals = &context->vals;
    u64_Array* lookup = &context->lookup;
    if(iter != 0)
    {
        ASSERT(lookup->size > 0);
        for(isize i = 0; i < BENCH_HASH_INDEX_LOOKUP_BATCH; i++)
        {
            isize curr_read = (iter*BENCH_HASH_INDEX_LOOKUP_BATCH + i) % lookup->size;
            u64 key = lookup->data[curr_read];

            isize found = hash_index_find(*index, key);
            perf_do_not_optimize(&found);
        }

        return true;
    }
    else
    {
        hash_index_deinit(index);
        allocator_release_arena(context->arena);
        context->arena = allocator_acquire_arena();
        
        hash_index_init_load_factor(index, context->arena, context->load_factor, 0);
        hash_index_reserve(index, keys->size);
        
        isize before = index->entries_count;
        for(isize i = 0; i < keys->size; i++)
            hash_index_insert(index, keys->data[i], vals->data[i]);
        
        ASSERT(before == index->entries_count);
        context->average_probe_length = (f64) index->hash_collisions / index->size;
        return false;
    }
}

bool benchmark_hash_index_dirty_lookup_bench(isize iter, void* _context)
{
    Benchmark_Hash_Index_Context* context = (Benchmark_Hash_Index_Context*) _context;
    
    Hash_Index* index = &context->index;
    u64_Array* keys = &context->keys;
    u64_Array* vals = &context->vals;
    u64_Array* lookup = &context->lookup;
    if(iter > 0)
    {
        ASSERT(lookup->size > 0);
        for(isize i = 0; i < BENCH_HASH_INDEX_LOOKUP_BATCH; i++)
        {
            isize curr_read = (iter*BENCH_HASH_INDEX_LOOKUP_BATCH + i) % context->max_entries;
            u64 key = lookup->data[curr_read];

            isize found = hash_index_find(*index, key);
            perf_do_not_optimize(&found);
        }

        return true;
    }
    else
    {
        hash_index_deinit(index);
        allocator_release_arena(context->arena);
        context->arena = allocator_acquire_arena();
        
        hash_index_init_load_factor(index, context->arena, context->load_factor, 0);

        enum {DIRTY_COUNT = 32, ADD_COUNT = 32};
        
        isize max_entries = keys->size - DIRTY_COUNT;
        if(max_entries <= 0)
            max_entries = keys->size;

        hash_index_reserve(index, max_entries);
        isize before = index->entries_count;
        for(isize i = 0; i < max_entries;)
        {
            u64 dirty[DIRTY_COUNT] = {0};
            random_bytes(dirty, sizeof(dirty));

            for(isize k = 0; k < DIRTY_COUNT; k++)
                hash_index_insert(index, dirty[k], dirty[k]);

            for(isize k = 0; k < ADD_COUNT && i < max_entries; k++, i++)
                hash_index_insert(index, keys->data[i], vals->data[i]);
                
            for(isize k = 0; k < DIRTY_COUNT; k++)
            {
                isize found = hash_index_find(*index, dirty[k]);
                hash_index_remove(index, found);
            }
        }
        
        ASSERT(before == index->entries_count);
        context->average_probe_length = (f64) index->hash_collisions / index->size;
        context->max_entries = max_entries;
        return false;
    }
}

bool benchmark_hash_index_remove_bench(isize iter, void* _context)
{
    Benchmark_Hash_Index_Context* context = (Benchmark_Hash_Index_Context*) _context;
    
    Hash_Index* index = &context->index;
    u64_Array* keys = &context->keys;
    u64_Array* vals = &context->vals;
    //u64_Array* lookup = &context->lookup;
    if(index->size > BENCH_HASH_INDEX_REMOVE_BATCH && iter > 0)
    {
        for(isize i = 0; i < BENCH_HASH_INDEX_REMOVE_BATCH; i++)
        {
            isize curr_read = (iter*BENCH_HASH_INDEX_LOOKUP_BATCH + i) % keys->size;
            u64 key = keys->data[curr_read];
            isize found = hash_index_find(*index, key);
            hash_index_remove(index, found);
            //perf_do_not_optimize(&found);
        }
        return true;
    }
    else
    {
        
        hash_index_deinit(index);
        allocator_release_arena(context->arena);
        context->arena = allocator_acquire_arena();

        hash_index_init_load_factor(index, context->arena, context->load_factor, 0);
        hash_index_reserve(index, keys->size);

        isize before = index->entries_count;
        for(isize i = 0; i < keys->size; i++)
            hash_index_insert(index, keys->data[i], vals->data[i]);
            
        context->average_probe_length = (f64) index->hash_collisions / index->size;
        ASSERT(before == index->entries_count);
        return false;

    }
}


bool benchmark_hash_index_insert_bench(isize iter, void* _context)
{
    Benchmark_Hash_Index_Context* context = (Benchmark_Hash_Index_Context*) _context;
    
    Hash_Index* index = &context->index;
    u64_Array* keys = &context->keys;
    u64_Array* vals = &context->vals;
    //u64_Array* lookup = &context->lookup;
    if(index->size + BENCH_HASH_INDEX_REMOVE_BATCH <= keys->size && iter > 0)
    {
        for(isize i = 0; i < BENCH_HASH_INDEX_REMOVE_BATCH; i++)
            hash_index_insert(index, keys->data[index->size], vals->data[index->size]);
        
        return true;
    }
    else
    {
        if(iter != 0)
            context->average_probe_length = (f64) index->hash_collisions / index->size;

        hash_index_deinit(index);
        allocator_release_arena(context->arena);
        context->arena = allocator_acquire_arena();

        hash_index_init_load_factor(index, context->arena, context->load_factor, 0);
        hash_index_reserve(index, keys->size);

        return false;
    }
}

bool benchmark_hash_index_fifo_bench(isize iter, void* _context)
{
    Benchmark_Hash_Index_Context* context = (Benchmark_Hash_Index_Context*) _context;
    
    Hash_Index* index = &context->index;
    u64_Array* keys = &context->keys;
    u64_Array* vals = &context->vals;
    isize to_size = keys->size / 2;

    //u64_Array* lookup = &context->lookup;
    if(iter > 0 && index->size + index->entries_removed + BENCH_HASH_INDEX_FIFO_BATCH <= keys->size)
    {
        for(isize i = 0; i < BENCH_HASH_INDEX_FIFO_BATCH; i++)
        {
            isize it = iter*BENCH_HASH_INDEX_FIFO_BATCH + i;
            isize curr_insert = MODULO(it, to_size);
            isize curr_remove = MODULO(it - to_size + 1, to_size);

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
                isize curr_lookup = MODULO(it - k, to_size);
                u64 key = keys->data[curr_lookup];
                isize found2 = hash_index_find(*index, key);
                perf_do_not_optimize(&found2);
            }

            ASSERT(index->entries_count <= context->capacity);
            context->fifo_sum_probe_length += index->hash_collisions;
            context->fifo_sum_item_count += index->size;
            context->fifo_sum_removed_count += index->entries_removed;
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
        allocator_release_arena(context->arena);
        context->arena = allocator_acquire_arena();

        hash_index_init_load_factor(index, context->arena, context->load_factor, context->load_factor);
        hash_index_reserve(index, keys->size);

        isize before = index->entries_count;
        for(isize i = 0; i < to_size; i++)
            hash_index_insert(index, keys->data[i], vals->data[i]);
            
        context->average_probe_length = (f64) index->hash_collisions / index->size;
        ASSERT(before == index->entries_count);
        return false;

        return false;
    }
}

void print_bench_stats(Perf_Stats stats, Benchmark_Hash_Index_Context context)
{
    printf("%7.3lf (%6.3lf) size %12lli capacity %12lli load %lli%%\n", stats.average_s * 1000*1000*1000, context.average_probe_length, context.size, context.capacity, context.load_factor);
}

void print_fifo_stats(Perf_Stats stats, Benchmark_Hash_Index_Context context)
{
    f64 avg_size = (f64) context.fifo_sum_item_count / stats.runs;
    f64 avg_removed = (f64) context.fifo_sum_removed_count / stats.runs / avg_size;
    f64 avg_probe = (f64) context.fifo_sum_probe_length / stats.runs / avg_size;
    printf("%7.3lf (%6.3lf - %6.3lf) size %14.2lf capacity %12lli load %lli%%\n", stats.average_s * 1000*1000*1000, avg_probe, avg_removed, avg_size, context.capacity, context.load_factor);
}

void benchmark_hash_index_lookup()
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

/*
The results: 
We refer with: 
- hood to the Robin Hood with backshifting hash index. 
- qudratic refers to quadrtic probing simple hash index
- linear refers to linear probing simple hash index
- double refers to double hashing probing simple hash index (where `step = (hash >> 58) | 1`)

The value in brackets is average probe length (APL) per entry. That is it takes APL iterations in the find loop to find the entry we were looking for.
We dont preocupy ourselves with the max probe distance (which can be WAY higher sometimes even 13 for linear probing) because we are not in hard realtime
environment and we expect the programmer to hash data using a good hash function. 

//FINAL VERDICT: QUADRATIC
//Quadratic speeds are similar to LINEAR but it has dramtically lower
// APL thus making it more resiliant to hash collisions. Compared to
// HOOD it has about twice as fast insertions and better APL. The only
// place where it really looses are dirty lookups.
// The fifo behaviour is similar to HOOD but better when lookups are involved.
//
// We choose it because generally we dont have a place for dirty lookups and
// thus its the fastest approach. If pure dirty lookup scenerio arises we can 
// always rehash before doing the lookups or set load factor really low for removals.
//
//The typical workloads are:
// 1) Static and lookups only
// 2) Insertions only (used for deduplication)
// 3) Insertions + lookups + clear (for temporaries)
// 4) FIFO like for caching. We use this in allocator_debug to map adresses.

LINEAR lookup
  3.634 ( 2.205) size           88 capacity          128 load 70%
  3.422 ( 1.385) size          715 capacity         1024 load 70%
 12.005 ( 1.133) size         5733 capacity         8192 load 70%
 16.492 ( 1.166) size        45874 capacity        65536 load 70%
 19.679 ( 1.183) size       367000 capacity       524288 load 70%
 36.359 ( 1.166) size      2936011 capacity      4194304 load 70%
 44.877 ( 1.166) size     23488101 capacity     33554432 load 70%
 
DOUBLE lookup
  3.009 ( 0.682) size           88 capacity          128 load 70%
  2.854 ( 0.765) size          715 capacity         1024 load 70%
  7.043 ( 0.747) size         5733 capacity         8192 load 70%
 16.394 ( 0.743) size        45874 capacity        65536 load 70%
 23.341 ( 0.729) size       367000 capacity       524288 load 70%
 51.692 ( 0.732) size      2936011 capacity      4194304 load 70%
 65.821 ( 0.731) size     23488101 capacity     33554432 load 70%
 
QUADRATIC lookup
  2.966 ( 0.716) size           88 capacity          128 load 70%
  3.057 ( 0.951) size          715 capacity         1024 load 70%
 11.252 ( 0.831) size         5733 capacity         8192 load 70%
 16.114 ( 0.832) size        45874 capacity        65536 load 70%
 19.314 ( 0.838) size       367000 capacity       524288 load 70%
 37.074 ( 0.839) size      2936011 capacity      4194304 load 70%
 43.940 ( 0.839) size     23488101 capacity     33554432 load 70%
 
HOOD lookup
  3.329 ( 1.159) size           88 capacity          128 load 70%
  2.811 ( 1.189) size          715 capacity         1024 load 70%
  9.348 ( 1.215) size         5733 capacity         8192 load 70%
 19.335 ( 1.211) size        45874 capacity        65536 load 70%
 24.304 ( 1.162) size       367000 capacity       524288 load 70%
 41.016 ( 1.164) size      2936011 capacity      4194304 load 70%
 46.938 ( 1.167) size     23488101 capacity     33554432 load 70%

 //Overall all double hashing is performing better for small to medium sizes but dramatically worse
 // for giant ones.
 //  It achieves dramatically APL compared to linear or hood.
 //Quadratic has very similar but slightly better performace to linear. 
 //  It achives nearly as good APL as double hashing.
 //Hood has also very similar performance to linear. 
 //  The APL is and always will be the same as linear because the
 //  robin hood algorhitm only exchanges probe lengths thus it
 //  keeps the total (and thus average) the same.


LINEAR dirty lookup
  4.275 ( 1.375) size           88 capacity          128 load 70%
  4.884 ( 2.291) size          715 capacity         1024 load 70%
 12.368 ( 2.221) size         5733 capacity         8192 load 70%
 17.841 ( 2.312) size        45874 capacity        65536 load 70%
 22.605 ( 2.303) size       367000 capacity       524288 load 70%
 48.613 ( 2.326) size      2936011 capacity      4194304 load 70%
 62.453 ( 2.333) size     23488101 capacity     33554432 load 70%
 
DOUBLE dirty lookup
  3.623 ( 1.107) size           88 capacity          128 load 70%
  4.134 ( 1.455) size          715 capacity         1024 load 70%
 14.023 ( 1.483) size         5733 capacity         8192 load 70%
 19.490 ( 1.481) size        45874 capacity        65536 load 70%
 29.679 ( 1.463) size       367000 capacity       524288 load 70%
 72.190 ( 1.463) size      2936011 capacity      4194304 load 70%
 90.980 ( 1.463) size     23488101 capacity     33554432 load 70%
 
QUADRATIC dirty lookup
  4.056 ( 1.661) size           88 capacity          128 load 70%
  4.130 ( 1.568) size          715 capacity         1024 load 70%
 11.994 ( 1.655) size         5733 capacity         8192 load 70%
 18.084 ( 1.656) size        45874 capacity        65536 load 70%
 25.999 ( 1.674) size       367000 capacity       524288 load 70%
 58.733 ( 1.674) size      2936011 capacity      4194304 load 70%
 73.214 ( 1.676) size     23488101 capacity     33554432 load 70%
 
HOOD dirty lookup
  2.610 ( 2.554) size           88 capacity          128 load 70%
  2.997 ( 2.360) size          715 capacity         1024 load 70%
  9.724 ( 2.379) size         5733 capacity         8192 load 70%
 20.542 ( 2.385) size        45874 capacity        65536 load 70%
 23.749 ( 2.323) size       367000 capacity       524288 load 70%
 40.016 ( 2.339) size      2936011 capacity      4194304 load 70%
 46.716 ( 2.333) size     23488101 capacity     33554432 load 70%

//For dirty lookup all stategies perform equaly badly only hood stays 
// constant because of back shifting. The APL for linear is really bad.
// The APL for double and quadratic are better however since we are doing
// more than one jump on average they result in frequent cache misses. This
// is especially prominent with double hashing.

//If we care only for fast lookups HOOD is a clear winner so far.

LINEAR insert
  4.413 ( 0.328) size           88 capacity          128 load 70%
  3.708 ( 0.955) size          715 capacity         1024 load 70%
  3.922 ( 1.115) size         5733 capacity         8192 load 70%
 10.689 ( 1.209) size        45874 capacity        65536 load 70%
 15.046 ( 1.168) size       367000 capacity       524288 load 70%
 31.188 ( 1.173) size      2936011 capacity      4194304 load 70%
 47.561 ( 1.166) size     23488101 capacity     33554432 load 70%
 
DOUBLE insert
  3.587 ( 0.328) size           88 capacity          128 load 70%
  3.690 ( 0.663) size          715 capacity         1024 load 70%
  3.934 ( 0.733) size         5733 capacity         8192 load 70%
 10.514 ( 0.731) size        45874 capacity        65536 load 70%
 15.662 ( 0.733) size       367000 capacity       524288 load 70%
 38.315 ( 0.731) size      2936011 capacity      4194304 load 70%
 54.488 ( 0.000) size     23488101 capacity     33554432 load 70%

QUADRATIC insert
  3.872 ( 0.188) size           88 capacity          128 load 70%
  3.548 ( 0.773) size          715 capacity         1024 load 70%
  3.785 ( 0.816) size         5733 capacity         8192 load 70%
 10.397 ( 0.838) size        45874 capacity        65536 load 70%
 14.860 ( 0.835) size       367000 capacity       524288 load 70%
 31.121 ( 0.838) size      2936011 capacity      4194304 load 70%
 40.417 ( 0.838) size     23488101 capacity     33554432 load 70%
 
HOOD insert
  7.000 ( 0.422) size           88 capacity          128 load 70%
  7.397 ( 0.980) size          715 capacity         1024 load 70%
  8.494 ( 1.270) size         5733 capacity         8192 load 70%
 14.253 ( 1.168) size        45874 capacity        65536 load 70%
 20.385 ( 1.148) size       367000 capacity       524288 load 70%
 33.721 ( 1.166) size      2936011 capacity      4194304 load 70%
 69.375 ( 0.000) size     23488101 capacity     33554432 load 70%

//Insertions are really interesting. Here the the APL refers to the APL on the filled 
// hash index. HOOD performs badly because of the complexity of the 
// insertion algorhitm. DOUBLE performs poorly for extremely large sizes 
// probably because of the same reasons as before. QUADRATIC seems to be
// the best contender when it comes to insertions.

LINEAR remove
  6.908 ( 1.614) size           88 capacity          128 load 70%
 12.244 ( 1.383) size          715 capacity         1024 load 70%
 17.219 ( 1.088) size         5733 capacity         8192 load 70%
 19.095 ( 1.187) size        45874 capacity        65536 load 70%
 24.389 ( 1.174) size       367000 capacity       524288 load 70%
 49.912 ( 1.170) size      2936011 capacity      4194304 load 70%
 65.435 ( 1.167) size     23488101 capacity     33554432 load 70%
 
DOUBLE remove
  4.076 ( 0.591) size           88 capacity          128 load 70%
 10.546 ( 0.701) size          715 capacity         1024 load 70%
 16.633 ( 0.701) size         5733 capacity         8192 load 70%
 18.608 ( 0.744) size        45874 capacity        65536 load 70%
 25.224 ( 0.731) size       367000 capacity       524288 load 70%
 55.972 ( 0.731) size      2936011 capacity      4194304 load 70%
 61.270 ( 0.731) size     23488101 capacity     33554432 load 70%
 
QUADRATIC remove
  4.534 ( 1.057) size           88 capacity          128 load 70%
 10.228 ( 0.834) size          715 capacity         1024 load 70%
 16.592 ( 0.814) size         5733 capacity         8192 load 70%
 18.406 ( 0.844) size        45874 capacity        65536 load 70%
 23.550 ( 0.844) size       367000 capacity       524288 load 70%
 48.657 ( 0.837) size      2936011 capacity      4194304 load 70%
 63.517 ( 0.837) size     23488101 capacity     33554432 load 70%
 
HOOD remove
  4.070 ( 0.909) size           88 capacity          128 load 70%
 16.868 ( 1.446) size          715 capacity         1024 load 70%
 18.236 ( 1.168) size         5733 capacity         8192 load 70%
 19.596 ( 1.152) size        45874 capacity        65536 load 70%
 25.484 ( 1.173) size       367000 capacity       524288 load 70%
 43.183 ( 1.170) size      2936011 capacity      4194304 load 70%
 62.980 ( 1.167) size     23488101 capacity     33554432 load 70%

 //Removal is lookup + the cost for removal. Surprisingly HOOD is 
 // proably doing okay even whith the backshifting. The rest are 
 // fairly similar
 
LINEAR fifo
 11.837 ( 0.547 -  0.375) size          62.01 capacity          128 load 70%
 11.826 ( 0.471 -  0.494) size         509.78 capacity         1024 load 70%
 13.662 ( 0.391 -  0.497) size        4097.53 capacity         8192 load 70%
 16.109 ( 0.419 -  0.499) size       32752.46 capacity        65536 load 70%
 19.138 ( 0.355 -  0.500) size      261940.07 capacity       524288 load 70%
 36.724 ( 0.348 -  0.494) size     2107621.34 capacity      4194304 load 70%
 43.706 ( 0.346 -  0.494) size    17225494.49 capacity     33554432 load 70%

DOUBLE fifo
 11.776 ( 0.469 -  0.375) size          62.26 capacity          128 load 70%
 12.264 ( 0.385 -  0.494) size         510.46 capacity         1024 load 70%
 13.822 ( 0.332 -  0.497) size        4095.46 capacity         8192 load 70%
 16.083 ( 0.359 -  0.499) size       32768.40 capacity        65536 load 70%
 19.330 ( 0.310 -  0.500) size      261653.45 capacity       524288 load 70%
 43.702 ( 0.302 -  0.493) size     2092133.38 capacity      4194304 load 70%
 52.932 ( 0.286 -  0.431) size    17398456.42 capacity     33554432 load 70%

QUADRATIC fifo
 12.045 ( 0.246 -  0.375) size          61.82 capacity          128 load 70%
 12.161 ( 0.403 -  0.494) size         509.43 capacity         1024 load 70%
 13.913 ( 0.396 -  0.497) size        4101.48 capacity         8192 load 70%
 16.130 ( 0.382 -  0.499) size       32717.53 capacity        65536 load 70%
 18.840 ( 0.328 -  0.500) size      261353.28 capacity       524288 load 70%
 38.907 ( 0.328 -  0.494) size     2102745.30 capacity      4194304 load 70%
 46.169 ( 0.319 -  0.470) size    17234369.53 capacity     33554432 load 70%

HOOD fifo
 11.820 (211800.913 -  0.000) size      63.79 capacity          128 load 70%
 11.585 (58582.484 -  0.000) size      506.86 capacity         1024 load 70%
 22.148 (5630.984 -  0.000) size      4105.55 capacity         8192 load 70%
 31.916 (441.629 -  0.000) size      32752.80 capacity        65536 load 70%
 39.396 (47.485 -  0.000) size      262014.48 capacity       524288 load 70%
 60.450 ( 4.064 -  0.000) size     2097930.47 capacity      4194304 load 70%
 68.679 ( 0.691 -  0.000) size    16828986.79 capacity     33554432 load 70%

//Fifo is the time it takes to insert one and remove one entry from the hash.
//The second number in brackest is the average number of removed entries per
// alive entry. It mostly depends on crossing the rehash boundary and thus is
// merily a reflection of the APL.

//LINEAR Seems to be doing best followed by quadrtayic. Hood is very bad for lage sizes

LINEAR fifo + 32 lookups
178.679 ( 0.421 -  0.375) size          62.74 capacity          128 load 70%
185.429 ( 0.399 -  0.494) size         509.85 capacity         1024 load 70%
236.342 ( 0.402 -  0.497) size        4098.13 capacity         8192 load 70%
245.360 ( 0.351 -  0.499) size       32851.72 capacity        65536 load 70%
249.992 ( 0.348 -  0.495) size      261596.16 capacity       524288 load 70%
282.931 ( 0.338 -  0.450) size     2124215.70 capacity      4194304 load 70%
277.871 ( 0.276 -  0.158) size    17311074.55 capacity     33554432 load 70%

DOUBLE fifo + 32 lookups
178.844 ( 0.530 -  0.375) size          62.88 capacity          128 load 70%
201.801 ( 0.389 -  0.494) size         509.28 capacity         1024 load 70%
238.990 ( 0.356 -  0.497) size        4075.07 capacity         8192 load 70%
250.028 ( 0.300 -  0.499) size       32874.19 capacity        65536 load 70%
255.699 ( 0.303 -  0.496) size      261992.01 capacity       524288 load 70%
298.408 ( 0.295 -  0.453) size     2130284.17 capacity      4194304 load 70%
282.357 ( 0.238 -  0.155) size    17318656.31 capacity     33554432 load 70%

QUADRATIC fifo + 32 lookups
179.929 ( 0.347 -  0.375) size          62.81 capacity          128 load 70%
199.274 ( 0.403 -  0.494) size         508.73 capacity         1024 load 70%
243.749 ( 0.384 -  0.497) size        4094.90 capacity         8192 load 70%
252.651 ( 0.340 -  0.499) size       32668.97 capacity        65536 load 70%
260.940 ( 0.332 -  0.498) size      263331.17 capacity       524288 load 70%
292.955 ( 0.319 -  0.452) size     2122343.04 capacity      4194304 load 70%
279.794 ( 0.258 -  0.157) size    17400198.37 capacity     33554432 load 70%

HOOD fifo + 32 lookups
173.865 (10365.830 -  0.000) size       62.85 capacity          128 load 70%
173.484 (5370.840 -  0.000) size       508.32 capacity         1024 load 70%
269.977 (404.430 -  0.000) size       4087.91 capacity         8192 load 70%
299.441 (47.600 -  0.000) size       32761.97 capacity        65536 load 70%
316.539 ( 6.227 -  0.000) size      263371.68 capacity       524288 load 70%
361.464 ( 0.914 -  0.000) size     2095462.87 capacity      4194304 load 70%
378.975 ( 0.345 -  0.000) size    16789383.71 capacity     33554432 load 70%

//Largely the same as before. Linear and quadratic are winning.






*/