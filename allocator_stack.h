#ifndef JOT_ALLOCATOR_STACK
#define JOT_ALLOCATOR_STACK

#include "allocator.h"
#include "allocator_malloc.h"

// Allocates lineary from fixed buffer placing 8 byte headers in front of each allocation.
// On deallocation marks the header as free. If the most recently block is deallocated
// 'pops' it moving the writing location to its start. Repeats this process until a non-free
// block is found.

// This is extremely performant allocator. Despite seeming more complex and thus higher 
// overhead from a simple linear/bump allocator, the resulting performance of Stack_Allocator
// is usually higher (when using the allocated space). This is because bump allocator 
// always advances forward meining that new memory needs to be added to the cache.
// Stack_Allocator on the other hand reususes the most recently used memory completely eliminating 
// these fetches. This is of course dependent on the acess pattern. If we allocated just once 
// then reset the allocator then linear allocator is going to be faster. Usually however we 
// allocate/deallocate many things (lets say 1000) and then reset the allocator. In those cases
// Stack_Allocator is a lot better.

typedef struct Stack_Allocator {
    Allocator allocator;
    Allocator* parent;
    Allocation_List overflown;

    uint8_t* buffer_from;
    uint8_t* buffer_to;
    uint8_t* last_block_to;
    uint8_t* last_block_from;

    isize max_alloced;
    isize current_alloced;
} Stack_Allocator;

EXPORT void stack_allocator_init(Stack_Allocator* allocator, void* buffer, isize buffer_size, Allocator* parent);
EXPORT void stack_allocator_deinit(Stack_Allocator* allocator);

EXPORT Allocator_Set stack_allocator_init_use(Stack_Allocator* allocator, void* buffer, isize buffer_size, Allocator* parent);
EXPORT Allocator_Set stack_allocator_deinit_unuse(Stack_Allocator* allocator, Allocator_Set allocator_set);
       
EXPORT void* stack_allocator_allocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
EXPORT Allocator_Stats stack_allocator_get_stats(Allocator* self);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ALLOCATOR_STACK_IMPL)) && !defined(JOT_ALLOCATOR_STACK_HAS_IMPL)
#define JOT_ALLOCATOR_STACK_HAS_IMPL

