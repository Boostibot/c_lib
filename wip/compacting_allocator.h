#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

typedef uint64_t Compacted;
typedef uint32_t Compacted32;
typedef int64_t isize;

//Each block is max UINT32_MAX size
//Each allocations is max UINT32_MAX/2 size
//align is max 1 << 15

//Block creation:  
// - have as little of them as possible to reduce internal fragmentation
// due to end of block not being enough 
// - though they also allow more granularity which can help
// to reduce the need to move big chunks of data

//fragmentation = removed_bytes/used_to
typedef struct Compact_Block {
    isize reserved_to;
    isize commit_to;
    isize used_to;

    isize allocations_count;
    isize allocations_bytes;
    isize allocations_aligns;

    uint32_t slot_first;
    uint32_t slot_last;

    bool is_allocated;
    uint8_t* data;
} Compact_Block;

typedef struct Compacted_Slot {
    uint8_t* data;
    uint64_t size : 48;
    uint64_t align : 6;
    uint64_t align_offset : 8;
    uint32_t block;
    uint32_t gen;

    uint32_t next;
    uint32_t prev;
} Compacted_Slot;

typedef void* (*Compact_Alloc_Func)(void* context, isize min_size, isize* commit_to);
typedef void  (*Compact_Dealloc_Func)(void* context, void* block_ptr, isize commit_to, isize reserve_to);
typedef isize (*Compact_Commit_Func)(void* context, void* block_ptr, isize commit_to, isize min_size);

typedef struct Compacting_Allocator {
    Compacted_Slot* slots;
    uint32_t slots_count;
    uint32_t slots_capacity;

    Compact_Block* blocks;
    uint32_t blocks_count;
    uint32_t blocks_capacity;

    isize default_block_commit;

    uint8_t id_bits;
    uint8_t gen_bits;
    uint32_t slot_first_free;
    uint32_t block_current;

    Compact_Alloc_Func alloc;
    Compact_Dealloc_Func dealloc;
    Compact_Commit_Func commit;
    void* context;
} Compacting_Allocator;

#include <stdlib.h>
void* compact_malloc_alloc_func(void* context, isize min_size, isize* commit_to)
{
    *commit_to = min_size;
    return malloc(min_size);
}

void compact_malloc_dealloc_func(void* context, void* block_ptr, isize commit_to, isize reserve_to)
{
    free(block_ptr);
}

isize compact_malloc_commit_func(void* context, void* block_ptr, isize commit_to, isize min_size)
{
    return -1;
}


#define ASSERT(x, ..) assert(x)

inline static uint64_t _compact_pack(const Compacting_Allocator* intern, uint32_t id, uint32_t gen)
{
    ASSERT(id < (uint64_t) 1 << intern->id_bits);
    ASSERT(gen < (uint64_t) 1 << intern->gen_bits);

    return id | gen << intern->id_bits;
}
inline static void _compact_unpack(const Compacting_Allocator* intern, Interned interned, uint32_t* id, uint32_t* gen)
{
    uint64_t gen_mask = ((uint64_t) 1 << intern->gen_bits) - 1;
    uint64_t id_mask = ((uint64_t) 1 << intern->id_bits) - 1;
    *gen = (uint32_t) ((interned >> intern->id_bits) & gen_mask);
    *id = (uint32_t) (interned & id_mask);
}

inline static uint8_t _compact_ffs(uint64_t val);

static void _compact_grow_blocks(Compacting_Allocator* alloc)
{
    if(alloc->blocks_count >= alloc->blocks_capacity)
    {
        isize new_capacity = alloc->blocks_capacity*2 + 8;
        alloc->blocks = realloc(alloc->blocks, new_capacity*sizeof(Compact_Block));
        alloc->blocks_capacity = new_capacity;
    }

    alloc->curr_block = alloc->blocks_count++;
    Compact_Block* block = (Compact_Block*) alloc->blocks[alloc->curr_block];
    memset(block, 0, sizeof(Compact_Block));

    uint32_t block_capacity = alloc->default_block_commit ? alloc->default_block_commit : 64*1024;
    if(block_capacity < size)
        block_capacity = size;

    block->data = (void*) malloc(block_capacity);
    block->commit_to = block_capacity;
    block->reserved_to = block_capacity;
    block->is_allocated = true;
}

static inline void* _compact_align_forward(void* ptr, isize align_to)
{
    isize mask = align_to - 1;
    isize ptr_num = (isize) ptr;
    ptr_num += (-ptr_num) & mask;
    return (void*) ptr_num;
}


