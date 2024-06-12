#ifndef JOT_STABLE_ARRAY
#define JOT_STABLE_ARRAY

// This is a data structure that aims to be as closely performant as array while being "stable"
// meaning that pointers to items remain valid even with additions and removals.
// 
// We achive this by storing unstable array of pointers to blocks of items. 
// Acess is thus simply two pointer dereferences instead of one. This guarantees O(1) fast lookup.
// 
// It can be used for implementing "tables" - SQL like collections of items with possibly multiple
// accelerating hashes. The main advantage over regular array is that we dont have to worry about vacant slots 
// on removal and can skip the hash table lookup by keeping the pointer and using it (because the adress is stable). 
// We can additionally keep also the key and compare if it still matches with the one currently there.
// 
// This structure can store up to UINT32_MAX*32 items which means 128GB worht of uint8_t's. Of course for
// that becomes 512GB for uint32_t. Because of the individual allocations for each of the blocks, the allocation
// of a new block is constant time. On the otehr hand any number of items can be located lineary in memory making 
// the iteration over elements very fast. Thus its well suite for large collections of data.  
// 
// Because we cannot swap any other item to a place of removed element we need to have some strategy
// for keeping track of removed item slots and filling them with newly added items. 
// We solve this by using bit fields where each bit indicates if the slot within block is used or empty. 
// Additionally we keep a linked list of blocks that contain at least one empty slot so that we never 
// have to scan through the entire array. This guarantees O(1) additions and removal.
// 
// Peformance consideration here is mainly the number of independent adresses we need to fetch before
// doing operation of interest. We need: 
//  1: The unstable ptr array as dense as possible so that it can  remain in the cache easily
//  
//  2: The bit field and next not empty link to be close enough to other things so that they never
//     require additional memory fetch.
// 
//  3: The final item array to have no additional data inside it to gurrantee optimal traversal speed.
// 
//  4: The number of allocations needs to be kept low. It should be possible to allocate exponentially 
//     bigger blocks at once.
// 
// Because of these three considerations the following design was chosen: 
// In the unstable array we store Stable_Array_Block structs that contain ptr to the block, the mask
// of used/empty elements and the link to next non empty. Because ptr is 8B we need to have both the link
// and mask 4B (if mask was 8B the structure would get padded to 24B which is way too much for consideration (1) ).
// 
// Additionally we store inside the Stable_Array_Block structure a bit indicating wheter this blocks adress was
// the one used for allocation. This enables us to allocate lets say 4 blocks at once add them to the unstable array
// and leter recover the information that only the first of those blocks should be freed (and the size
// can be calculated also).
//
// The main feature missing from this data structure is a way of mapping ptr to index without having to
// iterate all the blocks. This could be solved by aligning the pointer and then looking it up inside some 
// hash table. This however requires a lot of item specific information (to be able to set the align properly).

#include "allocator.h"

#define STABLE_ARRAY_BLOCK_SIZE             32
#define STABLE_ARRAY_FILLED_MASK_FULL       (u32) 0xffffffff
#define STABLE_ARRAY_KEEP_DATA_FLAG         (u32) 0xffffffff
#define STABLE_ARRAY_BLOCK_ALLOCATED_BIT    (uintptr_t) 1

typedef struct Stable_Array_Block {
    uintptr_t ptr_and_is_allocated_bit;
    u32 filled_mask;
    u32 next_not_filled_i1;
} Stable_Array_Block;

typedef struct Stable_Array {
    Allocator* allocator;
    Stable_Array_Block* blocks;
    u32 blocks_size;
    u32 blocks_capacity;

    isize size;
    u32 first_not_filled_i1;
    u32 item_size;
    u32 item_align; //Needs to satisfy because of allocation strategy: item_align divides item_size*STABLE_ARRAY_BLOCK_SIZE 
    //(which is trivially satisfied regardless of item_size when item_align <= 32) 

    //Specifies how many buckets to grow by at once using the formula
    // old_capcity = blocks_capacity*STABLE_ARRAY_BLOCK_SIZE;
    // new_capacity = old_capcity*growth_mult + growth_lin;
    i32 growth_lin; //default value STABLE_ARRAY_BLOCK_SIZE
    f32 growth_mult; //default value 1.25f

    //Specifies the value to set the empty slots with. 
    //This happens when the blocks are first allocated but also when
    // the item at the slot is removed. We may set to :
    // - 0x00 to make sure everything is zero-init (default option)
    // - 0x55 (or other garbage value between 0x00 and 0xFF) to indicate errors. Used for debugging.
    // - STABLE_ARRAY_KEEP_DATA_FLAG to not fill at all when removing. Used for keeping generation counters per slots etc.
    u32 fill_empty_with; 
} Stable_Array;

