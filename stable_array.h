#ifndef MODULE_STABLE
#define MODULE_STABLE

// This is a data structure that aims to be as closely performant as array while being 'stable'
// meaning that pointers to items remain valid even with additions and removals.
// 
// We achieve this by storing unstable array of pointers to blocks of items. 
// Access is thus simply two pointer dereferences instead of one. This guarantees O(1) fast lookup.
// 
// It can be used for implementing "tables" - SQL like collections of items with possibly multiple
// accelerating hashes. The main advantage over regular array is that we dont have to worry about vacant slots 
// on removal and can skip the hash table lookup by keeping the pointer and using it (because the address is stable). 
// We can additionally keep also the key and compare if it still matches with the one currently there.
// 
// This structure can store up to UINT32_MAX*32 items which means 128GB worth of uint8_t's. Of course for
// that becomes 512GB for uint32_t. Because of the individual allocations for each of the blocks, the allocation
// of a new block is constant time. On the other hand any number of items can be located linearly in memory making 
// the iteration over elements very fast. Thus its well suite for large collections of data.  
// 
// Because we cannot swap any other item to a place of removed element we need to have some strategy
// for keeping track of removed item slots and filling them with newly added items. 
// We solve this by using bit fields where each bit indicates if the slot within block is used or empty. 
// Additionally we keep a linked list of blocks that contain at least one empty slot so that we never 
// have to scan through the entire array. This guarantees O(1) additions and removal.
// 
// Performance consideration here is mainly the number of independent addresses we need to fetch before
// doing operation of interest. We need: 
//  1: The unstable ptr array as dense as possible so that it can  remain in the cache easily
//  
//  2: The bit field and next not empty link to be close enough to other things so that they never
//     require additional memory fetch.
// 
//  3: The final item array to have no additional data inside it to guarantee optimal traversal speed.
// 
//  4: The number of allocations needs to be kept low. It should be possible to allocate exponentially 
//     bigger blocks at once.

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef MODULE_ALL_COUPLED
    #include "assert.h"
    #include "profile.h"
    #include "allocator.h"
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

typedef struct Stable_Block {
    uint8_t* ptr;
    uint64_t mask;
    uint32_t next_free;
    uint32_t was_alloced;
} Stable_Block;

typedef struct Stable {
    Allocator* allocator;
    Stable_Block* blocks;
    isize count;

    uint32_t blocks_count;
    uint32_t blocks_capacity;

    uint32_t item_size;
    uint32_t item_align;
    uint32_t allocation_size;
    uint32_t first_free;
} Stable;

#ifndef EXTERNAL
    #define EXTERNAL
#endif
#define STABLE_BLOCK_SIZE 64

//stable
//bucket

EXTERNAL void  stable_init_custom(Stable* stable, Allocator* alloc, isize item_size, isize item_align, uint32_t allocation_size);
EXTERNAL void  stable_init(Stable* stable, Allocator* alloc, isize item_size);
EXTERNAL void  stable_deinit(Stable* stable);

EXTERNAL isize stable_capacity(const Stable* stable);
EXTERNAL void* stable_at(const Stable* stable, isize index);
EXTERNAL void* stable_at_or(const Stable* stable, isize index, void* if_not_found);
EXTERNAL isize stable_insert(Stable* stable, void** out_or_null);
EXTERNAL void  stable_remove(Stable* stable, isize index);
EXTERNAL void  stable_reserve(Stable* stable, isize to);

//useful for tables
EXTERNAL isize stable_insert_nozero(Stable* stable, void** out_or_null);
EXTERNAL isize stable_insert_zero_from(Stable* stable, void** out_or_null, isize zero_from);

EXTERNAL void stable_test_consistency(const Stable* stable, bool slow_checks);

//Iteration (with inline impl for reasonable perf)
typedef struct Stable_Iter {
    Stable* stable;
    Stable_Block* block;
    uint32_t block_i;
    uint32_t item_i;
    isize index;
    bool did_break;
} Stable_Iter;

inline static Stable_Iter _stable_iter_precond(Stable* stable, isize from_id);
inline static bool  _stable_iter_cond(Stable_Iter* it);
inline static void  _stable_iter_postcond(Stable_Iter* it);
inline static void* _stable_iter_per_slot(Stable_Iter* it, isize item_size);

