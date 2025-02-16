#ifndef MODULE_DEBUG_ALLOCATOR
#define MODULE_DEBUG_ALLOCATOR

// An allocator that tracks and validates allocation data. It validates the input pointers in a hash set,
// then validates the allocated blocks "dead zones" for overwrites and finally checks for validity of the provided
// size/alignment.
//
// For each allocation we optionally track the call stack at the time of the allocation, which can be used to
// print useful statistics on failure. 
// 
// The allocator is also capable of iterating, validating and printing all alive allocations for easy debugging.
//
//  Debug_Allocator
//  +-----------------------------------------------+
//  |   allocation_hash (static sized)              |                  +---------------------------------+
//  | +-------------+                               |   o------------->| call stack | dead | USER | dead |
//  | | 0           |                               |   |              +---------------------------------+
//  | | 0           |   Linked list of allocations  |   |    
//  | | 0x8157190a0 | --> Allocation <-> Allocation-----o              
//  | | 0           |                               |   
//  | | 0           |                               |             allocated block from parent allocator 
//  | | ...         |                               |       +------------------------------------------------+
//  | | 0x140144100 | --> Allocation ---------------------->| call stack | dead zone | USER DATA | dead zone | (*)
//  | | 0           |                               |       |------------------------|-----------------------+
//  | +-------------+                               |       |                        |               
//  +-----------------------------------------------+       L 8 aligned              L aligned to user specified align
//
// (*) Dead zones might be larger then specified by options to account for overaligned allocations.

#include "platform.h"
#include "assert.h"
#include "log.h"
#include "allocator.h"
#include "defines.h"
#include "profile.h"
#include <string.h>
#include <stdlib.h>

typedef struct Debug_Allocation {
    struct Debug_Allocation* next;
    struct Debug_Allocation* prev;

    void* ptr;
    isize size;
    uint16_t align;
    uint16_t pre_dead_zone;
    uint16_t post_dead_zone;
    uint16_t call_stack_count;
    uint64_t time;
    uint64_t id;
} Debug_Allocation;

typedef struct Debug_Allocator_Options {
    const char* name;                 //Optional name of this allocator for printing and debugging. 
    isize pre_dead_zone_size;         //size in bytes of overwrite prevention dead zone. 
    isize post_dead_zone_size;        //size in bytes of overwrite prevention dead zone. 
    isize capture_stack_frames_count; //number of stack frames to capture on each allocation. 
    bool do_printing;                 //prints all allocations/deallocation
    bool do_continual_checks;         //continually checks all allocations for overwrites
    bool do_deinit_leak_check;        //If the memory use on initialization and deinitialization does not match panics.
    bool do_set_as_default;           //set this allocator as default
    bool _[4];
} Debug_Allocator_Options;

//roughly one page big
typedef struct Debug_Allocation_Block {
    struct Debug_Allocation_Block* next;
    Debug_Allocation allocations[72];
} Debug_Allocation_Block;

typedef struct Debug_Allocator {
    Allocator alloc[1];
    Allocator* parent_alloc;
    Allocator* internal_alloc;
    
    isize alive_count;
    Debug_Allocation** allocation_hash;
    Debug_Allocation* allocation_first_free;
    Debug_Allocation_Block* allocation_blocks;

    bool is_within_allocation; 
    Debug_Allocator_Options options; //can be changed at will

    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    uint64_t last_id;
    Allocator_Set allocator_backup;
} Debug_Allocator;

#define DEBUG_ALLOC_LARGE_DEAD_ZONE     1  // dead_zone_size = 64 
#define DEBUG_ALLOC_NO_DEAD_ZONE        2  // dead_zone_size = 0 
#define DEBUG_ALLOC_CAPTURE_CALLSTACK   4  // capture_stack_frames_count = 16 
#define DEBUG_ALLOC_LEAK_CHECK          8  // do_deinit_leak_check = true 
#define DEBUG_ALLOC_CONTINUOUS          16 // do_continual_checks = true 
#define DEBUG_ALLOC_PRINT               32 // do_printing = true 
#define DEBUG_ALLOC_USE                 64 // do_set_as_default = true

EXTERNAL Debug_Allocator debug_allocator_make(Allocator* parent, uint64_t flags); //convenience wrapper for debug_allocator_init
EXTERNAL void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, Allocator* internal_allocator, Debug_Allocator_Options options);
EXTERNAL void debug_allocator_deinit(Debug_Allocator* allocator);

