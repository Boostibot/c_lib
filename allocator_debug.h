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
//  +------------------------------------------------+
//  |   alive_hash                                   |                        +---------------------------------+
//  | +-------------+                                |   o------------------->| call stack | dead | USER | dead |
//  | | 0           |                                |   |                    +---------------------------------+
//  | | 0           |                                |   |        
//  | | 0x8157190a0 | <--o       alive (allocations) |   |              
//  | | 0           |    |    +--------------------+ |   |
//  | | 0           |    o--> | {ptr, size, align} | |---o              *BLOCK*: allocated block from parent allocator 
//  | | ...         |         | {ptr, size, align} | |       +------------------------------------------------------------+
//  | | 0x140144100 | <-----> | {ptr, size, align} | |------>| call stack | [extra] dead zone | USER DATA | D | dead zone |
//  | | 0           |         | {ptr, size, align} | |       |--------------------------------|---------------|-----------+
//  | +-------------+         +--------------------+ |       |                                |               L 8 aligned
//  +------------------------------------------------+       L 8 aligned                      L aligned to user specified align
//
// Within each *BLOCK* are contained the properly sized user data along with some header containing meta
// data about the allocation (is used to validate arguments and facilitate debugging), dead zones which 
// are filled with 0x55 bytes (0 and 1 alternating in binary), and some unspecified padding bytes which 
// may occur due to overaligned requirements for user data.
// 
// Prior to each access the block address is looked up in the alive_allocations_hash. If it is found
// the dead zones and header is checked for validity (invalidity would indicate overwrites). Only then
// any allocation/deallocation takes place.

#include "allocator.h"
#include <string.h>
#include <stdlib.h>

typedef struct Debug_Allocation {
    u64 hash;
    void* ptr;
    isize size;
    u16 align;
    u16 pre_dead_zone;
    u16 post_dead_zone;
    u16 call_stack_count;
    u64 time;
    u64 id;
} Debug_Allocation;

//A simple robin-hood hashing hash set of ptrs.
//May not store NULL pointer.
typedef struct Robin_Hash {
    Allocator* alloc;
    Debug_Allocation* entries;
    uint32_t count;
    uint32_t capacity; 
} Robin_Hash;

EXTERNAL void robin_hash_init(Robin_Hash* set, Allocator* alloc);
EXTERNAL void robin_hash_deinit(Robin_Hash* set);
EXTERNAL bool robin_hash_reserve(Robin_Hash* set, isize count);
EXTERNAL isize robin_hash_find(const Robin_Hash* table, uint64_t hash);
INTERNAL bool robin_hash_set(Robin_Hash* table, Debug_Allocation added, isize* out);
EXTERNAL bool robin_hash_remove_at(Robin_Hash* table, uint64_t hash, isize found);
EXTERNAL void robin_hash_test_invariants(const Robin_Hash* set);

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

typedef struct Debug_Allocator {
    Allocator alloc[1];
    Allocator* parent;
    
    Robin_Hash alive;

    bool is_within_allocation; 
    Debug_Allocator_Options options; //can be changed at will

    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    u64 last_id;
    Allocator_Set allocator_backup;
} Debug_Allocator;

#define DEBUG_ALLOC_LARGE_DEAD_ZONE     1  // dead_zone_size = 64 
#define DEBUG_ALLOC_NO_DEAD_ZONE        2  // dead_zone_size = 0 
#define DEBUG_ALLOC_CAPTURE_CALLSTACK   4  // capture_stack_frames_count = 16 
#define DEBUG_ALLOC_LEAK_CHECK          8  // do_deinit_leak_check = true 
#define DEBUG_ALLOC_CONTINUOUS          16 // do_continual_checks = true 
#define DEBUG_ALLOC_PRINT               32 // do_printing = true 
#define DEBUG_ALLOC_USE                 64 // do_set_as_default = true

EXTERNAL Debug_Allocator debug_allocator_make(Allocator* parent, u64 flags); //convenience wrapper for debug_allocator_init
EXTERNAL void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, Debug_Allocator_Options options);
EXTERNAL void debug_allocator_deinit(Debug_Allocator* allocator);

EXTERNAL void** debug_allocation_get_callstack(const Debug_Allocation* alloc);