#define STABLE_FOR_CUSTOM(table_ptr, it, T, item, item_size, from_id) \
    for(Stable_Iter it = _stable_iter_precond(table_ptr, from_id); _stable_iter_cond(&it); _stable_iter_postcond(&it)) \
        for(T* item = NULL; it.item_i < STABLE_BLOCK_SIZE; it.item_i++, it.index++) \
            if(item = (T*) _stable_iter_per_slot(&it, item_size), item) \

#define STABLE_FOR_GENERIC(table_ptr, it, item) STABLE_FOR_CUSTOM((table_ptr), it, void, item, it.stable->item_size, 0)
#define STABLE_FOR(table_ptr, it, T, item) STABLE_FOR_CUSTOM((table_ptr), it, T, item, sizeof(T), 0)

#ifndef ASSERT
    #include <assert.h>
    #include <stdlib.h>
    #include <stdio.h>
    #define ASSERT(x, ...) assert(x)
    #define ASSERT_BOUNDS(x, ...) assert(x)
    #define CHECK_BOUNDS(i, count, ...) assert(0 <= (i) && (i) <= (count))
    #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
#endif

inline static Stable_Iter _stable_iter_precond(Stable* stable, isize from_id)
{
    Stable_Iter it = {0};
    it.stable = stable;
    it.item_i = from_id % STABLE_BLOCK_SIZE;
    it.block_i = from_id / STABLE_BLOCK_SIZE;
    it.index = from_id;
    return it;
}

inline static bool _stable_iter_cond(Stable_Iter* it)
{
    if(it->did_break == false && it->block_i < it->stable->blocks_count) {
        it->block = &it->stable->blocks[it->block_i];
        return true;
    }
    return false;
}

inline static void _stable_iter_postcond(Stable_Iter* it)
{
    it->did_break = (it->item_i != STABLE_BLOCK_SIZE);
    it->item_i = 0;
    it->block_i++;
}

inline static void* _stable_iter_per_slot(Stable_Iter* it, isize item_size)
{
    ASSERT(item_size == it->stable->item_size);
    Stable_Block* block = &it->stable->blocks[it->block_i];
    if(block->mask & (1ull << it->item_i)) {
        uint8_t* item_u8 = (uint8_t*) block->ptr + item_size*it->item_i;
        return item_u8;
    }
    return NULL;
}
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_STABLE)) && !defined(MODULE_HAS_IMPL_STABLE)
#define MODULE_HAS_IMPL_STABLE

#ifndef INTERNAL
     #define INTERNAL static
#endif

#include <string.h>
INTERNAL void _stable_check_consistency(const Stable* stable)
{
    (void) stable;
    #ifndef STABLE_DEBUG
        #if defined(DO_ASSERTS_SLOW)
            #define STABLE_DEBUG 2
        #elif !defined(NDEBUG)
            #define STABLE_DEBUG 1
        #else
            #define STABLE_DEBUG 0
        #endif
    #endif

    #if STABLE_DEBUG > 1
        stable_test_consistency(stable, true);
    #elif STABLE_DEBUG > 0
        stable_test_consistency(stable, false);
    #endif
}

#if defined(_MSC_VER)
    #include <intrin.h>
    INTERNAL int32_t _stable_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
#elif defined(__GNUC__) || defined(__clang__)
    INTERNAL int32_t _stable_find_first_set_bit64(uint64_t num)
    {
        ASSERT(num != 0);
        return __builtin_ffsll((long long) num) - 1;
    }
#else
    #error unsupported compiler!
#endif