EXTERNAL void** debug_allocation_get_callstack(const Debug_Allocation* alloc);
EXTERNAL const Debug_Allocation* debug_allocator_get_allocation(const Debug_Allocator* allocator, void* ptr); 
EXTERNAL void debug_allocator_test_all_allocations(const Allocator* self);
EXTERNAL void debug_allocator_test_allocation(const Allocator* self, void* user_ptr);
EXTERNAL void debug_allocator_test_invariants(const Debug_Allocator* self);

#define DEBUG_ALLOC_PRINT_ALIVE_CALLSTACK 1
#define DEBUG_ALLOC_PRINT_ALIVE_TIME      2
EXTERNAL void debug_allocator_print_alive_allocations(const char* name, Log_Type log_type, const Debug_Allocator* allocator, isize print_max, uint32_t flags);

EXTERNAL void* debug_allocator_func(void* self, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_DEBUG_ALLOCATOR)) && !defined(MODULE_HAS_IMPL_DEBUG_ALLOCATOR)
#define MODULE_HAS_IMPL_DEBUG_ALLOCATOR

#define _DEBUG_ALLOC_HASH_SIZE 1024
#define _DEBUG_ALLOCATOR_FILL_NUM8   0x8F //one of the 4 rare values which dont have corresponding ascii code
#define _DEBUG_ALLOCATOR_MAGIC_NUM8  0x9D //another one
#define _DEBUG_ALLOCATOR_MAGIC_NUM64 0x9D9D9D9D9D9D9D9Dull

#ifndef INTERNAL
    #define INTERNAL inline static
#endif

INTERNAL Debug_Allocation* _debug_allocator_get_new_allocation(Debug_Allocator* self);
INTERNAL Debug_Allocation* _debug_allocator_find_allocation(const Debug_Allocator* self, void* ptr);
INTERNAL void _debug_allocator_insert_allocation(Debug_Allocator* debug, Debug_Allocation* allocation);
INTERNAL void _debug_allocator_remove_allocation(Debug_Allocator* self, Debug_Allocation* allocation);
INTERNAL void _debug_allocator_deallocate_allocation(Debug_Allocator* self, Debug_Allocation* allocation);
INTERNAL void _debug_allocator_check_invariants(const Debug_Allocator* self);
INTERNAL void _debug_allocator_panic(const Debug_Allocator* self, void* user_ptr, const Debug_Allocation* allocation, isize dist, const char* panic_reason);
INTERNAL void _debug_allocation_test_dead_zones(const Debug_Allocator* self, const Debug_Allocation* allocation);
INTERNAL Debug_Allocation* _debug_allocation_get_closest(const Debug_Allocator* self, void* ptr, isize* dist_or_null);

EXTERNAL void debug_allocator_init(Debug_Allocator* self, Allocator* parent, Allocator* internal, Debug_Allocator_Options options)
{
    debug_allocator_deinit(self);
    TEST(parent && internal);
    
    self->parent_alloc = parent;
    self->internal_alloc = internal;
    self->options = options;
    self->options.pre_dead_zone_size = DIV_CEIL(self->options.pre_dead_zone_size, sizeof(uint64_t))*sizeof(uint64_t);
    self->options.post_dead_zone_size = DIV_CEIL(self->options.post_dead_zone_size, sizeof(uint64_t))*sizeof(uint64_t);
    self->alloc[0] = debug_allocator_func;

    self->allocation_hash = ALLOCATE(self->internal_alloc, _DEBUG_ALLOC_HASH_SIZE, Debug_Allocation*);
    memset(self->allocation_hash, 0, sizeof(Debug_Allocation*)*_DEBUG_ALLOC_HASH_SIZE);

    if(options.do_set_as_default)
        self->allocator_backup = allocator_set_default(self->alloc);
        
    _debug_allocator_check_invariants(self);
}