INTERNAL ATTRIBUTE_INLINE_NEVER void* _stack_allocator_allocate_from_parent(Stack_Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
{
    if(self->parent == NULL)
    {
        allocator_out_of_memory(&self->allocator, new_size, old_ptr, old_size, align, "");
        return NULL;
    }
    else
    {
        return allocation_list_allocate(&self->overflown, self->parent, new_size, old_ptr, old_size, align);
    }
}

EXPORT void stack_allocator_init(Stack_Allocator* allocator, void* buffer, isize buffer_size, Allocator* parent)
{
    stack_allocator_deinit(allocator);

    allocator->parent = parent;
    if(buffer == NULL && buffer_size > 0 && parent != NULL)
        buffer = _stack_allocator_allocate_from_parent(allocator, buffer_size, NULL, 0, DEF_ALIGN);

    allocator->buffer_from = (uint8_t*) buffer;
    allocator->buffer_to = allocator->buffer_from + buffer_size ;

    allocator->last_block_to = allocator->buffer_from;
    allocator->last_block_from = allocator->buffer_from;

    allocator->allocator.allocate = stack_allocator_allocate;
    allocator->allocator.get_stats = stack_allocator_get_stats;
}

EXPORT void stack_allocator_deinit(Stack_Allocator* allocator)
{
    allocation_list_free_all(&allocator->overflown, allocator->parent);
}

EXPORT Allocator_Set stack_allocator_init_use(Stack_Allocator* allocator, void* buffer, isize buffer_size, Allocator* parent)
{
    stack_allocator_init(allocator, buffer, buffer_size, parent);
    return allocator_set_both(&allocator->allocator, &allocator->allocator);
}

EXPORT Allocator_Set stack_allocator_deinit_unuse(Stack_Allocator* allocator, Allocator_Set allocators)
{
    stack_allocator_deinit(allocator);
    return allocator_set(allocators);
}

typedef struct _Stack_Allocator_Slot {
    uint64_t prev_offset;
    
    #ifdef STACK_ALLOC_DEBUG
    uint64_t magic_number;
    #endif // STACK_ALLOC_DEBUG
} _Stack_Allocator_Slot;

#define STACK_ALLOCATOR_MAGIC_NUMBER (uint64_t) "stackal"
#define STACK_ALLOCATOR_FREE_BIT ((uint64_t) 1 << 63)

#ifdef STACK_ALLOC_DEBUG
#define STACK_ALLOC_ASSERT(x) ASSERT(x)
#else
#define STACK_ALLOC_ASSERT(x) (void) (x)
#endif // STACK_ALLOC_DEBUG

#include "profile.h"

INTERNAL bool _stack_allocator_dummy() {return false;}

INTERNAL void _stack_allocator_check_invariants(Stack_Allocator* self)
{
    STACK_ALLOC_ASSERT(self->last_block_to >= self->last_block_from);
    STACK_ALLOC_ASSERT(self->buffer_from <= self->last_block_to);
    STACK_ALLOC_ASSERT(self->last_block_to <= self->buffer_to);
    STACK_ALLOC_ASSERT(self->buffer_from <= self->last_block_from && self->last_block_from <= self->buffer_to);
}

INTERNAL void _stack_allocator_check_slot(_Stack_Allocator_Slot* slot)
{
    #ifdef STACK_ALLOC_DEBUG
    STACK_ALLOC_ASSERT(slot->magic_number == STACK_ALLOCATOR_MAGIC_NUMBER);
    #endif // STACK_ALLOC_DEBUG
    STACK_ALLOC_ASSERT((slot->prev_offset & ~STACK_ALLOCATOR_FREE_BIT) > 0);
}
INTERNAL void* _stack_allocator_allocate(Stack_Allocator* self, isize new_size, isize align)
{
    STACK_ALLOC_ASSERT(new_size >= 0 && is_power_of_two(align));

    if(align <= sizeof(uint64_t))
        align = sizeof(uint64_t);
        
    _stack_allocator_check_invariants(self);

    //get the adress at which to place _Stack_Allocator_Slot header
    uint8_t* available_from = self->last_block_to + sizeof(_Stack_Allocator_Slot);

    uint8_t* aligned_from = (uint8_t*) align_forward(available_from, align);
    uint8_t* aligned_to = aligned_from + new_size;

    if(aligned_to > self->buffer_to) 
        return _stack_allocator_allocate_from_parent(self, new_size, NULL, 0, align);

    _Stack_Allocator_Slot* slot = ((_Stack_Allocator_Slot*) aligned_from) - 1;
    isize diff = (isize) aligned_from - (isize) self->last_block_from;
    slot->prev_offset = (uint64_t) diff; 
    
    #ifdef STACK_ALLOC_DEBUG
    slot->magic_number = STACK_ALLOCATOR_MAGIC_NUMBER;
    #endif // STACK_ALLOC_DEBUG

    _stack_allocator_check_slot(slot);

    self->current_alloced += new_size;
    if(self->max_alloced < self->current_alloced)
        self->max_alloced = self->current_alloced;

    self->last_block_to = aligned_to;
    self->last_block_from = aligned_from;
    
    
    STACK_ALLOC_ASSERT(self->last_block_to > self->last_block_from);
    _stack_allocator_check_invariants(self);

    return aligned_from;
}

INTERNAL bool _stack_allocator_deallocate(Stack_Allocator* self, void* old_ptr, isize old_size, isize align)
{
    STACK_ALLOC_ASSERT(old_size >= 0 && is_power_of_two(align));
    uint8_t* ptr = (uint8_t*) old_ptr;
    if(ptr < self->buffer_from || self->buffer_to <= ptr) 
        return _stack_allocator_allocate_from_parent(self, 0, old_ptr, old_size, align);

    _Stack_Allocator_Slot *slot = ((_Stack_Allocator_Slot*) old_ptr) - 1;
    _stack_allocator_check_slot(slot);

    slot->prev_offset = slot->prev_offset | STACK_ALLOCATOR_FREE_BIT;
    self->current_alloced -= old_size;

    isize i = 0;
    while (true) 
    {
        i++;
        
        _stack_allocator_check_invariants(self);
        _Stack_Allocator_Slot* last_slot = (_Stack_Allocator_Slot*) self->last_block_from - 1;
        _stack_allocator_check_slot(last_slot);
        if((last_slot->prev_offset & STACK_ALLOCATOR_FREE_BIT) == 0)
            return true;

        self->last_block_to = (uint8_t*) last_slot;
        self->last_block_from = ((uint8_t*) self->last_block_from) - (last_slot->prev_offset & ~STACK_ALLOCATOR_FREE_BIT);
        _stack_allocator_check_invariants(self);

        if(self->last_block_from <= self->buffer_from)
        {
            self->last_block_from = self->buffer_from;
            self->last_block_to = self->buffer_from;
            break;
        }
    }
    
    _stack_allocator_check_invariants(self);
    
    return true;
} 
        
EXPORT void* stack_allocator_allocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
{
    PERF_COUNTER_START(c);
    Stack_Allocator* self_ = (Stack_Allocator*) (void*) self;

    void* new_ptr = NULL;
    if(new_size > 0)
        new_ptr = _stack_allocator_allocate(self_, new_size, align);

    if(old_ptr != NULL)
    {
        memcpy(new_ptr, old_ptr, MIN(new_size, old_size));
        _stack_allocator_deallocate(self_, old_ptr, old_size, align);
    }
    
    PERF_COUNTER_END(c);
    return new_ptr;
}

        
EXPORT Allocator_Stats stack_allocator_get_stats(Allocator* self)
{
    Stack_Allocator* self_ = (Stack_Allocator*) (void*) self;
    Allocator_Stats stats = {0};
    stats.type_name = "Stack_Allocator";
    stats.parent = self_->parent;
    stats.bytes_allocated = self_->current_alloced;
    stats.max_bytes_allocated = self_->max_alloced;
            
    return stats;
}

#endif