EXPORT void  stable_array_init_custom(Stable_Array* stable, Allocator* alloc, isize item_size, isize item_align, isize growth_lin, f32 growth_mult, u32 fill_empty_with);
EXPORT void  stable_array_init(Stable_Array* stable, Allocator* alloc, isize item_size);
EXPORT void  stable_array_deinit(Stable_Array* stable);

EXPORT isize stable_array_capacity(const Stable_Array* stable);
EXPORT void* stable_array_at(const Stable_Array* stable, isize index);
EXPORT void* stable_array_alive_at(const Stable_Array* stable, isize index, void* if_not_found);
EXPORT isize stable_array_insert(Stable_Array* stable, void** out_or_null);
EXPORT void  stable_array_remove(Stable_Array* stable, isize index);
EXPORT void  stable_array_reserve(Stable_Array* stable, isize to);

EXPORT void stable_array_test_invariants(const Stable_Array* stable, bool slow_checks);

#define STABLE_ARRAY_FOR_EACH_BEGIN_UNTYPED(stable, Ptr_Type, ptr_name, Index_Type, index)                          \
    for(isize _block_i = 0; _block_i < (stable).blocks_size; _block_i++)                                            \
    {                                                                                                               \
        bool _did_break = false;                                                                                    \
        Stable_Array_Block* _block = &(stable).blocks[_block_i];                                                    \
        u8* block_ptr = (u8*) (_block->ptr_and_is_allocated_bit & ~STABLE_ARRAY_BLOCK_ALLOCATED_BIT);               \
        for(isize _item_i = 0; _item_i < STABLE_ARRAY_BLOCK_SIZE; _item_i++)                                        \
        {                                                                                                           \
            if(_block->filled_mask & ((u64) 1 << _item_i))                                                          \
            {                                                                                                       \
                _did_break = true;                                                                                  \
                Ptr_Type ptr_name = (Ptr_Type) (block_ptr + _item_i*(stable).item_size); (void) ptr_name;           \
                Index_Type index = (Index_Type) (_item_i + _block_i * STABLE_ARRAY_BLOCK_SIZE); (void) index;       \
                

#define STABLE_ARRAY_FOR_EACH_END   \
                _did_break = false; \
             }                      \
         }                          \
         if(_did_break)             \
            break;                  \
     }                              \

#define STABLE_ARRAY_FOR_EACH_BEGIN(stable, Ptr_Type, ptr_name, Index_Type, index)                    \
        ASSERT((stable).item_size == isizeof(*(Ptr_Type) NULL), "wrong type submitted to ITERATE_STABLE_ARRAY_BEGIN"); \
        STABLE_ARRAY_FOR_EACH_BEGIN_UNTYPED(stable, Ptr_Type, ptr_name, Index_Type, index) \

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_STABLE_ARRAY_IMPL)) && !defined(JOT_STABLE_ARRAY_HAS_IMPL)
#define JOT_STABLE_ARRAY_HAS_IMPL

#define _STABLE_ARRAY_DO_CHECKS
#define _STABLE_ARRAY_DO_SLOW_CHECKS
#define _STABLE_ARRAY_BLOCKS_ARR_ALIGN 8

INTERNAL void _stable_array_check_invariants(const Stable_Array* stable)
{
    #if defined(DO_ASSERTS)
        #if defined(_STABLE_ARRAY_DO_SLOW_CHECKS) && defined(DO_ASSERTS_SLOW)
            stable_array_test_invariants(stable, true);
        #else
            stable_array_test_invariants(stable, false);
        #endif
    #endif
}

EXPORT void stable_array_init_custom(Stable_Array* stable, Allocator* alloc, isize item_size, isize item_align, isize growth_lin, f32 growth_mult, u32 fill_empty_with)
{
    ASSERT(item_size > 0 && is_power_of_two(item_align));

    stable_array_deinit(stable);
    stable->allocator = alloc;
    stable->item_size = (u32) item_size;
    stable->item_align = (u32) item_align;
    stable->growth_lin = (i32) growth_lin;
    stable->growth_mult = growth_mult;
    stable->fill_empty_with = fill_empty_with;
    _stable_array_check_invariants(stable);
}