EXTERNAL void debug_allocator_deinit(Debug_Allocator* self)
{
    _debug_allocator_check_invariants(self);
    if(self->alive_count && self->options.do_deinit_leak_check)
    {
        const char* name = self->options.name;
        LOG_FATAL("DEBUG", "debug allocator%s%s leaking memory. Printing all allocations (%lli) below:", 
                name ? "name: " : "", name, (lli) self->alive_count);
        debug_allocator_print_alive_allocations(">DEBUG", LOG_FATAL, self, -1, DEBUG_ALLOC_PRINT_ALIVE_CALLSTACK | DEBUG_ALLOC_PRINT_ALIVE_TIME);
        PANIC("debug allocator%s%s reported failure '%s'", name ? "name: " : "", name, "memory leaked");
    }
    if(self->allocation_hash) {
        for(isize i = 0; i < _DEBUG_ALLOC_HASH_SIZE; i++) {
            for(Debug_Allocation* curr = self->allocation_hash[i]; curr; ) {
                ASSERT(curr->size != -1);
                Debug_Allocation* next = curr->next;
                _debug_allocation_test_dead_zones(self, curr);
                _debug_allocator_deallocate_allocation(self, curr);
                curr = next;
            }
        }

        DEALLOCATE(self->internal_alloc, self->allocation_hash, _DEBUG_ALLOC_HASH_SIZE, Debug_Allocation*);
    }

    for(Debug_Allocation_Block* curr = self->allocation_blocks; curr; ) {
        Debug_Allocation_Block* next = curr->next;
        DEALLOCATE(self->internal_alloc, curr, 1, Debug_Allocation_Block);
        curr = next;
    }

    if(self->options.do_set_as_default)
        allocators_set(self->allocator_backup);
    memset(self, 0, sizeof *self);
}

EXTERNAL Debug_Allocator debug_allocator_make(Allocator* parent, uint64_t flags)
{
    Debug_Allocator_Options options = {0};
    options.pre_dead_zone_size = 8;
    options.post_dead_zone_size = 16;
    if(flags & DEBUG_ALLOC_CONTINUOUS)
        options.do_continual_checks = true;
    if(flags & DEBUG_ALLOC_PRINT)
        options.do_printing = true;
    if(flags & DEBUG_ALLOC_LEAK_CHECK)
        options.do_deinit_leak_check = true;
    if(flags & DEBUG_ALLOC_USE)
        options.do_set_as_default = true;
    if(flags & DEBUG_ALLOC_LARGE_DEAD_ZONE) {
        options.post_dead_zone_size = 64;
        options.pre_dead_zone_size = 32;
    }
    if(flags & DEBUG_ALLOC_NO_DEAD_ZONE) {
        options.post_dead_zone_size = 0;
        options.pre_dead_zone_size = 0;
    }
    if(flags & DEBUG_ALLOC_CAPTURE_CALLSTACK)
        options.capture_stack_frames_count = 16;
    
    Debug_Allocator allocator = {0};
    debug_allocator_init(&allocator, parent, parent, options);
    return allocator;
}

EXTERNAL void** debug_allocation_get_callstack(const Debug_Allocation* self)
{
    uint8_t* ptr = (uint8_t*) self->ptr - self->pre_dead_zone - self->call_stack_count*sizeof(void*);
    return (void**) ptr;
}