INTERNAL void* _stable_alloc(Allocator* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
{
    #ifndef USE_MALLOC
        ASSERT(alloc);
        return (*alloc)(alloc, 0, new_size, old_ptr, old_size, align, NULL);
    #else
        if(new_size != 0) {
            void* out = realloc(old_ptr, new_size);
            TEST(out);
            return out;
        }
        else
            free(old_ptr);
    #endif
}

EXTERNAL void stable_init_custom(Stable* stable, Allocator* alloc, isize item_size, isize item_align, uint32_t allocation_size)
{
    ASSERT(item_size > 0 && item_align > 0 && item_align > 0);

    stable_deinit(stable);
    stable->allocator = alloc;
    stable->item_size = (uint32_t) item_size;
    stable->item_align = (uint32_t) item_align;
    stable->allocation_size = allocation_size;
    _stable_check_consistency(stable);
}

EXTERNAL void stable_init(Stable* stable, Allocator* alloc, isize item_size)
{
    stable_init_custom(stable, alloc, item_size, 64, 4096);
}

EXTERNAL isize stable_capacity(const Stable* stable)
{
    return stable->blocks_count * STABLE_BLOCK_SIZE;
}

EXTERNAL void stable_deinit(Stable* stable)
{
    _stable_check_consistency(stable);
    for(uint32_t i = 0; i < stable->blocks_count; )
    {
        uint32_t k = i;
        for(i += 1; i < stable->blocks_count; i++)
            if(stable->blocks[i].was_alloced)
                break;

        _stable_alloc(stable->allocator, 0, stable->blocks[k].ptr, (i - k)*STABLE_BLOCK_SIZE*stable->item_size, stable->item_align);
    }

    if(stable->blocks_count)
        _stable_alloc(stable->allocator, 0, stable->blocks, stable->blocks_count*sizeof(Stable_Block), 8);
    memset(stable, 0, sizeof *stable);
}

EXTERNAL void* stable_at(const Stable* stable, isize index)
{
    CHECK_BOUNDS(index, stable_capacity(stable));
    size_t block_i = (size_t) index / STABLE_BLOCK_SIZE;
    size_t item_i = (size_t) index %  STABLE_BLOCK_SIZE;
    Stable_Block* block = &stable->blocks[block_i];
    ASSERT_BOUNDS(block->mask & (1ull << item_i));
    return block->ptr + stable->item_size*item_i;
}

EXTERNAL void* stable_at_or(const Stable* stable, isize index, void* if_not_found)
{
    if(0 <= index && index <= stable_capacity(stable))
    {
        size_t block_i = (size_t) index / STABLE_BLOCK_SIZE;
        size_t item_i = (size_t) index %  STABLE_BLOCK_SIZE;
        Stable_Block* block = &stable->blocks[block_i];
        if(block->mask & (1ull << item_i))
            return block->ptr + stable->item_size*item_i;
    }

    return if_not_found;
}
EXTERNAL isize stable_insert_zero_from(Stable* stable, void** out_or_null, isize zero_from)
{
    _stable_check_consistency(stable);
    if(stable->count + 1 > stable_capacity(stable))
        stable_reserve(stable, stable->count + 1);

    isize block_i = stable->first_free - 1;
    Stable_Block* block = &stable->blocks[block_i];
    isize empty_i = _stable_find_first_set_bit64(~block->mask);
    block->mask |= (uint64_t) 1 << empty_i;

    //If is full remove from the linked list
    if(~block->mask == 0)
    {
        stable->first_free = block->next_free;
        block->next_free = 0;
    }

    isize out_i = block_i*STABLE_BLOCK_SIZE + empty_i;
    stable->count += 1;
    if(out_or_null)
        *out_or_null = block->ptr + empty_i*stable->item_size;

    _stable_check_consistency(stable);
    return out_i;
}

EXTERNAL isize stable_insert(Stable* stable, void** out_or_null)
{
    return stable_insert_zero_from(stable, out_or_null, 0);
}
EXTERNAL isize stable_insert_nozero(Stable* stable, void** out_or_null)
{
    return stable_insert_zero_from(stable, out_or_null, stable->item_size);
}

EXTERNAL void stable_remove(Stable* stable, isize index)
{
    _stable_check_consistency(stable);
    CHECK_BOUNDS(index, stable_capacity(stable));

    size_t block_i = (size_t) index / STABLE_BLOCK_SIZE;
    size_t item_i = (size_t) index %  STABLE_BLOCK_SIZE;
    Stable_Block* block = &stable->blocks[block_i];
    ASSERT_BOUNDS(block->mask & (1ull << item_i));

    //If was full before removal add to free list
    if(~block->mask == 0)
    {
        block->next_free = stable->first_free;
        stable->first_free = (uint32_t) block_i + 1;
    }

    stable->count -= 1;
    block->mask &= ~(1ull << item_i);
    _stable_check_consistency(stable);
}

EXTERNAL void stable_reserve(Stable* stable, isize to_size)
{
    if(to_size > stable_capacity(stable))
    {
        _stable_check_consistency(stable);
        
        isize desired_items = stable->allocation_size/stable->item_size;
        if(desired_items < to_size)
            desired_items = to_size;
        isize added_blocks = (desired_items + STABLE_BLOCK_SIZE - 1)/STABLE_BLOCK_SIZE;

        //If the ptr array needs reallocating
        if(stable->blocks_count + added_blocks > stable->blocks_capacity)
        {
            isize new_capacity = 16;
            while(new_capacity < stable->blocks_count + added_blocks)
                new_capacity *= 2;

            isize old_alloced = stable->blocks_capacity * sizeof(Stable_Block);
            isize new_alloced = new_capacity * sizeof(Stable_Block);
            
            uint8_t* alloced = (uint8_t*) _stable_alloc(stable->allocator, new_alloced, stable->blocks, old_alloced, 8);
            memset(alloced + old_alloced, 0, (size_t) (new_alloced - old_alloced));

            stable->blocks = (Stable_Block*) alloced;
            stable->blocks_capacity = (uint32_t) new_capacity;
        }

        ASSERT(stable->blocks_count < stable->blocks_capacity);

        isize alloced_blocks_bytes = added_blocks*stable->item_size*STABLE_BLOCK_SIZE;
        uint8_t* alloced_blocks = (uint8_t*) _stable_alloc(stable->allocator, alloced_blocks_bytes, NULL, 0, stable->item_align);
        memset(alloced_blocks, 0, (size_t) alloced_blocks_bytes);

        //Add the blocks into our array (backwards so that the next added item has lowest index)
        for(uint32_t i = (uint32_t) added_blocks; i-- > 0;)
        {
            uint32_t block_i = i + stable->blocks_count;
            stable->blocks[block_i].ptr = alloced_blocks + i*stable->item_size*STABLE_BLOCK_SIZE;
            stable->blocks[block_i].mask = 0;
            stable->blocks[block_i].next_free = stable->first_free;
            stable->first_free = block_i + 1;
        }
        
        stable->blocks[stable->blocks_count].was_alloced = true;
        stable->blocks_count += (uint32_t) added_blocks;
        _stable_check_consistency(stable);
    }

    ASSERT(stable->first_free != 0, "needs to have a place thats not filled when we reserved one!");
}

EXTERNAL void stable_test_consistency(const Stable* stable, bool slow_checks)
{
    if(stable->allocator == NULL)
        return;

    TEST(stable->blocks_count <= stable->blocks_capacity);
    TEST(stable->count <= stable_capacity(stable));

    bool is_power_of_two = ((uint64_t) stable->item_align & ((uint64_t) stable->item_align - 1)) == 0;
    TEST(stable->item_size > 0 && is_power_of_two, 
        "The item size and item align are those of a valid C type");
        
    TEST(stable->item_size*STABLE_BLOCK_SIZE % stable->item_align == 0, 
        "Needs to cleanly divide for simplicity of implementation (copy earlier commit if needed)");

    TEST((stable->blocks != NULL) == (stable->blocks_capacity > 0), 
        "When blocks are alloced capacity is non zero");
        
    TEST(0 <= stable->first_free && stable->first_free <= stable->blocks_count + 1, 
        "The not filled list needs to be in valid range");

    if(slow_checks)
    {
        isize computed_size = 0;
        isize not_filled_blocks = 0;
        isize alloacted_count = 0;

        //Check all blocks for valdity
        for(isize i = 0; i < stable->blocks_count; i++)
        {
            Stable_Block* block = &stable->blocks[i];
                
            TEST(block != NULL, "block is not null");
            TEST(0 <= block->next_free && block->next_free <= stable->blocks_count + 1,  
                "its next not filled needs to be in range");
                    
            TEST(block->ptr != NULL && (uintptr_t) block->ptr % stable->item_align == 0, 
                "the block must be properly aligned");

            isize item_count_in_block = 0;
            for(isize k = 0; k < STABLE_BLOCK_SIZE; k++)
            {
                uint64_t bit = (uint64_t) 1 << k;
                if(block->mask & bit)
                    item_count_in_block += 1;
            }

            if(item_count_in_block < STABLE_BLOCK_SIZE)
                not_filled_blocks += 1;

            computed_size += item_count_in_block;
        }
        TEST(computed_size == stable->count, 
            "The size retrieved from the used masks form all blocks needs to be exactly the tracked size");
            
        TEST(alloacted_count <= stable->blocks_count, 
            "The allocated size must be smaller than the total size. ");

        //Check not filled linked list for validity
        isize linked_list_size = 0;
        for(uint32_t block_i1 = stable->first_free;;)
        {
            if(block_i1 == 0)
                break;
            
            Stable_Block* block = &stable->blocks[block_i1 - 1];
            block_i1 = block->next_free;
            linked_list_size += 1;

            TEST(linked_list_size <= stable->blocks_count, "needs to not get stuck in an infinite loop");
            TEST(~block->mask > 0,                 "needs to have an empty slot");
        }

        TEST(linked_list_size == not_filled_blocks, "the number of not_filled blocks needs to be the lenght of the free list");
    }
}

#endif