EXPORT void stable_array_init(Stable_Array* stable, Allocator* alloc, isize item_size)
{
    stable_array_init_custom(stable, alloc, item_size, DEF_ALIGN, STABLE_ARRAY_BLOCK_SIZE, 1.5f, 0x00);
}

EXPORT isize stable_array_capacity(const Stable_Array* stable)
{
    return stable->blocks_size * STABLE_ARRAY_BLOCK_SIZE;
}

EXPORT void stable_array_deinit(Stable_Array* stable)
{
    if(stable->blocks_capacity > 0)
        _stable_array_check_invariants(stable);

    for(u32 i = 0; i < stable->blocks_size;)
    {
        //Not every block is allocated individually. Instead blocks have STABLE_ARRAY_BLOCK_ALLOCATED_BIT flag in the ptr
        // that indicates wheter they are the first block in the allocation. 
        // => Iterate untill the next allocated block is found then deallocate the whole contiguous region at once
        u32 k = i + 1;
        for(; k < stable->blocks_size; k ++)
            if((uintptr_t) stable->blocks[k].ptr_and_is_allocated_bit & STABLE_ARRAY_BLOCK_ALLOCATED_BIT)
                break;

        u8* start_block_ptr = (u8*) (stable->blocks[i].ptr_and_is_allocated_bit & ~STABLE_ARRAY_BLOCK_ALLOCATED_BIT);
        u8* end_block_ptr = (u8*) (stable->blocks[k - 1].ptr_and_is_allocated_bit & ~STABLE_ARRAY_BLOCK_ALLOCATED_BIT);

        isize alloced_bytes = (end_block_ptr - start_block_ptr) + stable->item_size*STABLE_ARRAY_BLOCK_SIZE;
        allocator_deallocate(stable->allocator, start_block_ptr, alloced_bytes, stable->item_align);
        i = k;
    }

    allocator_deallocate(stable->allocator, stable->blocks, stable->blocks_capacity * isizeof(Stable_Array_Block), _STABLE_ARRAY_BLOCKS_ARR_ALIGN);
    memset(stable, 0, sizeof *stable);
}

typedef struct _Stable_Array_Lookup {
    size_t block_i;
    size_t item_i;
    Stable_Array_Block* block;
    void* item;
} _Stable_Array_Lookup;

INTERNAL _Stable_Array_Lookup _stable_array_lookup(const Stable_Array* stable, isize index)
{
    _Stable_Array_Lookup out = {0};
    //Casting to unsigned is important here since for signed types the
    // geenreated assembly for the divisions is quite messy.
    out.block_i = (size_t) index / STABLE_ARRAY_BLOCK_SIZE;
    out.item_i = (size_t) index %  STABLE_ARRAY_BLOCK_SIZE;
    
    out.block = &stable->blocks[out.block_i];
    u8* ptr = (u8*)(out.block->ptr_and_is_allocated_bit & ~STABLE_ARRAY_BLOCK_ALLOCATED_BIT);
    out.item = ptr + stable->item_size*out.item_i;

    return out;
}

EXPORT void* stable_array_at(const Stable_Array* stable, isize index)
{
    CHECK_BOUNDS(index, stable_array_capacity(stable));
    _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);
    u32 bit = (u32) 1 << lookup.item_i;
    ASSERT(!!(lookup.block->filled_mask & bit));

    return lookup.item;
}

EXPORT void* stable_array_alive_at(const Stable_Array* stable, isize index, void* if_not_found)
{
    if(0 <= index && index <= stable_array_capacity(stable))
    {
        _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);
        if(lookup.block->filled_mask & (u64) 1 << lookup.item_i)
            return lookup.item;
    }

    return if_not_found;
}