EXTERNAL void* debug_allocator_func(void* self_void, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest)
{
    if(mode == ALLOCATOR_MODE_ALLOC) {
        PROFILE_START();
        Debug_Allocator* self = (Debug_Allocator*) (void*) self_void;
        _debug_allocator_check_invariants(self);
    
        const char* name = self->options.name;
        if(new_size < 0 || old_size < 0 || is_power_of_two(align) == false || align >= UINT16_MAX) {
            LOG_FATAL("DEBUG", "debug allocator%s%s provided with invalid params new_size:%lli old_size:%lli align:%lli ", 
                name ? "name: " : "", name, (llu) new_size, (llu) old_size, (llu) align);
            PANIC("debug allocator%s%s reported failure '%s'", name ? "name: " : "", name, "invalid size or align parameter");
        }

        //validate old ptr (if present)
        Debug_Allocation* old_alloc = NULL;
        Debug_Allocation* new_alloc = NULL;
        if(old_ptr != NULL)
        {
            old_alloc = _debug_allocator_find_allocation(self, old_ptr);
            if(old_alloc == NULL) {
                LOG_FATAL("DEBUG", "%s%s%s no allocation at 0x%016llx", 
                    name ? "name: " : "", name, (llu) old_ptr);

                isize closest_dist = 0;
                Debug_Allocation* closest = _debug_allocation_get_closest(self, old_ptr, &closest_dist);
                _debug_allocator_panic(self, old_ptr, closest, closest_dist, "invalid pointer");
            }

            if(old_alloc->size != old_size) {
                LOG_FATAL("DEBUG", "debug allocator%s%s size does not match for allocation 0x%016llx: given:%lli actual:%lli", 
                    name ? "name: " : "", name, (llu) old_ptr, (lli) old_size, (lli) old_alloc->size);
                _debug_allocator_panic(self, old_ptr, old_alloc, 0, "invalid size parameter");
            }
                
            if(old_alloc->align != align) {
                LOG_FATAL("DEBUG", "debug allocator%s%s align does not match for allocation 0x%016llx: given:%lli actual:%lli", 
                    name ? "name: " : "", name, (llu) old_ptr, (lli) align, (lli) old_alloc->align);
                _debug_allocator_panic(self, old_ptr, old_alloc, 0, "invalid align parameter");
            }
            _debug_allocation_test_dead_zones(self, old_alloc);
        }
        else
        {
            if(old_size != 0) {
                LOG_FATAL("DEBUG", "debug allocator%s%s given NULL allocation pointer but size of %lliB", 
                    name ? "name: " : "", name, (llu) old_size);
                
                PANIC("debug allocator%s%s reported failure '%s'", name ? "name: " : "", name, "invalid size parameter");
            }
        }

        //allocate new ptr
        uint8_t* out_ptr = NULL;
        if(new_size > 0)
        {
            isize preamble_size = self->options.pre_dead_zone_size + self->options.capture_stack_frames_count*sizeof(void*);
            isize postamble_size = self->options.post_dead_zone_size;
            isize new_total_size = preamble_size + postamble_size + align + new_size;
            uint8_t* new_block_ptr = (uint8_t*) allocator_try_reallocate(self->parent_alloc, new_total_size, NULL, 0, DEF_ALIGN, (Allocator_Error*) rest);
            if(new_block_ptr == NULL)
            {
                PROFILE_STOP();
                return NULL;
            }

            out_ptr = (uint8_t*) align_forward(new_block_ptr + preamble_size, align);
            void**   callstack = (void**) (void*) new_block_ptr;
            uint8_t* pre_dead_zone = (uint8_t*) (void*) (callstack + self->options.capture_stack_frames_count); 
            uint8_t* post_dead_zone = out_ptr + new_size;
             
            new_alloc = _debug_allocator_get_new_allocation(self);
            new_alloc->ptr = out_ptr;
            new_alloc->align = (uint16_t) align;
            new_alloc->size = new_size;
            new_alloc->call_stack_count = (uint16_t) self->options.capture_stack_frames_count;
            new_alloc->id = self->last_id++;
            new_alloc->pre_dead_zone = (uint16_t) (out_ptr - pre_dead_zone);
            new_alloc->post_dead_zone = (uint16_t) (new_block_ptr + new_total_size - post_dead_zone);
            new_alloc->time = platform_epoch_time();

            if(new_alloc->call_stack_count > 0)
                platform_capture_call_stack(callstack, new_alloc->call_stack_count, 1);

            ASSERT(new_block_ptr <= pre_dead_zone && pre_dead_zone + new_alloc->pre_dead_zone <= out_ptr);
            ASSERT(out_ptr + new_size <= post_dead_zone && post_dead_zone + new_alloc->post_dead_zone <= new_block_ptr + new_total_size);

            isize min_size = new_size < old_size ? new_size : old_size;
            memset(pre_dead_zone,  _DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_alloc->pre_dead_zone);
            if(old_ptr) memcpy(out_ptr, old_ptr, (size_t) min_size);
            memset(out_ptr + min_size, _DEBUG_ALLOCATOR_FILL_NUM8, (size_t) (new_size - min_size)); 
            memset(post_dead_zone, _DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_alloc->post_dead_zone);
            
            _debug_allocator_insert_allocation(self, new_alloc);
            #ifdef DO_ASSERTS_SLOW
                debug_allocator_test_allocation(self->alloc, out_ptr);
            #endif
        }
    
        //dealloc old ptr
        if(old_size != 0) {
            _debug_allocator_deallocate_allocation(self, old_alloc);
            _debug_allocator_remove_allocation(self, old_alloc);
        }
    
        self->bytes_allocated += new_size - old_size;
        self->max_bytes_allocated = MAX(self->max_bytes_allocated, self->bytes_allocated);
        if(self->options.do_printing && self->is_within_allocation == false)
        {
            self->is_within_allocation = true;
            LOG_DEBUG("MEMORY", "size: %10s -> %-10s ptr: 0x%016llx -> 0x%016llx align: %lli ",
                format_bytes(old_size).data, format_bytes(new_size).data, (llu) old_ptr, (llu) out_ptr, (lli) align);
            self->is_within_allocation = false;
        }

        if(old_size == 0)
            self->allocation_count += 1;
        if(new_size == 0)
            self->deallocation_count += 1;
        if(new_size != 0 && old_size != 0)
            self->reallocation_count += 1;
        
        #ifdef DO_ASSERTS_SLOW
            _debug_allocator_check_invariants(self);
        #else
            if(self->options.do_continual_checks)
                debug_allocator_test_all_allocations(self->alloc);
        #endif

        PROFILE_STOP();
        return out_ptr;
    }
    else if(mode == ALLOCATOR_MODE_GET_STATS) {
        Debug_Allocator* self = (Debug_Allocator*) (void*) self_void;
        Allocator_Stats out = {0};
        out.type_name = "Debug_Allocator";
        out.name = self->options.name;
        out.parent = self->parent_alloc;
        out.max_bytes_allocated = self->max_bytes_allocated;
        out.bytes_allocated = self->bytes_allocated;
        out.allocation_count = self->allocation_count;
        out.deallocation_count = self->deallocation_count;
        out.reallocation_count = self->reallocation_count;
        out.is_capable_of_free_all = true;
        out.is_capable_of_resize = true;
        out.is_growing = true;
        out.is_top_level = false;
        *(Allocator_Stats*) rest = out;
    }
    return NULL;
}

