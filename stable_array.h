#ifndef MODULE_STABLE_ARRAY
#define MODULE_STABLE_ARRAY

// This is a data structure that aims to be as closely performant as array while being "stable"
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
// 
// Because of these four considerations the following design was chosen: 
// In the unstable array we store Stable_Array_Block structs that contain ptr to the block, the mask
// of used/empty elements and the link to next non empty. Because ptr is 8B we need to have both the link
// and mask 4B (if mask was 8B the structure would get padded to 24B which is way too much for consideration (1) ).

#include "allocator.h"

#define STABLE_ARRAY_BLOCK_SIZE 64

typedef struct Stable_Array_Block {
    u8* ptr;
    u64 mask;
    u32 next_free;
    u32 was_alloced;
} Stable_Array_Block;

typedef struct Stable_Array {
    Allocator* allocator;
    Stable_Array_Block* blocks;

    u32 blocks_count;
    u32 blocks_capacity;

    isize count;
    u32 item_size;
    u32 item_align;
    u32 allocation_size;
    u32 first_free;
} Stable_Array;

EXTERNAL void  stable_array_init_custom(Stable_Array* stable, Allocator* alloc, isize item_size, isize item_align, u32 allocation_size);
EXTERNAL void  stable_array_init(Stable_Array* stable, Allocator* alloc, isize item_size);
EXTERNAL void  stable_array_deinit(Stable_Array* stable);

EXTERNAL isize stable_array_capacity(const Stable_Array* stable);
EXTERNAL void* stable_array_at(const Stable_Array* stable, isize index);
EXTERNAL void* stable_array_alive_at(const Stable_Array* stable, isize index, void* if_not_found);
EXTERNAL isize stable_array_insert(Stable_Array* stable, void** out_or_null);
EXTERNAL void  stable_array_remove(Stable_Array* stable, isize index);
EXTERNAL void  stable_array_reserve(Stable_Array* stable, isize to);

EXTERNAL void stable_array_test_invariants(const Stable_Array* stable, bool slow_checks);

#define STABLE_ARRAY_FOR_EACH_BEGIN_UNTYPED(stable, Ptr_Type, ptr_name, Index_Type, index)                          \
    for(isize _block_i = 0; _block_i < (stable).blocks_count; _block_i++)                                            \
    {                                                                                                               \
        bool _did_break = false;                                                                                    \
        Stable_Array_Block* _block = &(stable).blocks[_block_i];                                                    \
        if(_block->mask) { \
            for(isize _item_i = 0; _item_i < STABLE_ARRAY_BLOCK_SIZE; _item_i++) {      \
                if(_block->mask & ((u64) 1 << _item_i))                                                          \
                {                                                                                                       \
                    _did_break = true;                                                                                  \
                    Ptr_Type ptr_name = (Ptr_Type) (_block->ptr + _item_i*(stable).item_size); (void) ptr_name;           \
                    Index_Type index = (Index_Type) (_item_i + _block_i * STABLE_ARRAY_BLOCK_SIZE); (void) index;       \

#define STABLE_ARRAY_FOR_EACH_END   \
                    _did_break = false; \
                }                      \
            }                       \
         }                          \
         if(_did_break)             \
            break;                  \
     }                              \

#define STABLE_ARRAY_FOR_EACH_BEGIN(stable, Ptr_Type, ptr_name, Index_Type, index)                    \
        ASSERT((stable).item_size == isizeof(*(Ptr_Type) NULL), "wrong type submitted to ITERATE_STABLE_ARRAY_BEGIN"); \
        STABLE_ARRAY_FOR_EACH_BEGIN_UNTYPED(stable, Ptr_Type, ptr_name, Index_Type, index) \

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_STABLE_ARRAY)) && !defined(MODULE_HAS_IMPL_STABLE_ARRAY)
#define MODULE_HAS_IMPL_STABLE_ARRAY

#define _STABLE_ARRAY_DO_CHECKS
#define _STABLE_ARRAY_DO_SLOW_CHECKS
#define _STABLE_ARRAY_BLOCKS_ARR_ALIGN 8

INTERNAL void _stable_array_check_invariants(const Stable_Array* stable)
{
    (void) stable;
    #if defined(DO_ASSERTS)
        #if defined(_STABLE_ARRAY_DO_SLOW_CHECKS) && defined(DO_ASSERTS_SLOW)
            stable_array_test_invariants(stable, true);
        #else
            stable_array_test_invariants(stable, false);
        #endif
    #endif
}