EXTERNAL bool debug_allocator_get_allocation(const Debug_Allocator* allocator, void* ptr, Debug_Allocation* out_allocation); 
EXTERNAL void debug_allocator_print_alive_allocations(const char* name, Log_Type log_type, const Debug_Allocator* allocator, isize print_max, bool print_with_callstack);
EXTERNAL void debug_allocator_test_all_blocks(const Debug_Allocator* self);
EXTERNAL void debug_allocator_test_block(const Debug_Allocator* self, void* user_ptr);
EXTERNAL void debug_allocator_test_invariants(const Debug_Allocator* self);

EXTERNAL void* debug_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
EXTERNAL Allocator_Stats debug_allocator_get_stats(Allocator* self_);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_DEBUG_ALLOCATOR)) && !defined(MODULE_HAS_IMPL_DEBUG_ALLOCATOR)
#define MODULE_HAS_IMPL_DEBUG_ALLOCATOR

#define _DEBUG_ALLOCATOR_FILL_NUM8   0x88
#define _DEBUG_ALLOCATOR_MAGIC_NUM8  0x55
#define _DEBUG_ALLOCATOR_MAGIC_NUM64 0x5555555555555555ull

EXTERNAL void debug_allocator_init_custom(Debug_Allocator* debug, Allocator* parent, Debug_Allocator_Options options)
{
    debug_allocator_deinit(debug);
    robin_hash_init(&debug->alive, parent);
    
    debug->parent = parent;
    debug->options = options;
    debug->options.pre_dead_zone_size = DIV_CEIL(debug->options.pre_dead_zone_size, sizeof(uint64_t))*sizeof(uint64_t);
    debug->options.post_dead_zone_size = DIV_CEIL(debug->options.post_dead_zone_size, sizeof(uint64_t))*sizeof(uint64_t);
    debug->alloc[0].func = debug_allocator_func;
    debug->alloc[0].get_stats = debug_allocator_get_stats;

    if(options.do_set_as_default)
        debug->allocator_backup = allocator_set_default(debug->alloc);
}

EXTERNAL Debug_Allocator debug_allocator_make(Allocator* parent, u64 flags)
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
    debug_allocator_init_custom(&allocator, parent, options);
    return allocator;
}

EXTERNAL void** debug_allocation_get_callstack(const Debug_Allocation* alloc)
{
    u8* ptr = (u8*) alloc->ptr - alloc->pre_dead_zone - alloc->call_stack_count*sizeof(void*);
    return (void**) ptr;
}

EXTERNAL void* debug_allocation_get_block(const Debug_Allocation* alloc)
{
    return debug_allocation_get_callstack(alloc) - alloc->call_stack_count;
}

INTERNAL void _debug_allocator_panic(const Debug_Allocator* self, void* user_ptr, const Debug_Allocation* allocation, isize dist, const char* panic_reason)
{
    if(allocation) {
        if(dist == 0)
            LOG_FATAL("DEBUG", "Printing allocation info for ptr 0x%08llx:", (llu) allocation->ptr);
        else    
            LOG_FATAL("DEBUG", "Printing closest allocation to ptr 0x%08llx. Allocation 0x%08llx (%lliB away):", (llu) user_ptr, (llu) allocation->ptr, (lli) dist);
        LOG_FATAL(">DEBUG", "size : %s (%lliB)", format_bytes(allocation->size).data, (lli) allocation->size);
        LOG_FATAL(">DEBUG", "align: %lli", (lli) allocation->align);
        LOG_FATAL(">DEBUG", "id   : %lli", (lli) allocation->id);
        LOG_FATAL(">DEBUG", "time : %lli", (lli) allocation->time);
        if(allocation->call_stack_count) {
            void** callstack = debug_allocation_get_callstack(allocation);
            LOG_FATAL(">DEBUG", "callstack:");
            log_captured_callstack(LOG_FATAL, ">>DEBUG", callstack, allocation->call_stack_count);
        }
    }

    PANIC("debug allocator%s%s reported failure '%s'", self->options.name ? "name: " : "", self->options.name, panic_reason);
}