INTERNAL uint64_t _debug_alloc_ptr_hash(void* ptr) 
{
    uint64_t x = (uint64_t) ptr; 
    x = (x ^ (x >> 31) ^ (x >> 62)) * (uint64_t) 0x319642b2d24d8ec3;
    x = (x ^ (x >> 27) ^ (x >> 54)) * (uint64_t) 0x96de1b173f119089;
    x = x ^ (x >> 30) ^ (x >> 60);
    return x;
}

INTERNAL Debug_Allocation* _debug_allocator_get_new_allocation(Debug_Allocator* self)
{
    if(self->allocation_first_free == NULL)
    {
        Debug_Allocation_Block* block = ALLOCATE(self->internal_alloc, 1, Debug_Allocation_Block);
        memset(block, 0, sizeof *block);

        block->next = self->allocation_blocks;
        self->allocation_blocks = block;

        for(isize i = sizeof(block->allocations)/sizeof(block->allocations[0]); i-- > 0;)
        {
            block->allocations[i].next = self->allocation_first_free;
            self->allocation_first_free = &block->allocations[i];
        }
    }
    
    Debug_Allocation* out = self->allocation_first_free;
    self->allocation_first_free = out->next;
    memset(out, 0, sizeof *out);
    return out;
}

INTERNAL void _debug_allocator_insert_allocation(Debug_Allocator* self, Debug_Allocation* allocation)
{
    uint64_t hash = _debug_alloc_ptr_hash(allocation->ptr);
    uint64_t bucket_i = hash % _DEBUG_ALLOC_HASH_SIZE;
    Debug_Allocation** bucket = &self->allocation_hash[bucket_i];
    if(*bucket) 
        (*bucket)->prev = allocation;
    
    allocation->next = *bucket;
    *bucket = allocation;

    ASSERT(self->alive_count >= 0);
    self->alive_count += 1;
}

INTERNAL Debug_Allocation* _debug_allocator_find_allocation(const Debug_Allocator* self, void* ptr)
{
    uint64_t hash = _debug_alloc_ptr_hash(ptr);
    uint64_t bucket_i = hash % _DEBUG_ALLOC_HASH_SIZE;
    Debug_Allocation** bucket = &self->allocation_hash[bucket_i];
    for(Debug_Allocation* curr = *bucket; curr; curr = curr->next)
        if(curr->ptr == ptr)
            return curr;

    return NULL;
}

INTERNAL void _debug_allocator_remove_allocation(Debug_Allocator* self, Debug_Allocation* allocation)
{
    if(allocation->next)
        allocation->next->prev = allocation->prev;
        
    {
        //if is first in chain, make the bucket point to the next allocation
        uint64_t hash = _debug_alloc_ptr_hash(allocation->ptr);
        uint64_t bucket_i = hash % _DEBUG_ALLOC_HASH_SIZE;
        bool is_pointing_at_me = self->allocation_hash[bucket_i] == allocation;
        ASSERT(is_pointing_at_me == (allocation->prev == NULL));
    }
    
    if(allocation->prev)
        allocation->prev->next = allocation->next;
    else {
        //if is first in chain, make the bucket point to the next allocation
        uint64_t hash = _debug_alloc_ptr_hash(allocation->ptr);
        uint64_t bucket_i = hash % _DEBUG_ALLOC_HASH_SIZE;
        self->allocation_hash[bucket_i] = allocation->next;
    }

    //insert into free list
    allocation->size = -1;
    allocation->prev = NULL;
    allocation->next = self->allocation_first_free;
    self->allocation_first_free = allocation;
    
    self->alive_count -= 1;
    ASSERT(self->alive_count >= 0);
}


