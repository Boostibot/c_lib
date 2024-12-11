#pragma once

#include "lib/channel.h"
#include "lib/sync.h"

#define CHAN_ATOMIC(t) t

#if defined(_MSC_VER)
_CHAN_INLINE_ALWAYS  
static bool atomic_cas128_weak(volatile void* destination, 
    uint64_t old_val_lo, uint64_t old_val_hi,
    uint64_t new_val_lo, uint64_t new_val_hi,
    memory_order succ_mem_order,
    memory_order fail_mem_ordder)
{
    (void) succ_mem_order;
    (void) fail_mem_ordder;

    __int64 compare_and_out[] = {(__int64) old_val_lo, (__int64) old_val_hi};
    return _InterlockedCompareExchange128(destination, (__int64) new_val_hi, (__int64) new_val_lo, compare_and_out) != 0;
}
#elif defined(__GNUC__) || defined(__clang__)
_CHAN_INLINE_ALWAYS 
static bool atomic_cas128_weak(volatile void* destination, 
    uint64_t old_val_lo, uint64_t old_val_hi,
    uint64_t new_val_lo, uint64_t new_val_hi,
    memory_order succ_mem_order,
    memory_order fail_mem_ordder)
{
    const bool weak = true;
    __uint128_t old_val = ((__uint128_t) old_val_hi << 64) | (__uint128_t) old_val_lo;
    __uint128_t new_val = ((__uint128_t) new_val_hi << 64) | (__uint128_t) new_val_lo;
    return __atomic_compare_exchange_n(
        destination, old_val, new_val, weak, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#else
    #error Unsupported compiler.
#endif


typedef struct Fat_Stack_Slot {
    struct Fat_Stack_Slot* next;
    uint8_t data[];
} Fat_Stack_Slot;

typedef union Fat_Ptr {
    Fat_Stack_Slot* ptr;
    uint64_t generation;
} Fat_Ptr;

typedef struct Fat_Stack {
    CHAN_ATOMIC(Fat_Ptr) first_free;
    CHAN_ATOMIC(Fat_Ptr) last_used;
} Fat_Stack;

void _fat_stack_push(CHAN_ATOMIC(Fat_Ptr)* last_ptr, Fat_Stack_Slot* slot)
{
    for(;;) {
        Fat_Ptr last = atomic_load(last_ptr);
        Fat_Ptr new_last = {ptr, last.generation};
        atomic_store(&slot->next, (Fat_Stack_Slot*) last.ptr);
        if(atomic_compare_exchange_weak(last_ptr, &last, new_last))
            break;
    }
}

Fat_Ptr _fat_stack_pop(CHAN_ATOMIC(Fat_Ptr)* last_ptr)
{
    for(;;) {
        Fat_Ptr last = atomic_load(last_ptr);
        if(last.ptr) 
            return last;
            
        Fat_Stack_Slot* slot = (Fat_Stack_Slot*) last.ptr;
        Fat_Ptr new_last = {slot->next, last.generation + 1};
        if(atomic_compare_exchange_weak(last_ptr, &last, new_last))
            return last;
    }
}

void* fat_stack_alloc(CHAN_ATOMIC(Fat_Ptr)* tail, isize item_size)
{
    Fat_Ptr popped = _fat_stack_pop(tail);
    if(popped.ptr)
        return popped.ptr->data;

    Fat_Stack_Slot* slot = (Fat_Stack_Slot*) malloc(sizeof(Fat_Stack_Slot) + item_size);
    slot->next = NULL; //Should this be atomic?
    return slot->data;
}

void fat_stack_free(CHAN_ATOMIC(Fat_Ptr)* tail, void* alloced)
{
    _fat_stack_push(tail, (Fat_Stack_Slot*) alloced - 1);
}

void fat_stack_push(Fat_Stack* stack, const void* item, isize item_size)
{
    void* mem = fat_stack_alloc(&stack->first_free, item_size);
    memcpy(mem, item, item_size);
    _fat_stack_push(&stack->last_used, (Fat_Stack_Slot*) item);
}

bool fat_stack_pop(Fat_Stack* stack, void* item, isize item_size)
{
    Fat_Ptr popped = _fat_stack_pop(&stack->last_used, (Fat_Stack_Slot*) item);
    if(popped.ptr) 
    {
        memcpy(item, popped.ptr->data, item_size);
        fat_stack_free(&stack->first_free, popped.ptr->data);
    }

    return popped.ptr != NULL;
}


//UNPACKED PTR

typedef struct _Gen_Ptr_* Pack_Ptr; 

typedef struct Unpack_Ptr {
    void* ptr;
    uint64_t gen;
} Unpack_Ptr;

Pack_Ptr gen_ptr_pack(void* ptr, uint64_t gen, isize aligned)
{
    const uint64_t mul = ((uint64_t) 1 << 48)/(uint64_t) aligned;

    uint64_t ptr_part = ((uint64_t) ptr / aligned) % mul;
    uint64_t gen_part = (uint64_t) gen * mul;

    uint64_t gen_ptr_val = ptr_part | gen_part;
    return (Pack_Ptr) gen_ptr_val;
}

Unpack_Ptr gen_ptr_unpack(Pack_Ptr ptr, const isize aligned)
{
    //Everything marked const will get compiled at compile time 
    // (given that aligned is known at compile time)
    Unpack_Ptr unpacked = {0};
    const uint64_t mul = ((uint64_t) 1 << 48)/(uint64_t) aligned;
    
    uint64_t ptr_bits = (uint64_t) ptr % mul;
    uint64_t gen_bits = (uint64_t) ptr / mul;
    
    //prepare ptr_mask = 0x0000FFFFFFFFFFF0 (at compile time)
    const uint64_t ptr_low_bits_mask = (uint64_t) -1 * (uint64_t) aligned;
    const uint64_t ptr_high_bits_mask = (uint64_t) -1 >> 16;
    const uint64_t ptr_mask = ptr_low_bits_mask & ptr_high_bits_mask;

    //Stuff dummy_ptr_bits with the bits of some address. 
    // I am hoping this will compile to simply using the stack pointer register
    uint64_t dummy_ptr_bits = (uint64_t) &unpacked;

    //"blend" between ptr_bits and dummy_ptr_bits based on ptr_mask: 
    // where there are 1s in ptr_mask the ptr_bits should be written else dummy_ptr_bits should be written.
    //The following two lines do the same thing except the uncommented is one instruction faster (and less readable)
    uint64_t ptr_val = ptr_bits ^ ((ptr_bits ^ dummy_ptr_bits) & ptr_mask); 
    //uint64_t ptr_val = (dummy_ptr_bits & ~ptr_mask) | (ptr_bits & ptr_mask);
    
    unpacked.gen = gen_bits;
    unpacked.ptr = ptr_val;
    return unpacked;
}

typedef struct Pack_Stack {
    CHAN_ATOMIC(Pack_Ptr) first_free;
    CHAN_ATOMIC(Pack_Ptr) last_used;
} Pack_Stack;

void _pack_stack_push(CHAN_ATOMIC(Pack_Ptr)* last_ptr, Fat_Stack_Slot* slot)
{
    for(;;) {
        Pack_Ptr last = atomic_load(last_ptr);
        Unpack_Ptr last_unpacked = gen_ptr_unpack(last, 8);
        Pack_Ptr new_last = gen_ptr_pack(ptr, last_unpacked.generation, 8);
        atomic_store(&slot->next, (Fat_Stack_Slot*) last_unpacked.ptr);
        if(atomic_compare_exchange_weak(last_ptr, &last, new_last))
            break;
    }
}

Unpack_Ptr _pack_stack_pop(CHAN_ATOMIC(Pack_Ptr)* last_ptr)
{
    for(;;) {
        Pack_Ptr last = atomic_load(last_ptr);
        Unpack_Ptr last_unpacked = gen_ptr_unpack(last, 8);
        if(last_unpacked.ptr) 
            return last_unpacked;
            
        Fat_Stack_Slot* slot = (Fat_Stack_Slot*) last_unpacked.ptr;
        Pack_Ptr new_last = gen_ptr_pack(slot->next, last.generation + 1, 8);
        if(atomic_compare_exchange_weak(last_ptr, &last, new_last))
            return last_unpacked;
    }
}

void* pack_stack_alloc(CHAN_ATOMIC(Pack_Ptr)* tail, isize item_size)
{
    Unpack_Ptr popped = _pack_stack_pop(tail);
    if(popped.ptr)
        return popped.ptr->data;

    Fat_Stack_Slot* slot = (Fat_Stack_Slot*) malloc(sizeof(Fat_Stack_Slot) + item_size);
    slot->next = NULL; //Should this be atomic?
    return slot->data;
}

void pack_stack_free(CHAN_ATOMIC(Pack_Ptr)* tail, void* alloced)
{
    _pack_stack_push(tail, (Fat_Stack_Slot*) alloced - 1);
}

void pack_stack_push(Pack_Stack* stack, const void* item, isize item_size)
{
    void* mem = pack_stack_alloc(&stack->first_free, item_size);
    memcpy(mem, item, item_size);
    _pack_stack_push(&stack->last_used, (Fat_Stack_Slot*) item);
}

bool pack_stack_pop(Pack_Stack* stack, void* item, isize item_size)
{
    Pack_Ptr popped = _pack_stack_pop(&stack->last_used, (Fat_Stack_Slot*) item);
    if(popped.ptr) 
    {
        memcpy(item, popped.ptr->data, item_size);
        pack_stack_free(&stack->first_free, popped.ptr->data);
    }

    return popped.ptr != NULL;
}

// MEM PTR

typedef int64_t isize;
typedef struct Index_Mem_Node {
    struct Index_Mem_Node* next;
    uint32_t capacity;
    uint32_t count;
    CHAN_ATOMIC(void*) blocks[];    
} Index_Mem_Node;

typedef struct Index_Mem {
    CHAN_ATOMIC(isize) capacity;
    CHAN_ATOMIC(void*) blocks;
} Index_Mem;

void* index_mem_get_explicit(Index_Mem* mem, isize index, isize block_size, isize item_size, memory_order order)
{
    void** blocks = atomic_load_explicit(&mem->blocks, order);
    Index_Mem_Node* node = (Index_Mem_Node*) blocks - 1;

    uint64_t block_i = (uint64_t) index / block_size;
    uint64_t item_i = (uint64_t) index % block_size;
    ASSERT(block->count == (uint32_t) block_i);
    ASSERT(0 <= index && index <= list->capacity);
    
    //all of this should compile into: 
    // mov rax, QWORD PTR [rdi+rsi*8]
    // given that item_size=8,4,2
    void* block = atomic_load_explicit(&blocks[block_i], order);
    return (uint8_t*) block + (uint64_t) item_size * (uint64_t)index;
}

void* index_mem_get(Index_Mem* mem, isize index, isize block_size, isize item_size)
{
    return index_mem_get_explicit(mem, index, item_size, memory_order_seq_cst);
}

//must be only called by one thread at a time. 
//Other threads can still access capacity and index_mem_get_explicit
void* index_mem_unsafe_grow(Index_Mem* mem, isize block_size, isize item_size)
{
    void** blocks = atomic_load_explicit(&mem->blocks, order);
    Index_Mem_Node* node = ((Index_Mem_Node*) (void*) blocks) - 1;
    
    isize old_capacity = 0;
    isize new_capacity = 0;
    if(blocks == NULL)
        new_capacity = 64;
    else if(node->count >= node->capacity) 
    {
        old_capacity = node->capacity;
        new_capacity = old_capacity*2;
    }

    if(new_capacity > 0)
    {
        Index_Mem_Node* new_node = (Index_Mem_Node*) calloc(sizeof(Index_Mem_Node) + new_capacity*sizeof(void*));
        new_node->capacity = new_capacity;
        new_node->count = 0;
        memcpy(new_node->blocks, blocks, old_capacity*sizeof(void*));

        new_node->next = node;
        atomic_store(&mem->blocks, new_node->blocks);
        node = new_node;        
    }

    void* new_block = chan_aligned_alloc(block_size*list->slot_size, 64);

    uint32_t my_index = node->count;
    node->count += 1;
    atomic_store(&node->block, new_block);
    atomic_fetch_add(&mem->capacity, block_size);

    return new_block;
}

//must be called from one thread. All other threads must no longer touch mem.
void index_mem_unsafe_deinit(Index_Mem* mem, isize block_size, isize item_size)
{
    Index_Mem_Node* last_node = ((Index_Mem_Node*) (void*) mem->blocks) - 1;
    if(mem->blocks != NULL)
    {
        for(uint32_t i = 0; i < last_node.count; i++)
            chan_aligned_free(last_node->blocks[i]);
    }

    for(Index_Mem_Node* node = last_node; node != NULL; )
    {
        Index_Mem_Node* next = node->next;
        free(node);
        node = next;
    }

    atomic_store(&mem->capacity, 0); 
    atomic_store(&mem->block, 0); 
}


#define FREE_LIST_BLOCK_SIZE 64

typedef union Gen_Index {
    struct {
        uint32_t index;
        uint32_t generation;
    };
    uint64_t combined;
} Gen_Index;

typedef struct Index_Stack {
    CHAN_ATOMIC(Gen_Index) last_used;
    CHAN_ATOMIC(Gen_Index) first_free;

    Index_Mem mem;

    uint32_t item_size;
    uint32_t slot_size;
    Ticket_Lock growing_lock;
} Index_Stack;

typedef struct Index_Stack_Slot {
    uint32_t next;
    uint8_t data[];
} Index_Stack_Slot;

typedef struct Index_Stack_Allocation {
    uint32_t index;
    uint32_t generation;
    void* ptr;
} Index_Stack_Allocation;

Index_Stack_Slot* sync_free_list_slot(Index_Stack* list, isize index)
{
    return (Index_Stack_Slot*) index_mem_get(&list->mem, index, FREE_LIST_BLOCK_SIZE, list->slot_size);    
}

void _index_stack_push(Index_Stack* list, CHAN_ATOMIC(Gen_Index)* last_ptr, Index_Stack_Allocation allocation)
{
    Index_Stack_Slot* slot = ((Index_Stack_Slot*) (void*) allocation.ptr) - 1;
    for(;;) {
        Gen_Index last = atomic_load(last_ptr);
        Gen_Index new_last = {allocation.index, last.generation};
        atomic_store(&slot->next, last);
        if(atomic_compare_exchange_weak(last_ptr, &last, new_last))
            break;
    }
}

Index_Stack_Allocation _index_stack_pop(Index_Stack* list, CHAN_ATOMIC(Gen_Index)* last_ptr)
{
    for(;;) {
        Gen_Index last = atomic_load(last_ptr);
        if(last.index == (uint32_t) -1) {
            Index_Stack_Allocation out = {-1, last.gen, NULL};
            return out;
        }
            
        Index_Stack_Slot* slot = sync_free_list_slot(list, last.index);
        Gen_Index new_last = {slot->next, last.generation + 1};
        if(atomic_compare_exchange_weak(last_ptr, &last, new_last))
        {
            slot->next = (uint32_t) -1;
            Index_Stack_Allocation out = {last.index, last.gen, slot->data};
            return out;
        }
    }
}

Index_Stack_Allocation index_stack_alloc(Index_Stack* list)
{
    Index_Stack_Allocation first_free = {0};
    for(;;) {
        first_free = _index_stack_pop(list, &list->first_free);
        if(first_free.ptr != NULL)
            break;
        else
        {
            ticket_lock(&list->growing_lock, SYNC_WAIT_BLOCK);
            //if someone grew in the meantime they have increased the generation.
            //In that case we dont have to grow ourselves.
            // (The genration couldnt have increased by other means because it only
            //  increases on pop and the queue is empty).
            Gen_Index reload_index = atomic_load(&list->first_free);
            if(reload_index.generation == first_free.generation)
            {
                //add a new block and link it to the free list. 
                //We link backwards so tht when we actually pop we pop front to back (not that it matters much).
                uint8_t* new_block = (uint8_t*) _sync_free_list_unsafe_grow(list, FREE_LIST_BLOCK_SIZE, list->slot_size);
                uint32_t prev = (uint32_t) -1;
                for(isize i = reload_capacity + FREE_LIST_BLOCK_SIZE; i-- > reload_capacity; )
                {
                    Index_Stack_Slot* curr = (Index_Stack_Slot*) (void*) (new_block + list->slot_size*i);
                    curr->next = prev;
                    prev = (uint32_t) i;
                }

                //publish our newly allcocated series of blocks
                Gen_Index new_first_free = {prev, first_free.generation + 1};
                atomic_store(&list->first_free, new_first_free);
            }
            
            ticket_unlock(&list->growing_lock, SYNC_WAIT_BLOCK);
        }
    }

    return first_free;
}

void index_stack_free(Index_Stack* list, Index_Stack_Allocation allocation)
{
    _sync_free_list_push(list, &list->first_free, allocation);
}

void index_stack_push(Index_Stack* list, const void* data)
{
    Index_Stack_Allocation alloc = index_stack_alloc(list);
    memcpy(alloc.ptr, data, list->item_size);
    _sync_free_list_push(list, &list->last_used, alloc);
}

bool index_stack_pop(Index_Stack* list, void* data)
{
    Index_Stack_Allocation alloc = _sync_free_list_pop(list, &list->last_used);
    if(alloc.ptr == NULL)
        return false;

    memcpy(data, alloc.ptr, list->item_size);
    index_stack_free(list, alloc);
    return true;
}