EXPORT isize stable_array_insert(Stable_Array* stable, void** out)
{
    _stable_array_check_invariants(stable);
    if(stable->size + 1 > stable_array_capacity(stable))
        stable_array_reserve(stable, stable->size + 1);

    ASSERT(stable->first_not_filled_i1 != 0, "needs to have a place thats not filled when we reserved one!");
    isize block_i = stable->first_not_filled_i1 - 1;
    CHECK_BOUNDS(block_i, stable->blocks_size);

    Stable_Array_Block* block = &stable->blocks[block_i];
    ASSERT(block->filled_mask != STABLE_ARRAY_FILLED_MASK_FULL, "Needs to have a free slot");

    isize first_empty_index = platform_find_first_set_bit32(~block->filled_mask);
    block->filled_mask |= (u64) 1 << first_empty_index;

    //If is full remove from the linked list
    if(block->filled_mask == STABLE_ARRAY_FILLED_MASK_FULL)
    {
        stable->first_not_filled_i1 = block->next_not_filled_i1;
        block->next_not_filled_i1 = 0;
    }

    u8* ptr = (u8*) (block->ptr_and_is_allocated_bit  & ~STABLE_ARRAY_BLOCK_ALLOCATED_BIT);
    void* out_ptr = ptr + first_empty_index * stable->item_size;
    if(stable->fill_empty_with != STABLE_ARRAY_KEEP_DATA_FLAG)
        memset(out_ptr, 0, (size_t) stable->item_size);

    stable->size += 1;
    _stable_array_check_invariants(stable);

    if(out)
        *out = out_ptr;

    return block_i*STABLE_ARRAY_BLOCK_SIZE + first_empty_index;
}

EXPORT void stable_array_remove(Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    CHECK_BOUNDS(index, stable_array_capacity(stable));

    _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);
    Stable_Array_Block* block = lookup.block;
    u32 bit = (u32) 1 << lookup.item_i;
    ASSERT(!!(block->filled_mask & bit));

    //If was full before removal add to free list
    if(block->filled_mask == STABLE_ARRAY_FILLED_MASK_FULL)
    {
        block->next_not_filled_i1 = stable->first_not_filled_i1;
        stable->first_not_filled_i1 = (u32) lookup.block_i + 1;
    }
        
    if(stable->fill_empty_with != STABLE_ARRAY_KEEP_DATA_FLAG)
        memset(lookup.item, stable->fill_empty_with, (size_t) stable->item_size);

    stable->size -= 1;
    block->filled_mask = block->filled_mask & ~bit;
    _stable_array_check_invariants(stable);
}

EXPORT void stable_array_reserve(Stable_Array* stable, isize to_size)
{
    isize capacity = stable_array_capacity(stable);
    if(to_size > capacity)
    {
        _stable_array_check_invariants(stable);
        ASSERT(stable->first_not_filled_i1 == 0, "If there are not empty slots the stable array should really be full");
        
        isize old_capacity = (isize) stable->blocks_capacity*STABLE_ARRAY_BLOCK_SIZE;
        isize new_capacity_item = (isize) ((f32) old_capacity * stable->growth_mult) + stable->growth_lin;
        new_capacity_item = MAX(new_capacity_item, to_size);

        u32 blocks_before = stable->blocks_size;
        u32 blocks_after = (u32) DIV_CEIL(new_capacity_item, STABLE_ARRAY_BLOCK_SIZE);
        
        //If the ptr array needs reallocating
        if(blocks_after > stable->blocks_capacity)
        {
            isize new_capacity = 8;
            while(new_capacity < blocks_after)
                new_capacity *= 2;

            isize old_alloced = stable->blocks_capacity * isizeof(Stable_Array_Block);
            isize new_alloced = new_capacity * isizeof(Stable_Array_Block);
            
            u8* alloced = (u8*) allocator_reallocate(stable->allocator, new_alloced, stable->blocks, old_alloced, _STABLE_ARRAY_BLOCKS_ARR_ALIGN);
            memset(alloced + old_alloced, 0, (size_t) (new_alloced - old_alloced));

            stable->blocks = (Stable_Array_Block*) alloced;
            stable->blocks_capacity = (u32) new_capacity;
        }

        ASSERT(stable->blocks_size < stable->blocks_capacity);
        isize alloced_blocks_bytes = (blocks_after - blocks_before)*stable->item_size*STABLE_ARRAY_BLOCK_SIZE;
        u8* alloced_blocks = (u8*) allocator_allocate(stable->allocator, alloced_blocks_bytes, stable->item_align);
        memset(alloced_blocks, 0, (size_t) alloced_blocks_bytes);

        //Add the blocks into our array (backwards so that the next added item has lowest index)
        uintptr_t curr_block_addr = (uintptr_t) alloced_blocks + alloced_blocks_bytes;
        for(u32 i = blocks_after; i-- > blocks_before;)
        {
            curr_block_addr -= stable->item_size*STABLE_ARRAY_BLOCK_SIZE;
            stable->blocks[i].ptr_and_is_allocated_bit = curr_block_addr;
            stable->blocks[i].filled_mask = 0;
            stable->blocks[i].next_not_filled_i1 = stable->first_not_filled_i1;
            stable->first_not_filled_i1 = i + 1;
        }
        
        stable->blocks_size = blocks_after;
        //mark the block on the allocation as allocated 
        stable->blocks[blocks_before].ptr_and_is_allocated_bit |= STABLE_ARRAY_BLOCK_ALLOCATED_BIT;
        _stable_array_check_invariants(stable);
    }
}