INTERNAL void _debug_allocator_deallocate_allocation(Debug_Allocator* self, Debug_Allocation* allocation)
{
    void* block = debug_allocation_get_callstack(allocation);
    isize total_size = sizeof(void*)*allocation->call_stack_count + allocation->pre_dead_zone + allocation->size + allocation->post_dead_zone;
    allocator_deallocate(self->parent_alloc, block, total_size, DEF_ALIGN);
}

#include <time.h>
INTERNAL void _debug_allocator_panic(const Debug_Allocator* self, void* user_ptr, const Debug_Allocation* allocation, isize dist, const char* panic_reason)
{
    if(allocation) {
        if(dist == 0)
            LOG_FATAL("DEBUG", "Printing allocation info for ptr 0x%016llx:", (llu) allocation->ptr);
        else    
            LOG_FATAL("DEBUG", "Printing closest allocation to ptr 0x%016llx. Allocation 0x%016llx (%lliB away):", (llu) user_ptr, (llu) allocation->ptr, (lli) dist);

        time_t epoch_time_secs = allocation->time / 1000000;
        struct tm local_time = *localtime(&epoch_time_secs);
        LOG_FATAL(">DEBUG", "size : %s (%lliB)", format_bytes(allocation->size).data, (lli) allocation->size);
        LOG_FATAL(">DEBUG", "align: %lli", (lli) allocation->align);
        LOG_FATAL(">DEBUG", "id   : %lli", (lli) allocation->id);
        LOG_FATAL(">DEBUG", "time : %02i:%02i:%02i", local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
        if(allocation->call_stack_count) {
            void** callstack = debug_allocation_get_callstack(allocation);
            LOG_FATAL(">DEBUG", "callstack:");
            log_captured_callstack(LOG_FATAL, ">>DEBUG", callstack, allocation->call_stack_count);
        }
    }

    PANIC("debug allocator%s%s reported failure '%s'", self->options.name ? "name: " : "", self->options.name, panic_reason);
}

INTERNAL void _debug_allocation_test_dead_zones(const Debug_Allocator* self, const Debug_Allocation* allocation)
{
    uint8_t* pre_dead_zone = (uint8_t*) allocation->ptr - allocation->pre_dead_zone;
    uint8_t* post_dead_zone = (uint8_t*) allocation->ptr + allocation->size;

    uint64_t* pre_aligned = (uint64_t*) pre_dead_zone;
    uint64_t* post_aligned = (uint64_t*) align_forward(post_dead_zone, sizeof(uint64_t));

    ASSERT((uintptr_t) pre_aligned % sizeof(uint64_t) == 0);
    ASSERT((uintptr_t) post_aligned % sizeof(uint64_t) == 0);
    uint32_t rem = (uint32_t) ((uint8_t*) post_aligned - post_dead_zone);

    for(uint32_t i = 0; i < allocation->pre_dead_zone/sizeof(uint64_t); i++)
        if(pre_aligned[i] != _DEBUG_ALLOCATOR_MAGIC_NUM64)
            goto has_fault;
            
    for(uint32_t i = 0; i < rem; i++)
        if(pre_dead_zone[i] != _DEBUG_ALLOCATOR_MAGIC_NUM8)
            goto has_fault;

    for(uint32_t i = 0; i < allocation->post_dead_zone/sizeof(uint64_t); i++)
        if(post_aligned[i] != _DEBUG_ALLOCATOR_MAGIC_NUM64)
            goto has_fault;

    return;

    //slow path 
    has_fault:
    for(int zone_i = 0; zone_i < 2; zone_i ++) {
        uint8_t* zone = zone_i ? pre_dead_zone : post_dead_zone;
        isize zone_size = zone_i ? allocation->pre_dead_zone : allocation->post_dead_zone;

        //find the exact first postion of override
        isize override_pos = -1;
        if(zone_i)
        {
            for(isize i = zone_size; i-- > 0; )
                if(zone[i] != _DEBUG_ALLOCATOR_MAGIC_NUM8) {
                    override_pos = i;
                    break;
                }
        }
        else
        {
            for(isize i = 0; i < zone_size; i++)
                if(zone[i] != _DEBUG_ALLOCATOR_MAGIC_NUM8) {
                    override_pos = i;
                    break;
                }
        }

        //if this side has an overwrite
        if(override_pos != -1)
        {
            //make a textual view of the overwrite
            isize text_cap = zone_size*3 + 1;
            char* text_hex = (char*) malloc(text_cap);
            char* text_ascii = (char*) malloc(text_cap);
            const char* val_to_hex = "0123456789abcdef";

            isize wh = 0;
            isize wa = 0;
            for(isize i = 0; i < zone_size; i++)
            {
                uint8_t val = zone[i];
                text_hex[wh++] = val_to_hex[val >> 4];  
                text_hex[wh++] = val_to_hex[val & 0x7];  
                text_hex[wh++] = ' ';

                bool is_ascii = 33 <= val && val <= 126;
                text_ascii[wa++] = is_ascii ? ' '        : '?';
                text_ascii[wa++] = is_ascii ? (char) val : '?';
                text_ascii[wa++] = ' ';
            }

            //remove last space and null terminate
            if(wh > 0) wh -= 1;
            if(wa > 0) wa -= 1;

            text_hex[wh] = '\0';
            text_ascii[wa] = '\0';
            ASSERT(wh < text_cap && wa < text_cap);
        
            //print out the 
            const char* name = self->options.name;
            if(zone_i) {
                LOG_FATAL("DEBUG", "debug allocator%s%s found write %lliB before the beginning of allocation 0x%016llx. Printing view of dead zone:", 
                    name ? "name: " : "", name, (lli) (zone_size - override_pos), (llu) allocation->ptr);
            }
            else {
                LOG_FATAL("DEBUG", "debug allocator%s%s found write %lliB after the end of allocation 0x%016llx. Printing view of dead zone:", 
                    name ? "name: " : "", name, (lli) override_pos, (llu) allocation->ptr);
            }

            LOG_FATAL("DEBUG", "hex view:   %s", text_hex);
            LOG_FATAL("DEBUG", "ascii view: %s", text_ascii);

            free(text_hex);
            free(text_ascii);
            
            _debug_allocator_panic(self, allocation->ptr, allocation, 0, zone_i ? "overwrite before block" : "overwrite after block");
        }
    }
}

INTERNAL Debug_Allocation* _debug_allocation_get_closest(const Debug_Allocator* self, void* ptr, isize* dist_or_null)
{
    Debug_Allocation* closest = NULL;
    isize closest_dist = INT64_MAX;
    
    if(self->allocation_hash)
        for(isize i = 0; i < _DEBUG_ALLOC_HASH_SIZE; i++) {
            for(Debug_Allocation* curr = self->allocation_hash[i]; curr; curr = curr->next) {
                isize dist = (ptrdiff_t) curr->ptr - (ptrdiff_t) ptr;
                if(dist < 0)
                    dist = -dist;
            
                if(closest_dist > dist) {
                    closest_dist = dist;
                    closest = curr;
                }
            }
        }

    if(dist_or_null)
        *dist_or_null = (ptrdiff_t) ptr - (ptrdiff_t) closest;
    return closest;
}

EXTERNAL void debug_allocator_test_all_allocations(const Allocator* self_alloc)
{
    if(self_alloc == NULL || *self_alloc != debug_allocator_func)
        return;

    const Debug_Allocator* self = (const Debug_Allocator*) (void*) self_alloc;
    isize size_sum = 0;
    if(self->allocation_hash)
        for(isize i = 0; i < _DEBUG_ALLOC_HASH_SIZE; i++) {
            for(Debug_Allocation* curr = self->allocation_hash[i]; curr; curr = curr->next) {
                _debug_allocation_test_dead_zones(self, curr);
                size_sum += curr->size;
            }
        }

    TEST(size_sum == self->bytes_allocated);
    TEST(size_sum <= self->max_bytes_allocated);
}

EXTERNAL void debug_allocator_test_invariants(const Debug_Allocator* self)
{
    //TODO: test hash invariants - well linked, can find all...
    debug_allocator_test_all_allocations(self->alloc);
    TEST(self->allocation_count >= self->deallocation_count && self->deallocation_count >= 0);
    TEST(self->reallocation_count >= 0);
    TEST(0 <= self->bytes_allocated && self->bytes_allocated <= self->max_bytes_allocated);
    TEST((self->alive_count == 0) == (self->bytes_allocated == 0));
}

INTERNAL void _debug_allocator_check_invariants(const Debug_Allocator* self)
{
    (void) self;
    #ifdef DO_ASSERTS_SLOW
        debug_allocator_test_invariants(self);
    #endif
}

EXTERNAL const Debug_Allocation* debug_allocator_get_allocation(const Debug_Allocator* self, void* ptr)
{
    return _debug_allocator_find_allocation(self, ptr);
} 

EXTERNAL void debug_allocator_test_allocation(const Allocator* self_alloc, void* user_ptr)
{
    if(self_alloc == NULL || *self_alloc != debug_allocator_func)
        return;

    const Debug_Allocator* self = (const Debug_Allocator*) (void*) self_alloc;
    const Debug_Allocation* found = debug_allocator_get_allocation(self, user_ptr);
    TEST(found != NULL);
    _debug_allocation_test_dead_zones(self, found);
}

INTERNAL int _debug_allocation_alloc_id_compare(const void* a_, const void* b_)
{
    Debug_Allocation* a = (Debug_Allocation*) a_;
    Debug_Allocation* b = (Debug_Allocation*) b_;
    return (a->id > b->id) - (a->id < b->id);  
}

EXTERNAL Debug_Allocation* debug_allocator_get_alive_allocations(const Debug_Allocator* self, Allocator* alloc_result_from, isize max_entries, isize* count)
{
    _debug_allocator_check_invariants(self);
    isize alloc_count = max_entries;
    if(alloc_count < 0)
        alloc_count = self->alive_count;
    if(alloc_count > self->alive_count)
        alloc_count = self->alive_count;
    
    Debug_Allocation* allocations = (Debug_Allocation*) allocator_allocate(alloc_result_from, sizeof(Debug_Allocation)*alloc_count, DEF_ALIGN);
    isize curr_count = 0;
    
    if(self->allocation_hash)
        for(isize i = 0; i < _DEBUG_ALLOC_HASH_SIZE; i++) {
            for(Debug_Allocation* curr = self->allocation_hash[i]; curr; curr = curr->next) {
                allocations[curr_count++] = *curr;
                if(curr_count >= alloc_count)
                    break;
            }
        }
    
    qsort(allocations, alloc_count, sizeof *allocations, _debug_allocation_alloc_id_compare);
    *count = alloc_count;
    return allocations;
}

#include <time.h>
EXTERNAL void debug_allocator_print_alive_allocations(const char* name, Log_Type log_type, const Debug_Allocator* self, isize print_max, uint32_t flags)
{
    isize alive_count = 0;
    Debug_Allocation* alive = debug_allocator_get_alive_allocations(self, self->internal_alloc, print_max, &alive_count);
    LOG(log_type, name, "printing alive allocations (%lli) below:", (lli)alive_count);
    
    //calculate needed width
    int max_id_width = 0;
    isize max_id = alive_count > 0 ? alive[alive_count - 1].id : 0;
    for(isize i = max_id; i; i /= 10)
        max_id_width += 1;

    for(isize i = 0; i < alive_count; i++)
    {
        Debug_Allocation curr = alive[i];

        char time_buffer[16] = "";
        if(flags & DEBUG_ALLOC_PRINT_ALIVE_TIME) {
            time_t epoch_time_secs = curr.time / 1000000;
            struct tm local_time = *localtime(&epoch_time_secs);
            snprintf(time_buffer, sizeof time_buffer, " time:%02i:%02i:%02i", local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
        }

        LOG(log_type, name, "[%0*lli]: size:%10s ptr:0x%016llx align:%lli%s",
            max_id_width, (lli) curr.id, format_bytes(curr.size).data, (llu) curr.ptr, (lli) curr.align, time_buffer);
    
        if((flags & DEBUG_ALLOC_PRINT_ALIVE_CALLSTACK) && curr.call_stack_count > 0) 
            log_captured_callstack(log_type, name, debug_allocation_get_callstack(&curr), curr.call_stack_count);
    }

    allocator_deallocate(self->internal_alloc, alive, sizeof(Debug_Allocation)*alive_count, DEF_ALIGN);
}
#endif