EXTERNAL void _debug_allocation_test_dead_zones(const Debug_Allocator* self, const Debug_Allocation* allocation)
{
    u8* pre_dead_zone = (u8*) allocation->ptr - allocation->pre_dead_zone;
    u8* post_dead_zone = (u8*) allocation->ptr + allocation->size;

    uint64_t* pre_aligned = (uint64_t*) pre_dead_zone;
    uint64_t* post_aligned = (uint64_t*) align_forward(post_dead_zone, sizeof(uint64_t));

    ASSERT((uintptr_t) pre_aligned % sizeof(uint64_t) == 0);
    ASSERT((uintptr_t) post_aligned % sizeof(uint64_t) == 0);
    for(u32 i = 0; i < allocation->pre_dead_zone/sizeof(uint64_t); i++)
        if(pre_aligned[i] != _DEBUG_ALLOCATOR_MAGIC_NUM64)
            goto has_fault;
            
    u32 rem = (u32) ((u8*) post_aligned - post_dead_zone);
    for(u32 i = 0; i < rem; i++)
        if(pre_dead_zone[i] != _DEBUG_ALLOCATOR_MAGIC_NUM8)
            goto has_fault;

    for(u32 i = 0; i < allocation->post_dead_zone/sizeof(uint64_t); i++)
        if(post_aligned[i] != _DEBUG_ALLOCATOR_MAGIC_NUM64)
            goto has_fault;

    return;

    //slow path 
    has_fault:
    for(int zone_i = 0; zone_i < 2; zone_i ++) {
        u8* zone = zone_i ? pre_dead_zone : post_dead_zone;
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
            for(isize i = 0; i < zone_size;)
            {
                u8 val = zone[i];
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
                LOG_FATAL("DEBUG", "debug allocator%s%s found write %lliB before the beginning of allocation 0x%08llx. Printing view of dead zone:", 
                    name ? "name: " : "", name, (lli) (zone_size - override_pos), (llu) allocation->ptr);
            }
            else {
                LOG_FATAL("DEBUG", "debug allocator%s%s found write %lliB after the end of allocation 0x%08llx. Printing view of dead zone:", 
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

EXTERNAL Debug_Allocation* _debug_allocation_get_closest(const Debug_Allocator* self, void* ptr, isize* dist_or_null)
{
    Debug_Allocation* closest = NULL;
    isize closest_dist = INT64_MAX;

    for(isize i = 0; i < self->alive.capacity; i++)
    {
        Debug_Allocation* curr = &self->alive.entries[i];
        if(curr->ptr != NULL) {
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

EXTERNAL void debug_allocator_test_all_blocks(const Debug_Allocator* self)
{
    isize size_sum = 0;
    for(isize i = 0; i < self->alive.capacity; i++)
    {
        Debug_Allocation* curr = &self->alive.entries[i];
        if(curr->ptr != NULL) {
            _debug_allocation_test_dead_zones(self, curr);
            size_sum += curr->size;
        }
    }

    TEST(size_sum == self->bytes_allocated);
    TEST(size_sum <= self->max_bytes_allocated);
}

EXTERNAL void debug_allocator_test_invariants(const Debug_Allocator* self)
{
    robin_hash_test_invariants(&self->alive);
    debug_allocator_test_all_blocks(self);
    TEST(self->allocation_count >= self->deallocation_count && self->deallocation_count >= 0);
    TEST(self->reallocation_count >= 0);
    TEST(0 <= self->bytes_allocated && self->bytes_allocated <= self->max_bytes_allocated);
    TEST((self->alive.count == 0) == (self->bytes_allocated == 0));
}

INTERNAL void _debug_allocator_check_invariants(const Debug_Allocator* self)
{
    (void) self;
    #ifdef DO_ASSERTS_SLOW
        debug_allocator_test_invariants(self);
    #endif
}

EXTERNAL void debug_allocator_deinit(Debug_Allocator* self)
{
    if(self->alive.count && self->options.do_deinit_leak_check)
    {
        const char* name = self->options.name;
        LOG_FATAL("DEBUG", "debug allocator%s%s leaking memory. Printing all allocations (%lli) below:", 
                name ? "name: " : "", name, (lli) self->alive.count);
        debug_allocator_print_alive_allocations(">DEBUG", LOG_FATAL, self, -1, true);
        PANIC("debug allocator%s%s reported failure '%s'", name ? "name: " : "", name, "memory leaked");
    }

    for(isize i = 0; i < self->alive.capacity; i++)
    {
        Debug_Allocation* curr = &self->alive.entries[i];
        if(curr->ptr != NULL) {
            _debug_allocation_test_dead_zones(self, curr);

            void* block = debug_allocation_get_block(curr);
            isize total_size = sizeof(void*)*curr->call_stack_count + curr->pre_dead_zone + curr->size + curr->post_dead_zone;
            allocator_deallocate(self->parent, block, total_size, DEF_ALIGN);
        }
    }

    allocator_set(self->allocator_backup);
    robin_hash_deinit(&self->alive);
    memset(self, 0, sizeof *self);
}

EXTERNAL uint64_t _debug_alloc_ptr_hash(void* ptr) 
{
    uint64_t x = (uint64_t) ptr; 
    x = (x ^ (x >> 31) ^ (x >> 62)) * (uint64_t) 0x319642b2d24d8ec3;
    x = (x ^ (x >> 27) ^ (x >> 54)) * (uint64_t) 0x96de1b173f119089;
    x = x ^ (x >> 30) ^ (x >> 60);
    return x;
}

EXTERNAL void* debug_allocator_func(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
{
    Debug_Allocator* self = (Debug_Allocator*) (void*) self_;
    PROFILE_START();
    _debug_allocator_check_invariants(self);
    
    const char* name = self->options.name;
    if(new_size < 0 || old_size < 0 || is_power_of_two(align) == false || align >= UINT16_MAX) {
        LOG_FATAL("DEBUG", "debug allocator%s%s provided with invalid params new_size:%lli old_size:%lli align:%lli ", 
            name ? "name: " : "", name, (llu) new_size, (llu) old_size, (llu) align);
        PANIC("debug allocator%s%s reported failure '%s'", name ? "name: " : "", name, "invalid size or align parameter");
    }
    isize preamble_size = self->options.pre_dead_zone_size + self->options.capture_stack_frames_count*sizeof(void*);
    isize postamble_size = self->options.post_dead_zone_size;

    isize new_total_size = new_size ? preamble_size + postamble_size + align + new_size : 0;
    isize old_total_size = old_size ? preamble_size + postamble_size + align + old_size : 0;

    uint64_t old_hash = 0;
    isize old_found = 0;
    void* old_block_start = NULL;
    Debug_Allocation* old_info = NULL;

    //Check old_ptr if any for correctness
    if(old_ptr != NULL)
    {
        old_hash = _debug_alloc_ptr_hash(old_ptr);
        old_found = robin_hash_find(&self->alive, old_hash); 
        if(old_found == -1) {
            LOG_FATAL("DEBUG", "%s%s%s no allocation at 0x%08llx", 
                name ? "name: " : "", name, (llu) old_ptr);

            isize closest_dist = 0;
            Debug_Allocation* closest = _debug_allocation_get_closest(self, old_ptr, &closest_dist);
            _debug_allocator_panic(self, old_ptr, closest, closest_dist, "invalid pointer");
        }

        old_info = &self->alive.entries[old_found];

        if(old_info->size != old_size) {
            LOG_FATAL("DEBUG", "debug allocator%s%s size does not match for allocation 0x%08llx: given:%lli actual:%lli", 
                name ? "name: " : "", name, (llu) old_ptr, (lli) old_size, (lli) old_info->size);
            _debug_allocator_panic(self, old_ptr, old_info, 0, "invalid size parameter");
        }
                
        if(old_info->align != align) {
            LOG_FATAL("DEBUG", "debug allocator%s%s align does not match for allocation 0x%08llx: given:%lli actual:%lli", 
                name ? "name: " : "", name, (llu) old_ptr, (lli) align, (lli) old_info->align);
            _debug_allocator_panic(self, old_ptr, old_info, 0, "invalid align parameter");
        }

        _debug_allocation_test_dead_zones(self, old_info);
        old_block_start = debug_allocation_get_block(old_info);
    }
    else
    {
        if(old_size != 0) {
            LOG_FATAL("DEBUG", "debug allocator%s%s given NULL allocation pointer but size of %lliB", 
                name ? "name: " : "", name, (llu) old_size);
                
            PANIC("debug allocator%s%s reported failure '%s'", name ? "name: " : "", name, "invalid size parameter");
        }
    }

    u8* new_block_ptr = (u8*) allocator_try_reallocate(self->parent, new_total_size, old_block_start, old_total_size, DEF_ALIGN, error);
    u8* new_ptr = NULL;

    //if hasnt failed
    if(new_block_ptr != NULL || new_size == 0)
    {
        //if previous block existed remove it from control structures
        if(old_ptr != NULL)
            robin_hash_remove_at(&self->alive, old_hash, old_found);

        //if allocated/reallocated (new block exists)
        if(new_size != 0)
        {
            new_ptr = (u8*) align_forward(new_block_ptr + preamble_size, align);

            void** callstack = (void**) (void*) new_block_ptr;
            uint8_t* pre_dead_zone = (uint8_t*) (void*) (callstack + self->options.capture_stack_frames_count); 
            uint8_t* post_dead_zone = new_ptr + new_size;
             
            Debug_Allocation new_alloc = {0};
            new_alloc.ptr = new_ptr;
            new_alloc.hash = _debug_alloc_ptr_hash(new_ptr);
            new_alloc.align = (u16) align;
            new_alloc.size = new_size;
            new_alloc.call_stack_count = (u16) self->options.capture_stack_frames_count;
            new_alloc.id = self->last_id++;
            new_alloc.pre_dead_zone = (u16) (new_ptr - pre_dead_zone);
            new_alloc.post_dead_zone = (u16) (new_block_ptr + new_total_size - post_dead_zone);
            new_alloc.time = platform_epoch_time();

            if(new_alloc.call_stack_count > 0)
                platform_capture_call_stack(callstack, new_alloc.call_stack_count, 1);

            memset(pre_dead_zone,  _DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_alloc.pre_dead_zone);
            //memset(new_ptr,        0, (size_t) new_size);
            //memset(new_ptr,        _DEBUG_ALLOCATOR_FILL_NUM8, (size_t) new_size); //?!?
            memset(post_dead_zone, _DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_alloc.post_dead_zone);
            
            isize added_index = 0;
            bool was_added = robin_hash_set(&self->alive, new_alloc, &added_index);
            ASSERT(was_added);
            
            #ifdef DO_ASSERTS_SLOW
                debug_allocator_test_block(self, new_ptr);
            #endif
        }

        self->bytes_allocated += new_size - old_size;
        self->max_bytes_allocated = MAX(self->max_bytes_allocated, self->bytes_allocated);
        if(self->options.do_printing && self->is_within_allocation == false)
        {
            self->is_within_allocation = true;
            LOG_DEBUG("MEMORY", "size %6lli -> %-6lli ptr: 0x%08llx -> 0x%08llx align: %lli ",
                (lli) old_size, (lli) new_size, (lli) old_ptr, (lli) new_ptr, (lli) align);
            self->is_within_allocation = false;
        }

        if(old_ptr == NULL)
            self->allocation_count += 1;
        else if(new_size == 0)
            self->deallocation_count += 1;
        else
            self->reallocation_count += 1;
    }
    
    #ifdef DO_ASSERTS_SLOW
        bool do_debug = true;
    #else
        bool do_debug = false;
    #endif
    if(do_debug)
        _debug_allocator_check_invariants(self);
    else if(self->options.do_continual_checks)
        debug_allocator_test_all_blocks(self);

    PROFILE_STOP();
    return new_ptr;
}

EXTERNAL Allocator_Stats debug_allocator_get_stats(Allocator* self_)
{
    Debug_Allocator* self = (Debug_Allocator*) (void*) self_;
    Allocator_Stats out = {0};
    out.type_name = "Debug_Allocator";
    out.name = self->options.name;
    out.parent = self->parent;
    out.max_bytes_allocated = self->max_bytes_allocated;
    out.bytes_allocated = self->bytes_allocated;
    out.allocation_count = self->allocation_count;
    out.deallocation_count = self->deallocation_count;
    out.reallocation_count = self->reallocation_count;
    out.is_capable_of_free_all = true;
    out.is_capable_of_resize = true;
    out.is_growing = true;
    out.is_top_level = false;
    return out;
}

EXTERNAL bool debug_allocator_get_allocation(const Debug_Allocator* self, void* ptr, Debug_Allocation* out)
{
    isize found = robin_hash_find(&self->alive, _debug_alloc_ptr_hash(ptr));
    if(found == -1)
        return false;

    *out = self->alive.entries[found];
    return true;
} 

EXTERNAL void debug_allocator_test_block(const Debug_Allocator* self, void* user_ptr)
{
    isize found = robin_hash_find(&self->alive, _debug_alloc_ptr_hash(user_ptr));
    TEST(found >= 0);
    _debug_allocation_test_dead_zones(self, &self->alive.entries[found]);
}

INTERNAL int _debug_allocation_alloc_id_compare(const void* a_, const void* b_)
{
    Debug_Allocation* a = (Debug_Allocation*) a_;
    Debug_Allocation* b = (Debug_Allocation*) b_;
    return (a->id > b->id) - (a->id < b->id);  
}

EXTERNAL Debug_Allocation* debug_allocator_get_alive_allocations(const Debug_Allocator* self, isize max_entries, isize* count)
{
    _debug_allocator_check_invariants(self);
    isize alloc_count = max_entries;
    if(alloc_count < 0)
        alloc_count = self->alive.count;
    if(alloc_count > self->alive.count)
        alloc_count = self->alive.count;
    
    Debug_Allocation* allocations = (Debug_Allocation*) allocator_allocate(self->parent, sizeof(Debug_Allocation)*alloc_count, DEF_ALIGN);
    isize curr_count = 0;
    for(isize i = 0; i < self->alive.capacity; i++) {
        Debug_Allocation* curr = &self->alive.entries[i];
        if(curr->ptr != NULL) {
            allocations[curr_count++] = *curr;
            if(curr_count >= alloc_count)
                break;
        }
    }
    
    qsort(allocations, alloc_count, sizeof *allocations, _debug_allocation_alloc_id_compare);
    *count = alloc_count;
    return allocations;
}

EXTERNAL void debug_allocator_print_alive_allocations(const char* name, Log_Type log_type, const Debug_Allocator* self, isize print_max, bool print_with_callstack)
{
    isize alive_count = 0;
    Debug_Allocation* alive = debug_allocator_get_alive_allocations(self, print_max, &alive_count);
    LOG(log_type, name, "printing ALIVE allocations (%lli) below:", (lli)alive_count);
    for(isize i = 0; i < alive_count; i++)
    {
        Debug_Allocation curr = alive[i];
        LOG(log_type, name, "[%3lli]: ptr:0x%08llx size:%5.2lfKB (%lliB) align:%lli",
            (lli) i, (llu) curr.ptr, (double)curr.size/1024, (lli) curr.size, (lli) curr.align);
    
        if(print_with_callstack && curr.call_stack_count > 0) 
            log_captured_callstack(log_type, name, debug_allocation_get_callstack(&curr), curr.call_stack_count);
    }

    allocator_deallocate(self->parent, alive, sizeof(Debug_Allocation)*alive_count, DEF_ALIGN);
}



//Ptr set =====================

EXTERNAL void robin_hash_init(Robin_Hash* set, Allocator* alloc)
{
    robin_hash_deinit(set);
    set->alloc = alloc;
}
EXTERNAL void robin_hash_deinit(Robin_Hash* set)
{
    if(set->entries && set->alloc)
        allocator_deallocate(set->alloc, set->entries, set->capacity*sizeof(Debug_Allocation), sizeof(void*));
    memset(set, 0, sizeof *set);
}

INTERNAL bool _robin_hash_add(Debug_Allocation* entries, uint64_t capacity, Debug_Allocation added, isize* out)
{
    ASSERT(added.hash != 0);

    uint64_t mask = capacity - 1;
    uint64_t i = added.hash & mask;
    uint64_t stored_into = (uint64_t) -1;
    
    Debug_Allocation depositing = added;
    for(uint64_t dist = 0;; dist++) {
        ASSERT(dist < capacity);

        Debug_Allocation* entry = &entries[i];
        if(entry->hash == depositing.hash) {
            *entry = depositing;
            *out = i;
            return false;
        }

        if(entry->hash == 0) {
            *entry = depositing;
            *out = ~stored_into ? stored_into :  i;
            return true;
        }

        //If we are further then the current entry,
        // store into that entry and continue probing with 
        // that entry's value
        uint64_t entry_dist = (i - entry->hash) & mask;
        if(entry_dist < dist) {
            Debug_Allocation temp = *entry;
            *entry = depositing;
            depositing = temp;
            dist = entry_dist;
            stored_into = i;
        }
        
        i = (i + 1) & mask;
    }
}

EXTERNAL bool robin_hash_reserve(Robin_Hash* table, isize count)
{   
    uint64_t needed_cap = (uint64_t)count*4/3;
    if(table->capacity > needed_cap)
        return false;

    #ifdef DO_ASSERTS_SLOW
        robin_hash_test_invariants(table);
    #endif
    uint64_t new_cap = 16;
    while(new_cap < needed_cap)
        new_cap *= 2;

    Debug_Allocation* new_entries = (Debug_Allocation*) allocator_allocate(table->alloc, new_cap*sizeof(Debug_Allocation), sizeof(void*));
    memset(new_entries, 0, new_cap*sizeof(Debug_Allocation));
    
    for(uint32_t i = 0; i < table->capacity; i++)
    {
        Debug_Allocation* entry = &table->entries[i];
        if(entry->hash != 0) {
            isize index = 0;
            bool added = _robin_hash_add(new_entries, new_cap, *entry, &index);
            ASSERT(added);
        }
    }

    allocator_deallocate(table->alloc, table->entries, table->capacity*sizeof(Debug_Allocation), sizeof(void*));
    table->entries = new_entries;
    table->capacity = (uint32_t) new_cap;
    
    #ifdef DO_ASSERTS_SLOW
        robin_hash_test_invariants(table);
    #endif
    return true;
}

//returns index + 1 if found or 0 if not
EXTERNAL isize robin_hash_find(const Robin_Hash* table, uint64_t hash)
{
    if(table->count > 0) {
        uint64_t mask = table->capacity - 1;
        uint64_t i = hash & mask;
        for(uint64_t dist = 0;;) {
            ASSERT(dist++ < table->capacity);

            Debug_Allocation* entry = &table->entries[i];
            if(entry->hash == hash)
                return i;

            if(entry->hash == 0)
                break;

            i = (i + 1) & mask;
        }
    }
    
    return -1;
}

INTERNAL bool robin_hash_set(Robin_Hash* table, Debug_Allocation added, isize* out)
{
    robin_hash_reserve(table, table->count + 1);

    bool was_added = _robin_hash_add(table->entries, table->capacity, added, out);
    table->count += was_added;

    ASSERT_SLOW(robin_hash_find(table, added.hash) != -1);
    return was_added;
}

EXTERNAL bool robin_hash_remove_at(Robin_Hash* table, uint64_t hash, isize found)
{
    ASSERT_SLOW(found == robin_hash_find(table, hash));
    if(found < 0)
        return false;

    ASSERT(found < table->capacity);

    //keep shifting entries back until we find
    // one thats empty or thats precisely in its correct place
    uint64_t mask = table->capacity - 1;
    uint64_t curr = (uint64_t) found;
    for(uint64_t dist = 0;;) {
        ASSERT(dist++ < table->capacity);

        uint64_t next = (curr + 1) & mask;
        Debug_Allocation* entry = &table->entries[next];
        if(entry->hash == 0 || (entry->hash & mask) == next)
            break;

        table->entries[curr] = *entry; 
        curr = next;
    }

    //remove the entry we finished on
    // (shifting back leaves the last entry written twice)
    memset(table->entries + curr, 0, sizeof *table->entries);
    table->count -= 1;

    ASSERT_SLOW(robin_hash_find(table, hash) == -1);
    return true;
}

EXTERNAL void robin_hash_test_invariants(const Robin_Hash* table)
{
    if(table->capacity == 0) {
        TEST(table->count == 0);
        TEST(table->entries == NULL);
    }
    else {
        TEST(table->alloc);
        TEST(table->entries);
        TEST(is_power_of_two(table->capacity));
        TEST(table->count*4/3 <= table->capacity);

        uint32_t found_nonzero = 0;
        for(uint64_t i = 0; i < table->capacity; i++) {
            Debug_Allocation* entry = &table->entries[i];
            if(entry->hash == 0) 
                TEST(entry->ptr == 0);
            else
            {
                uint64_t found = robin_hash_find(table, entry->hash);
                TEST(found == i);
                found_nonzero += 1;
            }
        }
        TEST(table->count == found_nonzero);
        TEST(table->count == found_nonzero);
    }
}
#endif