EXPORT void stable_array_test_invariants(const Stable_Array* stable, bool slow_checks)
{
    #define IS_IN_RANGE(lo, a, hi) ((lo) <= (a) && (a) < (hi))
    TEST((stable->allocator != NULL));
    TEST(stable->blocks_size <= stable->blocks_capacity);
    TEST(stable->size <= stable_array_capacity(stable));

    TEST(stable->item_size > 0 && is_power_of_two(stable->item_align), 
        "The item size and item align are those of a valid C type");
        
    TEST(stable->item_size*STABLE_ARRAY_BLOCK_SIZE % stable->item_align == 0, 
        "Needs to cleanly divide for simplicity of implementation (copy earlier commit if needed)");

    TEST((stable->blocks != NULL) == (stable->blocks_capacity > 0), 
        "When blocks are alloced capacity is non zero");
        
    TEST(IS_IN_RANGE(0, stable->first_not_filled_i1, stable->blocks_size + 1), 
        "The not filled list needs to be in valid range");

    if(slow_checks)
    {
        isize computed_size = 0;
        isize not_filled_blocks = 0;
        isize alloacted_count = 0;

        //Check all blocks for valdity
        for(isize i = 0; i < stable->blocks_size; i++)
        {
            Stable_Array_Block* block = &stable->blocks[i];
                
            TEST(block != NULL, "block is not null");
            TEST(IS_IN_RANGE(0, block->next_not_filled_i1, stable->blocks_size + 1),  
                "its next not filled needs to be in range");
                    
            void* block_ptr = (void*) (block->ptr_and_is_allocated_bit & ~STABLE_ARRAY_BLOCK_ALLOCATED_BIT);
            TEST(block_ptr != NULL && block_ptr == align_forward(block_ptr, stable->item_align), 
                "the block must be properly aligned");

            isize item_count_in_block = 0;
            for(isize k = 0; k < STABLE_ARRAY_BLOCK_SIZE; k++)
            {
                u64 bit = (u64) 1 << k;
                if(block->filled_mask & bit)
                    item_count_in_block += 1;
            }
                
            bool was_alloced = !!(block->ptr_and_is_allocated_bit & STABLE_ARRAY_BLOCK_ALLOCATED_BIT);
            if(i == 0)
                TEST(was_alloced, "The first block must be always alloced!");

            if(was_alloced)
                alloacted_count += 1;

            if(item_count_in_block < STABLE_ARRAY_BLOCK_SIZE)
                not_filled_blocks += 1;

            computed_size += item_count_in_block;
        }
        TEST(computed_size == stable->size, 
            "The size retrieved from the used masks form all blocks needs to be exactly the tracked size");
            
        TEST(alloacted_count <= stable->blocks_size, 
            "The allocated size must be smaller than the total size. ");

        //Check not filled linked list for validity
        isize linked_list_size = 0;
        for(u32 block_i1 = stable->first_not_filled_i1;;)
        {
            if(block_i1 == 0)
                break;
            
            Stable_Array_Block* block = &stable->blocks[block_i1 - 1];
            block_i1 = block->next_not_filled_i1;
            linked_list_size += 1;

            TEST(linked_list_size <= stable->blocks_size, "needs to not get stuck in an infinite loop");
            TEST(~block->filled_mask > 0,                 "needs to have an empty slot");
        }

        TEST(linked_list_size == not_filled_blocks, "the number of not_filled blocks needs to be the lenght of the free list");
    }

    #undef IS_IN_RANGE
}

#endif