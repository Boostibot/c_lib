#ifndef LIB_STABLE_ARRAY
#define LIB_STABLE_ARRAY

// This is a data structure that aims to be as closely performant as array while being "stable"
// meaning that pointers to items remain valid even with additions and removals.
// 
// We achive this by storing unstable array of pointers to blocks of items. 
// Acess is thus simply two pointer dereferences instead of one. This guarantees O(1) fast lookup.
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
// and mask 4B (if mask was 8B the structure would get padded to 24B which is way too much for 1: ).
// 
// Additionally at the end of the unstable Stable_Array_Block array we store a bitfield of which blocks
// have allocations. This enables us to allocate lets say 4 blocks at once add them to the unstable array
// and leter recover the information that only the first of those blocks should be freed (and the size
// can be calculated also).
//
// This structure can be used for implementing "tables" - SQL like collections of items with multiple
// accelerating hashes. The main advantage over regular array is that we can skip the hash table lookup
// by keeping the pointer and using it. We can additionally keep also the key and compare if it still matches
// with the one currently there.
//
// The main feature missing from this data structure is a way of mapping ptr to index without having to
// iterate all the blocks. This could be solved by aligning the pointer and then looking it up inside some 
// hash table. This however requires a lot of item specific information (to be able to set the align properly)

#include "allocator.h"

#define STABLE_ARRAY_BLOCK_SIZE 32
#define STABLE_ARRAY_FILLED_MASK_FULL 0xffffffff

typedef struct Stable_Array_Block {
    void* ptr;
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
    i32 item_size;
    i32 item_align;
    i32 growth_lin;
    f32 growth_mult;
} Stable_Array;

EXPORT void  stable_array_init_custom(Stable_Array* stable, Allocator* alloc, isize item_size, isize item_align, isize growth_lin, f32 growth_mult);
EXPORT void  stable_array_init(Stable_Array* stable, Allocator* alloc, isize item_size);
EXPORT void  stable_array_deinit(Stable_Array* stable);

EXPORT isize stable_array_capacity(const Stable_Array* stable);
EXPORT void* stable_array_get_block(const Stable_Array* stable, isize index);
EXPORT void* stable_array_get_block_safe(const Stable_Array* stable, isize index);
EXPORT void* stable_array_at(const Stable_Array* stable, isize index);
EXPORT void* stable_array_at_if_alive(const Stable_Array* stable, isize index);
EXPORT void* stable_array_at_safe(const Stable_Array* stable, isize index);
EXPORT isize stable_array_insert(Stable_Array* stable, void** out);
EXPORT bool  stable_array_remove(Stable_Array* stable, isize index);
EXPORT void  stable_array_reserve(Stable_Array* stable, isize to);