EXTERNAL void stable_array_init_custom(Stable_Array* stable, Allocator* alloc, isize item_size, isize item_align, u32 allocation_size)
{
    ASSERT(item_size > 0 && item_align > 0 && is_power_of_two(item_align));

    stable_array_deinit(stable);
    stable->allocator = alloc;
    stable->item_size = (u32) item_size;
    stable->item_align = (u32) item_align;
    stable->allocation_size = allocation_size;
    _stable_array_check_invariants(stable);
}

EXTERNAL void stable_array_init(Stable_Array* stable, Allocator* alloc, isize item_size)
{
    stable_array_init_custom(stable, alloc, item_size, DEF_ALIGN, 4096);
}

EXTERNAL isize stable_array_capacity(const Stable_Array* stable)
{
    return stable->blocks_count * STABLE_ARRAY_BLOCK_SIZE;
}

EXTERNAL void stable_array_deinit(Stable_Array* stable)
{
    _stable_array_check_invariants(stable);
    for(u32 i = 0; i < stable->blocks_count; )
    {
        u32 k = i;
        for(i += 1; i < stable->blocks_count; i++)
            if(stable->blocks[i].was_alloced)
                break;

        allocator_deallocate(stable->allocator, stable->blocks[k].ptr, (i - k)*STABLE_ARRAY_BLOCK_SIZE*stable->item_size, stable->item_align);
    }

    allocator_deallocate(stable->allocator, stable->blocks, stable->blocks_count*isizeof(Stable_Array_Block), _STABLE_ARRAY_BLOCKS_ARR_ALIGN);
    memset(stable, 0, sizeof *stable);
}

EXTERNAL void* stable_array_at(const Stable_Array* stable, isize index)
{
    CHECK_BOUNDS(index, stable_array_capacity(stable));
    size_t block_i = (size_t) index / STABLE_ARRAY_BLOCK_SIZE;
    size_t item_i = (size_t) index %  STABLE_ARRAY_BLOCK_SIZE;
    Stable_Array_Block* block = &stable->blocks[block_i];
    REQUIRE(block->mask & (1ull << item_i));
    return block->ptr + stable->item_size*item_i;
}

EXTERNAL void* stable_array_alive_at(const Stable_Array* stable, isize index, void* if_not_found)
{
    if(0 <= index && index <= stable_array_capacity(stable))
    {
        size_t block_i = (size_t) index / STABLE_ARRAY_BLOCK_SIZE;
        size_t item_i = (size_t) index %  STABLE_ARRAY_BLOCK_SIZE;
        Stable_Array_Block* block = &stable->blocks[block_i];
        if(block->mask & (1ull << item_i))
            return block->ptr + stable->item_size*item_i;
    }

    return if_not_found;
}

EXTERNAL isize stable_array_insert(Stable_Array* stable, void** out)
{
    _stable_array_check_invariants(stable);
    if(stable->count + 1 > stable_array_capacity(stable))
        stable_array_reserve(stable, stable->count + 1);

    isize block_i = stable->first_free - 1;
    Stable_Array_Block* block = &stable->blocks[block_i];

    isize empty_i = platform_find_first_set_bit64(~block->mask);
    block->mask |= (u64) 1 << empty_i;

    //If is full remove from the linked list
    if(~block->mask == 0)
    {
        stable->first_free = block->next_free;
        block->next_free = 0;
    }

    isize out_i = block_i*STABLE_ARRAY_BLOCK_SIZE + empty_i;
    stable->count += 1;
    if(out)
        *out = block->ptr + empty_i * stable->item_size;

    _stable_array_check_invariants(stable);
    return out_i;
}

EXTERNAL void stable_array_remove(Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    CHECK_BOUNDS(index, stable_array_capacity(stable));

    size_t block_i = (size_t) index / STABLE_ARRAY_BLOCK_SIZE;
    size_t item_i = (size_t) index %  STABLE_ARRAY_BLOCK_SIZE;
    Stable_Array_Block* block = &stable->blocks[block_i];
    REQUIRE(block->mask & (1ull << item_i));

    //If was full before removal add to free list
    if(~block->mask == 0)
    {
        block->next_free = stable->first_free;
        stable->first_free = (u32) block_i + 1;
    }

    stable->count -= 1;
    block->mask &= ~(1ull << item_i);
    _stable_array_check_invariants(stable);
}