Compacted compact_alloc(Compacting_Allocator* alloc, isize size, isize align, isize align_offset)
{
    if(size == 0)
        return 0;

    //If there is no block insert one
    if(alloc->blocks_capacity == 0)
        _compact_grow_blocks(alloc);

    Compact_Block* block = &alloc->blocks[alloc->block_current];
    if(block->used_to + size + align > block->commit_to)
    {
        //find a block that has enough size
        uint32_t block_i = (uint32_t) -1;
        for(uint32_t i = 0; i < alloc->blocks_count; i++) {
            block = &alloc->blocks[i];
            if(block->used_to + size + align <= block->commit_to) {
                block_i = i;
                break;
            }
        }

        //else alloc one
        if(block_i == -1) 
            _compact_grow_blocks(alloc);
        else    
            alloc->block_current = block_i;
        block = &alloc->blocks[alloc->block_current];
    }

    //grab a free id
    uint32_t alloced_id = 0;
    {
        //if there are no free IDs allocate some
        if(alloc->slots_first_free == 0)
        {
            ASSERT(alloc->slots_count == alloc->slots_capacity);
            uint32_t new_slots_capacity = alloc->slots_capacity ? alloc->slots_capacity*2 : 64;
            uint32_t old_slots_capacity = alloc->slots_capacity;

            alloc->slots = (Compacted_Slot*) realloc(alloc->slots, new_slots_capacity*sizeof(Compacted_Slot));
            alloc->slots_capacity = old_slots_capacity;
            memset(alloc->slots + old_slots_capacity, 0, (new_slots_capacity - old_slots_capacity)*sizeof(Compacted_Slot));

            for(uint32_t i = new_slots_capacity; i-- > old_slots_capacity;) {
                alloc->slots[i].next = alloc->slots_first_free;
                alloc->slots_first_free = i;
            }
        }    
        
        alloced_id = alloc->slots_first_free;
        alloc->slots_first_free = alloc->slots[alloced_id - 1].next_free;
    }


    ASSERT(alloc->block_current);
    ASSERT(alloced_id);
    
    Compacted_Slot* slot = alloc->slots[alloced_id - 1];

    //get free space - either after the last allocation or start of the block.
    if(block->slot_last) {
        Compacted_Slot* last = &alloc->slots[block->slot_last - 1];
        slot->next = block->slot_last;
        last->prev = alloced_id;
        block->slot_last = alloced_id;
    }
    else {
        block->slot_first = alloced_id;
        block->slot_last = alloced_id;
    }

    slot->data = _compact_align_forward(block->used_to + align_offset, align) - align_offset;
    slot->align = _compact_ffs(align);
    slot->size = (uint64_t) size;
    slot->align_offset = (uint64_t) align_offset;
    slot->block_offset = (uint8_t*) (void*) block - slot;

    block->allocations_count += 1;
    block->allocations_bytes += size;
    block->allocations_aligns += align;
    return _compact_pack(alloc, alloced_id, slot->gen);
}

void compact_dealloc(Compacting_Allocator* alloc, Compacted compacted)
{
    uint32_t id = 0;
    uint32_t gen = 0;
    _compact_unpack(alloc, &id, &gen);

    if(0 < id && id < alloc->slots_capacity)
    {
        Compacted_Slot* slot = &alloc->slots[id - 1];
        Compact_Block* block = &alloc->blocks[slot->block];
        if(slot->generation == gen && slot->data != NULL)
        {
            //unlik self
            if(slot->next) 
                alloc->slots[slot->next - 1].prev = slot->prev;
            else 
                block->slot_last = slot->prev;

            if(slot->prev) 
                alloc->slots[slot->prev - 1].next = slot->next;
            else 
                block->slot_first = slot->next;

            //if was last then move used_to back
            if(slot->next == 0) {
                if(slot->prev) {
                    Compacted_Slot* prev = &alloc->slots[slot->prev - 1];
                    block->used_to = prev->data + prev->size;
                }
                else 
                    block->used_to = 0;
            }

            //update stats
            ASSERT(block->allocations_count >= 1);
            ASSERT(block->allocations_bytes >= slot->size);
            block->allocations_count -= 1;
            block->allocations_bytes -= slot->size;
            block->allocations_aligns += align;

            memset(slot, 0, sizeof *slot);
            slot->generation = gen + 1;
            slot->next = alloc->slot_first_free;
            alloc->slot_first_free = id;
        }
    }
}

void compact_all(Compacting_Allocator* alloc)
{

    for(isize block_i = 0; block_i < alloc->blocks_capacity; block_i++) {
        Compact_Block* block = &alloc->blocks[block_i];

        double frac = 0.5;
        isize combined_bytes = block->allocations_aligns + block->allocations_bytes;
        if(combined_bytes/block->used_to < frac)
        {
            uint8_t* move_tp
            for(isize i = block->slot_first; i; )
            {
                Compacted_Slot* slot = &alloc->slots[i];
                if(slot->next)


                i = slot->next;
            }
        }
    }
}