#define STABLE_ARRAY_FOR_EACH_BEGIN(stable, Ptr_Type, ptr_name, Index_Type, index)                    \
    for(isize _block_i = 0; _block_i < (stable).blocks_size; _block_i++)                        \
    {                                                                                           \
        Ptr_Type _dummy = NULL;                                                                 \
        ASSERT_MSG((stable).item_size == sizeof(*_dummy), "wrong type submitted to ITERATE_STABLE_ARRAY_BEGIN"); \
        Stable_Array_Block* _block = &(stable).blocks[_block_i];                                \
        for(isize _item_i = 0; _item_i < STABLE_ARRAY_BLOCK_SIZE; _item_i++)                    \
        {                                                                                       \
            if(_block->filled_mask & ((u64) 1 << _item_i))                                      \
            {                                                                                   \
                Ptr_Type ptr_name = (Ptr_Type) ((u8*) _block->ptr + _item_i*(stable).item_size); (void) ptr_name;   \
                Index_Type index = (Index_Type) (_item_i + _block_i * STABLE_ARRAY_BLOCK_SIZE); (void) index; \
                
#define STABLE_ARRAY_FOR_EACH_END }}}   


#define STABLE_ARRAY_FOR_EACH_BEGIN2(stable, Ptr_Type, ptr_name, Index_Type, index)             \
    for(isize _block_i = 0, _item_i = 0; _block_i < (stable).blocks_size; _item_i++)            \
    {                                                                                           \
        if(_item_i > STABLE_ARRAY_BLOCK_SIZE)                                                   \
        {                                                                                       \
            _block_i += 1;                                                                      \
            _item_i = 0;                                                                        \
        }                                                                                       \
        Ptr_Type _dummy = NULL;                                                                 \
        ASSERT_MSG((stable).item_size == sizeof(*_dummy), "wrong type submitted to ITERATE_STABLE_ARRAY_BEGIN"); \
        Stable_Array_Block* _block = &(stable).blocks[_block_i];                                \
        if(_block->filled_mask & ((u64) 1 << _item_i))                                      \
        {                                                                                   \
            Ptr_Type ptr_name = (Ptr_Type) ((u8*) _block->ptr + _item_i*(stable).item_size); (void) ptr_name;   \
            Index_Type index = (Index_Type) (_item_i + _block_i * STABLE_ARRAY_BLOCK_SIZE); (void) index; \
                
#define STABLE_ARRAY_FOR_EACH_END2 }} 

#endif

#if (defined(LIB_ALL_IMPL) || defined(LIB_STABLE_ARRAY_IMPL)) && !defined(LIB_STABLE_ARRAY_HAS_IMPL)
#define LIB_STABLE_ARRAY_HAS_IMPL

#define _STABLE_ARRAY_DO_CHECKS
#define _STABLE_ARRAY_DO_SLOW_CHECKS
#define _STABLE_ARRAY_BLOCKS_ARR_ALIGN      8 /* @TEMP! */

typedef struct _Stable_Array_Lookup {
    isize block_i;
    isize item_i;
    Stable_Array_Block* block;
    void* item;
} _Stable_Array_Lookup;

EXPORT void stable_array_init_custom(Stable_Array* stable, Allocator* alloc, isize item_size, isize item_align, isize growth_lin, f32 growth_mult)
{
    ASSERT(item_size > 0 && is_power_of_two(item_align));

    stable_array_deinit(stable);
    stable->allocator = alloc;
    stable->item_size = (u32) item_size;
    stable->item_align = (u32) item_align;
    stable->growth_lin = (i32) growth_lin;
    stable->growth_mult = growth_mult;
}

EXPORT void stable_array_init(Stable_Array* stable, Allocator* alloc, isize item_size)
{
    stable_array_init_custom(stable, alloc, item_size, DEF_ALIGN, STABLE_ARRAY_BLOCK_SIZE, 1.5f);
}

INTERNAL _Stable_Array_Lookup _stable_array_lookup(const Stable_Array* stable, isize index)
{
    _Stable_Array_Lookup out = {0};
    out.block_i = index / STABLE_ARRAY_BLOCK_SIZE;
    out.item_i = index %  STABLE_ARRAY_BLOCK_SIZE;
    
    out.block = &stable->blocks[out.block_i];
    out.item = (u8*) out.block->ptr + stable->item_size*out.item_i;

    return out;
}

EXPORT isize stable_array_capacity(const Stable_Array* stable)
{
    return stable->blocks_size * STABLE_ARRAY_BLOCK_SIZE;
}

INTERNAL u64* _stable_array_alloced_mask(const Stable_Array* stable, u32 block_index, u64* bit)
{
    u32 mask_i = block_index / 64;
    u32 bit_i = block_index % 64;

    *bit = (u64) 1 << bit_i;
    u64* masks = (u64*) &stable->blocks[stable->blocks_capacity];
    return (u64*) &masks[mask_i];
}

INTERNAL isize _stable_array_alloced_mask_size(isize size)
{
    return DIV_ROUND_UP(size, 64);
}

INTERNAL void _stable_array_check_invariants(const Stable_Array* stable);

EXPORT void stable_array_deinit(Stable_Array* stable)
{
    if(stable->blocks_capacity > 0)
        _stable_array_check_invariants(stable);

    for(u32 i = 0; i < stable->blocks_size;)
    {
        u32 k = i + 1;
        for(; k < stable->blocks_size; k ++)
        {   
            u64 alloced_bit = 0;
            u64* alloced_mask = _stable_array_alloced_mask(stable, k, &alloced_bit);
            if(*alloced_mask & alloced_bit)
                break;
        }

        u8* start_block_ptr = (u8*) stable->blocks[i].ptr;
        u8* end_block_ptr = (u8*) stable->blocks[k - 1].ptr;

        isize alloced_bytes = (end_block_ptr - start_block_ptr) + stable->item_size * STABLE_ARRAY_BLOCK_SIZE;
        allocator_deallocate(stable->allocator, start_block_ptr, alloced_bytes, stable->item_align, SOURCE_INFO());
        i = k;
    }

    isize prev_extra = _stable_array_alloced_mask_size(stable->blocks_capacity) * sizeof(u64);
    isize prev_size = stable->blocks_capacity * sizeof(Stable_Array_Block);
    allocator_deallocate(stable->allocator, stable->blocks, prev_size + prev_extra, _STABLE_ARRAY_BLOCKS_ARR_ALIGN, SOURCE_INFO());

    memset(stable, 0, sizeof *stable);
}

EXPORT void* stable_array_get_block(const Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    CHECK_BOUNDS(index, stable_array_capacity(stable));

    isize block_i = index / STABLE_ARRAY_BLOCK_SIZE;
    
    void* block = stable->blocks[block_i].ptr;
    return block;
}

EXPORT void* stable_array_get_block_safe(const Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    if(0 <= index && index <= stable_array_capacity(stable))
    {
        isize block_i = index / STABLE_ARRAY_BLOCK_SIZE;

        void* block = stable->blocks[block_i].ptr;
        return block;
    }

    return NULL;
}

EXPORT void* stable_array_at(const Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    CHECK_BOUNDS(index, stable_array_capacity(stable));

    _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);

    #ifdef DO_BOUNDS_CHECKS
    u64 bit = (u64) 1 << lookup.item_i;
    bool is_alive = !!(lookup.block->filled_mask & bit);
    TEST_MSG(is_alive, "Needs to be alive! Use stable_array_at_if_alive if unsure!");
    #endif // DO_BOUNDS_CHECKS

    return lookup.item;
}

EXPORT void* stable_array_at_if_alive(const Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    if(0 <= index && index <= stable_array_capacity(stable))
    {
        _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);
        u64 bit = (u64) 1 << lookup.item_i;
        bool is_alive = !!(lookup.block->filled_mask & bit);
        if(is_alive)
            return lookup.item;
    }

    return NULL;
}

EXPORT void* stable_array_at_safe(const Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    if(0 <= index && index <= stable_array_capacity(stable))
    {
        _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);
        return lookup.item;
    }

    return NULL;
}

EXPORT isize stable_array_insert(Stable_Array* stable, void** out)
{
    _stable_array_check_invariants(stable);
    
    if(stable->size + 1 > stable_array_capacity(stable))
        stable_array_reserve(stable, stable->size + 1);

    ASSERT_MSG(out != NULL, "out must not be NULL!");
    ASSERT_MSG(stable->first_not_filled_i1 != 0, "needs to have a place thats not filled when we reserved one!");
    isize block_i = stable->first_not_filled_i1 - 1;
    CHECK_BOUNDS(block_i, stable->blocks_size);

    Stable_Array_Block* block = &stable->blocks[block_i];
    ASSERT_MSG(block->filled_mask != STABLE_ARRAY_FILLED_MASK_FULL, "Needs to have a free slot");

    isize first_empty_index = platform_find_first_set_bit32(~block->filled_mask);
    block->filled_mask |= (u64) 1 << first_empty_index;

    //If is full remove from the linked list
    if(block->filled_mask == STABLE_ARRAY_FILLED_MASK_FULL)
    {
        stable->first_not_filled_i1 = block->next_not_filled_i1;
        block->next_not_filled_i1 = 0;
    }

    isize out_index = block_i * STABLE_ARRAY_BLOCK_SIZE + first_empty_index;
    void* out_ptr = (u8*) block->ptr + first_empty_index * stable->item_size;
    memset(out_ptr, 0, stable->item_size);

    stable->size += 1;
    _stable_array_check_invariants(stable);

    *out = out_ptr;
    return out_index;
}

EXPORT bool stable_array_remove(Stable_Array* stable, isize index)
{
    _stable_array_check_invariants(stable);
    if(0 <= index && index <= stable_array_capacity(stable))
    {
        _Stable_Array_Lookup lookup = _stable_array_lookup(stable, index);
        Stable_Array_Block* block = lookup.block;
        u64 bit = (u64) 1 << lookup.item_i;

        memset(lookup.item, 0, stable->item_size);
        bool is_alive = !!(block->filled_mask & bit);

        //If is full
        if(block->filled_mask == STABLE_ARRAY_FILLED_MASK_FULL)
        {
            block->next_not_filled_i1 = stable->first_not_filled_i1;
            stable->first_not_filled_i1 = (u32) lookup.block_i + 1;
        }
        
        if(is_alive)
        {
            stable->size -= 1;
            block->filled_mask = block->filled_mask & ~bit;
        }

        _stable_array_check_invariants(stable);
        return is_alive;
    }

    return false;
}

EXPORT void stable_array_reserve(Stable_Array* stable, isize to_size)
{
    isize capacity = stable_array_capacity(stable);
    if(to_size > capacity)
    {
        _stable_array_check_invariants(stable);
        ASSERT_MSG(stable->first_not_filled_i1 == 0, "If there are not empty slots the stable array should really be full");
        
        isize new_capacity_item = (isize) (stable->size * (stable->growth_mult)) + stable->growth_lin;
        new_capacity_item = MAX(new_capacity_item, to_size);

        u32 blocks_before = stable->blocks_size;
        u32 blocks_after = (u32) DIV_ROUND_UP(new_capacity_item, STABLE_ARRAY_BLOCK_SIZE);
        
        //If the ptr array needs reallocating
        if(blocks_after > stable->blocks_capacity)
        {
            isize new_capacity = 8;
            while(new_capacity < blocks_after)
                new_capacity *= 2;

            isize old_extra = _stable_array_alloced_mask_size(stable->blocks_capacity) * sizeof(u64);
            isize old_alloced = stable->blocks_capacity * sizeof(Stable_Array_Block);

            isize new_extra = _stable_array_alloced_mask_size(new_capacity) * sizeof(u64);
            isize new_alloced = new_capacity * sizeof(Stable_Array_Block);
            
            u8* alloced = (u8*) allocator_reallocate(stable->allocator, new_alloced + new_extra, stable->blocks, old_alloced + old_extra, _STABLE_ARRAY_BLOCKS_ARR_ALIGN, SOURCE_INFO());
          
            ASSERT(old_extra <= new_extra);

            //move over the extra info
            memmove(alloced + new_alloced, alloced + old_alloced, old_extra);
            //clear the newly added info
            memset(alloced + new_alloced + old_extra, 0, new_extra - old_extra);
            //zero the added pointers - optional
            memset(alloced + old_alloced, 0, new_alloced - old_alloced);
            stable->blocks = (Stable_Array_Block*) alloced;
            stable->blocks_capacity = (u32) new_capacity;
        }

        ASSERT(stable->blocks_size < stable->blocks_capacity);
        
        u64 prev_alloced_bit = 0;
        u64* prev_mask = _stable_array_alloced_mask(stable, (u32) blocks_before, &prev_alloced_bit);
        (void) prev_mask;

        //Calculate the needed size for blocks
        isize alloced_blocks_bytes = 0;
        isize block_size = stable->item_size * STABLE_ARRAY_BLOCK_SIZE;
        for(isize i = blocks_before; i < blocks_after; i++)
        {
            alloced_blocks_bytes = (isize) align_forward((void*) alloced_blocks_bytes, stable->item_align);
            alloced_blocks_bytes += block_size;
        }

        u8* alloced_blocks = (u8*) allocator_allocate(stable->allocator, alloced_blocks_bytes, stable->item_align, SOURCE_INFO());
        //Optional!
        memset(alloced_blocks, 0, alloced_blocks_bytes);

        //Add the blocks into our array (backwards so that the next added item has lowest index)
        u8* curr_block_addr = (u8*) alloced_blocks + alloced_blocks_bytes;
        for(u32 i = blocks_after; i-- > blocks_before;)
        {
            curr_block_addr -= block_size;
            curr_block_addr = (u8*) align_backward(curr_block_addr, stable->item_align);
            stable->blocks[i].ptr = curr_block_addr;
            stable->blocks[i].filled_mask = 0;
            stable->blocks[i].next_not_filled_i1 = stable->first_not_filled_i1;
            stable->first_not_filled_i1 = i + 1;
        }
        
        stable->blocks_size = blocks_after;
        u64 alloced_bit = 0;
        u64* alloced_mask = _stable_array_alloced_mask(stable, (u32) blocks_before, &alloced_bit);
        *alloced_mask |= alloced_bit;

        _stable_array_check_invariants(stable);
    }
}

INTERNAL void _stable_array_check_invariants(const Stable_Array* stable)
{
    bool do_checks = false;
    bool do_slow_check = false;

    #if defined(DO_ASSERTS)
    do_checks = true;
    #endif

    #if defined(_STABLE_ARRAY_DO_SLOW_CHECKS) && defined(DO_ASSERTS_SLOW)
    do_slow_check = true;
    #endif

    #define IS_IN_RANGE(lo, a, hi) ((lo) <= (a) && (a) < (hi))

    if(do_checks)
    {
        if(stable->blocks_capacity > 0)
        {
            TEST_MSG((stable->allocator != NULL), 
                "If there is something alloced allocator must not be null!");
            //TEST_MSG(stable->first_not_filled_i1 != 0, 
                //"The not filled list is empty exactly when the stable array is completely filled (size == capacity)");
        }

        TEST_MSG((stable->blocks != NULL) == (stable->blocks_capacity > 0), 
            "When blocks are alloced capacity is non zero");

        TEST_MSG(stable->blocks_size <= stable->blocks_capacity, 
            "Size must be smaller than capacity!");
            
        TEST_MSG(stable->size <= stable_array_capacity(stable), 
            "Size must be smaller than capacity!");

        TEST_MSG(stable->item_size > 0 && is_power_of_two(stable->item_align), 
            "The item size and item align are those of a valid C type");
        
        TEST_MSG(IS_IN_RANGE(0, stable->first_not_filled_i1, stable->blocks_size + 1), 
            "The not filled list needs to be in valid range");

        if(do_slow_check)
        {
            isize computed_size = 0;
            isize not_filled_blocks = 0;
            isize alloacted_count = 0;

            //Check all blocks for valdity
            for(isize i = 0; i < stable->blocks_size; i++)
            {
                Stable_Array_Block* block = &stable->blocks[i];

                TEST_MSG(block != NULL && block->ptr != NULL,                           
                    "block is not null");
                TEST_MSG(IS_IN_RANGE(0, block->next_not_filled_i1, stable->blocks_size + 1),  
                    "its next not filled needs to be in range");
                    
                TEST_MSG(block->ptr == align_forward(block->ptr, stable->item_align), 
                    "the block must be properly aligned");

                isize item_count_in_block = 0;
                for(isize k = 0; k < STABLE_ARRAY_BLOCK_SIZE; k++)
                {
                    u64 bit = (u64) 1 << k;
                    if(block->filled_mask & bit)
                        item_count_in_block += 1;
                }
                
                u64 alloced_bit = 0;
                u64* alloced_mask = _stable_array_alloced_mask(stable, (u32) i, &alloced_bit);
                bool was_alloced = !!(*alloced_mask & alloced_bit);
                if(i == 0)
                    TEST_MSG(was_alloced, 
                        "The first block must be always alloced!");

                if(was_alloced)
                    alloacted_count += 1;

                if(item_count_in_block < STABLE_ARRAY_BLOCK_SIZE)
                    not_filled_blocks += 1;

                computed_size += item_count_in_block;
            }
            TEST_MSG(computed_size == stable->size, 
                "The size retrieved from the used masks form all blocks needs to be exactly the tracked size");
            
            TEST_MSG(alloacted_count <= stable->blocks_size, 
                "The allocated size must be smaller than the total size. ");

            //Check not filled linked list for validity
            isize linke_list_size = 0;
            isize iters = 0;
            for(u32 block_i1 = stable->first_not_filled_i1;;)
            {
                if(block_i1 == 0)
                    break;
            
                TEST_MSG(IS_IN_RANGE(0, block_i1, stable->blocks_size + 1), "the block needs to be in range");
                Stable_Array_Block* block = &stable->blocks[block_i1 - 1];

                block_i1 = block->next_not_filled_i1;
                linke_list_size += 1;

                TEST_MSG(~block->filled_mask > 0,        "needs to have an empty slot");
                TEST_MSG(iters++ <= stable->blocks_size, "needs to not get stuck in an infinite loop");
            }

            TEST_MSG(linke_list_size == not_filled_blocks, "the number of not_filled blocks needs to be the lenght of the list");
        }
    }

    #undef IS_IN_RANGE
}

#endif