EXTERNAL void stable_array_reserve(Stable_Array* stable, isize to_size)
{
    if(to_size > stable_array_capacity(stable))
    {
        _stable_array_check_invariants(stable);
        
        isize desired_items = stable->allocation_size/stable->item_size;
        if(desired_items < to_size)
            desired_items = to_size;
        isize added_blocks = (desired_items + STABLE_ARRAY_BLOCK_SIZE - 1)/STABLE_ARRAY_BLOCK_SIZE;

        //If the ptr array needs reallocating
        if(stable->blocks_count + added_blocks > stable->blocks_capacity)
        {
            isize new_capacity = 16;
            while(new_capacity < stable->blocks_count + added_blocks)
                new_capacity *= 2;

            isize old_alloced = stable->blocks_capacity * sizeof(Stable_Array_Block);
            isize new_alloced = new_capacity * sizeof(Stable_Array_Block);
            
            u8* alloced = (u8*) allocator_reallocate(stable->allocator, new_alloced, stable->blocks, stable->blocks_capacity, _STABLE_ARRAY_BLOCKS_ARR_ALIGN);
            memset(alloced + old_alloced, 0, (size_t) (new_alloced - old_alloced));

            stable->blocks = (Stable_Array_Block*) alloced;
            stable->blocks_capacity = (u32) new_capacity;
        }

        ASSERT(stable->blocks_count < stable->blocks_capacity);

        isize alloced_blocks_bytes = added_blocks*stable->item_size*STABLE_ARRAY_BLOCK_SIZE;
        u8* alloced_blocks = (u8*) allocator_allocate(stable->allocator, alloced_blocks_bytes, stable->item_align);
        memset(alloced_blocks, 0, (size_t) alloced_blocks_bytes);

        //Add the blocks into our array (backwards so that the next added item has lowest index)
        for(u32 i = (u32) added_blocks; i-- > 0;)
        {
            u32 block_i = i + stable->blocks_count;
            stable->blocks[block_i].ptr = alloced_blocks + i*stable->item_size*STABLE_ARRAY_BLOCK_SIZE;
            stable->blocks[block_i].mask = 0;
            stable->blocks[block_i].next_free = stable->first_free;
            stable->first_free = block_i + 1;
        }
        
        stable->blocks[stable->blocks_count].was_alloced = true;
        stable->blocks_count += (u32) added_blocks;
        _stable_array_check_invariants(stable);
    }

    ASSERT(stable->first_free != 0, "needs to have a place thats not filled when we reserved one!");
}

EXTERNAL void stable_array_test_invariants(const Stable_Array* stable, bool slow_checks)
{
    if(stable->allocator == NULL)
        return;

    TEST(stable->blocks_count <= stable->blocks_capacity);
    TEST(stable->count <= stable_array_capacity(stable));

    TEST(stable->item_size > 0 && is_power_of_two(stable->item_align), 
        "The item size and item align are those of a valid C type");
        
    TEST(stable->item_size*STABLE_ARRAY_BLOCK_SIZE % stable->item_align == 0, 
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
            Stable_Array_Block* block = &stable->blocks[i];
                
            TEST(block != NULL, "block is not null");
            TEST(0 <= block->next_free && block->next_free <= stable->blocks_count + 1,  
                "its next not filled needs to be in range");
                    
            TEST(block->ptr != NULL && block->ptr == align_forward(block->ptr, stable->item_align), 
                "the block must be properly aligned");

            isize item_count_in_block = 0;
            for(isize k = 0; k < STABLE_ARRAY_BLOCK_SIZE; k++)
            {
                u64 bit = (u64) 1 << k;
                if(block->mask & bit)
                    item_count_in_block += 1;
            }

            if(item_count_in_block < STABLE_ARRAY_BLOCK_SIZE)
                not_filled_blocks += 1;

            computed_size += item_count_in_block;
        }
        TEST(computed_size == stable->count, 
            "The size retrieved from the used masks form all blocks needs to be exactly the tracked size");
            
        TEST(alloacted_count <= stable->blocks_count, 
            "The allocated size must be smaller than the total size. ");

        //Check not filled linked list for validity
        isize linked_list_size = 0;
        for(u32 block_i1 = stable->first_free;;)
        {
            if(block_i1 == 0)
                break;
            
            Stable_Array_Block* block = &stable->blocks[block_i1 - 1];
            block_i1 = block->next_free;
            linked_list_size += 1;

            TEST(linked_list_size <= stable->blocks_count, "needs to not get stuck in an infinite loop");
            TEST(~block->mask > 0,                 "needs to have an empty slot");
        }

        TEST(linked_list_size == not_filled_blocks, "the number of not_filled blocks needs to be the lenght of the free list");
    }
}

#endif