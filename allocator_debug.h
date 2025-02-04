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
// The high level allocator structure looks like the following:
//
//  Debug_Allocator
//  |-------------------------|
//  | Allocator* parent       |                        |------------------------------------------------------|
//  | ...                     |           0----------->| XXX | header | call stack | dead | USER | dead | XXX |
//  | alive_allocations:      |           |            |------------------------------------------------------|
//  | |-------------|         |           |
//  | | 0x8157190a0 | --------------------o
//  | | 0           |         |
//  | | 0           |         |                  *BLOCK*: allocated block from parent allocator 
//  | | ...         |         |       |------------------------------------------------------------------------|
//  | | 0x140144100 | --------------->| XXXX | header | call stack | dead zone | USER DATA | dead zone | XXXXX |
//  | | 0           |         |       ^----------------------------------------^-------------------------------|
//  | |_____________|         |       ^                                        ^ 
//  |_________________________|       L 8 aligned                              L aligned to user specified align
//
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

typedef struct Ptr_Set_Entry {
    uint64_t hash;
    void* ptr;
} Ptr_Set_Entry;

//A simple robin-hood hashing hash set of ptrs.
//May not store NULL pointer.
typedef struct Ptr_Set {
    Allocator* alloc;
    Ptr_Set_Entry* entries;
    uint32_t count;
    uint32_t capacity; 
} Ptr_Set;

EXTERNAL void ptr_set_init(Ptr_Set* set, Allocator* alloc);
EXTERNAL void ptr_set_deinit(Ptr_Set* set);
EXTERNAL bool ptr_set_reserve(Ptr_Set* set, isize count);
EXTERNAL bool ptr_set_has(Ptr_Set* set, void* ptr);
EXTERNAL uint64_t ptr_set_found(Ptr_Set* set, void* ptr);
EXTERNAL bool ptr_set_add(Ptr_Set* set, void* ptr);
EXTERNAL bool ptr_set_remove(Ptr_Set* set, void* ptr);
EXTERNAL bool ptr_set_remove_at(Ptr_Set* set, uint64_t found);
EXTERNAL void ptr_set_test_invariants(Ptr_Set* set);

typedef struct Debug_Allocator {
    Allocator alloc[1];
    Allocator* parent;
    const char* name;
    
    Ptr_Set alive_allocations;

    bool do_printing;            //whether each allocations/deallocations should be printed. can be safely toggled during lifetime
    bool do_continual_checks;     //whether it should checks all allocations for overwrites after each allocation.
                                 //incurs huge performance costs. can be safely toggled during runtime.
    bool do_deinit_leak_check;   //If the memory use on initialization and deinitialization does not match panics.
                                 //can be toggled during runtime. 
    bool is_within_allocation;   //prevents infinite recursion on logging functions
    isize capture_stack_frames_count; //number of stack frames to capture on each allocation. Defaults to 0.
                                   //If this is greater than 0 replaces passed source info in reports
    isize dead_zone_size;        //size in bytes of the dead zone. CANNOT be changed after creation!

    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    Allocator_Set allocator_backup;
} Debug_Allocator;

typedef struct Debug_Allocation {
    void* ptr;                     
    isize size;
    isize align;
    i64 epoch_time;
    void** call_stack;
} Debug_Allocation;

typedef struct Debug_Allocator_Options {
    const char* name;              //Optional name of this allocator for printing and debugging. 
    isize dead_zone_size;          //size in bytes of overwite prevention dead zone. 
    isize capture_stack_frames_count; //number of stack frames to capture on each allocation. 
    bool do_printing;           //prints all allocations/deallocation
    bool do_continual_checks;   //continually checks all allocations
    bool do_deinit_leak_check;  //If the memory use on initialization and deinitialization does not match panics.
    bool do_set_as_default;     
    bool _[4];
} Debug_Allocator_Options;

#define DEBUG_ALLOCATOR_CONTINUOUS          1  /* do_continual_checks = true */
#define DEBUG_ALLOCATOR_PRINT               2  /* do_printing = true */
#define DEBUG_ALLOCATOR_LARGE_DEAD_ZONE     4  /* dead_zone_size = 64 */
#define DEBUG_ALLOCATOR_NO_DEAD_ZONE        8  /* dead_zone_size = 0 */
#define DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK   16 /* do_deinit_leak_check = true */
#define DEBUG_ALLOCATOR_CAPTURE_CALLSTACK   32 /* capture_stack_frames_count = 16 */
#define DEBUG_ALLOCATOR_USE                 64

EXTERNAL void debug_allocator_init_custom(Debug_Allocator* allocator, Allocator* parent, Debug_Allocator_Options options);
EXTERNAL void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, u64 flags);
EXTERNAL void debug_allocator_deinit(Debug_Allocator* allocator);

EXTERNAL void* debug_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
EXTERNAL Allocator_Stats debug_allocator_get_stats(Allocator* self_);

EXTERNAL Debug_Allocation  debug_allocator_get_allocation(const Debug_Allocator* allocator, void* ptr); 
EXTERNAL Debug_Allocation* debug_allocator_get_alive_allocations(Allocator* result_alloc, const Debug_Allocator* allocator, isize get_max); //If get_max == -1 returns all
EXTERNAL void debug_allocator_print_alive_allocations(const char* name, Log_Type log_type, const Debug_Allocator allocator, isize print_max, bool print_with_callstack); 
EXTERNAL void debug_allocator_test_all_blocks(const Debug_Allocator* self);
EXTERNAL void debug_allocator_test_block(const Debug_Allocator* self, void* user_ptr);
EXTERNAL void debug_allocator_test_invariants(const Debug_Allocator* self);

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_DEBUG_ALLOCATOR)) && !defined(MODULE_HAS_IMPL_DEBUG_ALLOCATOR)
#define MODULE_HAS_IMPL_DEBUG_ALLOCATOR

#define DEBUG_ALLOCATOR_MAGIC_NUM8  (u8)  0x55

EXTERNAL void debug_allocator_init_custom(Debug_Allocator* debug, Allocator* parent, Debug_Allocator_Options options)
{
    debug_allocator_deinit(debug);
    ptr_set_init(&debug->alive_allocations, parent);

    options.dead_zone_size = DIV_CEIL(options.dead_zone_size, DEF_ALIGN)*DEF_ALIGN;
    debug->capture_stack_frames_count = options.capture_stack_frames_count;
    debug->do_deinit_leak_check = options.do_deinit_leak_check;
    debug->name = options.name;
    debug->do_continual_checks = options.do_continual_checks;
    debug->dead_zone_size = options.dead_zone_size;
    debug->do_printing = options.do_printing;
    debug->parent = parent;
    debug->alloc[0].func = debug_allocator_func;
    debug->alloc[0].get_stats = debug_allocator_get_stats;

    if(options.do_set_as_default)
        debug->allocator_backup = allocator_set_default(debug->alloc);
}

EXTERNAL void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, u64 flags)
{
    Debug_Allocator_Options options = {0};
    options.dead_zone_size = 16;
    if(flags & DEBUG_ALLOCATOR_CONTINUOUS)
        options.do_continual_checks = true;
    if(flags & DEBUG_ALLOCATOR_PRINT)
        options.do_printing = true;
    if(flags & DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK)
        options.do_deinit_leak_check = true;
    if(flags & DEBUG_ALLOCATOR_USE)
        options.do_set_as_default = true;
    if(flags & DEBUG_ALLOCATOR_LARGE_DEAD_ZONE)
        options.dead_zone_size = 64;
    if(flags & DEBUG_ALLOCATOR_NO_DEAD_ZONE)
        options.dead_zone_size = 0;
    if(flags & DEBUG_ALLOCATOR_CAPTURE_CALLSTACK)
        options.capture_stack_frames_count = 16;
    debug_allocator_init_custom(allocator, parent, options);
}

typedef struct Debug_Allocation_Header {
    isize size;
    u32 align;
    i32 offset; //offset to the start of the allocated block
    i64 index; //the index of 
    i64 epoch_time;
} Debug_Allocation_Header;

typedef struct Debug_Allocation_Info {
    Debug_Allocation_Header* header;
    void** call_stack;
    u8*    pre_dead_zone;
    u8*    post_dead_zone;
    u8*    block_start;
} Debug_Allocation_Info;

INTERNAL Debug_Allocation_Info _debug_allocator_get_block_info(const Debug_Allocator* self, void* user_ptr)
{
    Debug_Allocation_Info info = {0};
    info.pre_dead_zone = (u8*) user_ptr - self->dead_zone_size;
    info.call_stack = (void**) info.pre_dead_zone - self->capture_stack_frames_count;
    info.header = (Debug_Allocation_Header*) info.call_stack - 1;
    info.post_dead_zone = (u8*) user_ptr + info.header->size;
    info.block_start = (u8*) info.header - info.header->offset;
    return info;
}

INTERNAL void _debug_allocator_panic(const Debug_Allocator* self, void* user_ptr, const char* panic_reason)
{
    uint64_t found = ptr_set_find(&self->alive_allocations, user_ptr); 
    if(self->capture_stack_frames_count > 0 && found) {
        Debug_Allocation_Info info = _debug_allocator_get_block_info(self, user_ptr);
        LOG_FATAL("DEBUG", "Printing allocation call stack:");
        log_captured_callstack(LOG_FATAL, ">DEBUG", info.call_stack, self->capture_stack_frames_count);
    }
    PANIC("debug allocator%s%s reported failure '%s' because of allocation at 0x%08llx",
        self->name ? "name: " : "", self->name, panic_reason, (llu) user_ptr);
}

INTERNAL isize _debug_alloc_find_first_not(void* mem, int val, isize size)
{
    for(isize i = 0; i < size; i++)
        if(((u8*) mem)[i] != (u8) val)
            return i;

    return -1;
}

EXTERNAL void debug_allocator_test_block(const Debug_Allocator* self, void* user_ptr)
{
    Debug_Allocation_Info info = _debug_allocator_get_block_info(self, user_ptr);

    isize override_before = _debug_alloc_find_first_not(info.pre_dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, self->dead_zone_size);
    isize override_after = _debug_alloc_find_first_not(info.post_dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, self->dead_zone_size);
    if(override_before != -1) {
        LOG_FATAL("DEBUG", "debug allocator%s%s found write %lliB before the begining of allocation 0x%08llx", 
            self->name ? "name: " : "", self->name, (lli) (self->dead_zone_size - override_before), (llu) user_ptr);
        _debug_allocator_panic(self, user_ptr, "overwrite before block");
    }
    if(override_after != -1) {
        LOG_FATAL("DEBUG", "debug allocator%s%s found write %lliB past the end of allocation 0x%08llx", 
            self->name ? "name: " : "", self->name, (lli) override_after, (llu) user_ptr);
        _debug_allocator_panic(self, user_ptr, "overwrite after block");
    }

    bool corrupted_size = info.header->size <= 0 || info.header->size > (1ll << 48);
    bool corrupted_align = is_power_of_two(info.header->align) == false || ((size_t) user_ptr & ((size_t) info.header->align - 1)) != 0;
    bool corrupted_index = info.header->index < 0 || info.header->index > (1ll << 48);
    bool corrupted_offset = info.header->offset < 0 || info.header->offset >= info.header->align;
    bool corrupted_time = info.header->epoch_time < 0;
    if(corrupted_align || corrupted_size || corrupted_index || corrupted_offset) {
        LOG_FATAL("DEBUG", "debug allocator%s%s found corrupted header at least %lliB before the begining of allocation 0x%08llx:", 
            self->name ? "name: " : "", self->name, (lli) self->dead_zone_size, (llu) user_ptr);
        LOG_FATAL(">DEBUG", "size: %lli", (lli) info.header->size);
        LOG_FATAL(">DEBUG", "align: %lli", (lli) info.header->align);
        LOG_FATAL(">DEBUG", "index: %lli", (lli) info.header->index);
        LOG_FATAL(">DEBUG", "offset: %lli", (lli) info.header->offset);
        LOG_FATAL(">DEBUG", "time: %lli", (lli) info.header->epoch_time);
        _debug_allocator_panic(self, user_ptr, "overwrite before block - corrupted header");
    }
}

EXTERNAL void debug_allocator_test_all_blocks(const Debug_Allocator* self)
{
    //All alive allocations must be in hash
    isize size_sum = 0;
    for(isize i = 0; i < self->alive_allocations.count; i ++)
    {
        Ptr_Set_Entry curr = self->alive_allocations.entries[i];
        if(curr.ptr != NULL)
        {
            debug_allocator_test_block(self, curr.ptr);
            Debug_Allocation_Info info = _debug_allocator_get_block_info(self, curr.ptr);
            size_sum += info.header->size;
        }
    }

    TEST(size_sum == self->bytes_allocated);
    TEST(size_sum <= self->max_bytes_allocated);
}

EXTERNAL void debug_allocator_test_invariants(const Debug_Allocator* self)
{
    debug_allocator_test_all_blocks(self);
    ptr_set_test_invariants(&self->alive_allocations);
    TEST(self->allocation_count >= self->deallocation_count && self->deallocation_count >= 0);
    TEST(self->allocation_count >= self->reallocation_count && self->reallocation_count >= 0);
    TEST(0 <= self->bytes_allocated && self->bytes_allocated <= self->max_bytes_allocated);
    TEST(0 <= self->dead_zone_size && 0 <= self->capture_stack_frames_count);
    TEST((self->allocation_count == 0) == (self->bytes_allocated == 0));
    TEST(self->dead_zone_size/DEF_ALIGN*DEF_ALIGN == self->dead_zone_size 
        && "dead zone size must be a valid multiple of alignment"
        && "this is so that the pointers within the header will be properly aligned!");
}

INTERNAL void _debug_allocator_check_invariants(const Debug_Allocator* self)
{
    #ifdef DO_ASSERTS_SLOW
        debug_allocator_test_invariants(self);
    #endif
}

EXTERNAL void debug_allocator_deinit(Debug_Allocator* self)
{
    for(isize i = 0; i < self->alive_allocations.count; i++)
    {
        Ptr_Set_Entry entry = self->alive_allocations.entries[i];
        if(entry.ptr)
        {
            if(self->do_deinit_leak_check) {
                LOG_FATAL("DEBUG", "debug allocator%s%s leaking memory. Printing all allocations below:", 
                    self->name ? "name: " : "", self->name, (lli) self->dead_zone_size, (llu) entry.ptr);
                debug_allocator_print_alive_allocations(">DEBUG", LOG_FATAL, self, -1, true);
                _debug_allocator_panic(self, entry.ptr, "memory leaked");
            }

            debug_allocator_test_block(self, entry.ptr);
            Debug_Allocation_Info info = _debug_allocator_get_block_info(self, entry.ptr);
            isize total_size = sizeof(Debug_Allocation_Header) + 2*self->dead_zone_size + self->capture_stack_frames_count*sizeof(void*) + info.header->size;
            allocator_deallocate(self->parent, info.block_start, total_size, DEF_ALIGN);
        }
    }

    allocator_set(self->allocator_backup);
    ptr_set_deinit(&self->alive_allocations);
    memset(self, 0, sizeof *self);
}



EXTERNAL void* debug_allocator_func(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
{
    Debug_Allocator* self = (Debug_Allocator*) (void*) self_;
    PROFILE_START();
    _debug_allocator_check_invariants(self);
    
    if(new_size < 0 || old_size < 0 || is_power_of_two(align) == false) {
        LOG_FATAL("DEBUG", "debug allocator%s%s provided with invalid params new_size:%lli old_size:%lli align:%lli ", 
            self->name ? "name: " : "", self->name, (llu) new_size, (llu) old_size, (llu) align);
        _debug_allocator_panic(self, old_ptr, "invalid size or align parameter");
    }

    isize preamble_size = sizeof(Debug_Allocation_Header) + self->dead_zone_size + self->capture_stack_frames_count*sizeof(void*);
    isize postamble_size = self->dead_zone_size;

    isize new_total_size = new_size ? preamble_size + postamble_size + align + old_size : 0;
    isize old_total_size = old_size ? preamble_size + postamble_size + align + old_size : 0;

    uint64_t old_found = 0;
    Debug_Allocation_Info old_info = {0};
    Debug_Allocation_Info new_info = {0};

    //Check old_ptr if any for correctness
    if(old_ptr != NULL)
    {
        old_found = ptr_set_find(&self->alive_allocations, old_ptr); 
        if(old_found == 0) {
            LOG_FATAL("DEBUG", "%s%s%s no allocation at 0x%08llx", 
                self->name ? "name: " : "", self->name, (llu) old_ptr);
            _debug_allocator_panic(self, old_ptr, "invalid pointer");
        }

        debug_allocator_test_block(self, old_ptr);

        Debug_Allocation_Info old_info = _debug_allocator_get_block_info(self, old_ptr);
        if(old_info.header->size != old_size) {
            LOG_FATAL("DEBUG", "debug allocator%s%s size does not match for allocation 0x%08llx: given:%lli actual:%lli", 
                self->name ? "name: " : "", self->name, (llu) old_ptr, (lli) old_size, (lli) old_info.header->size);
            _debug_allocator_panic(self, old_ptr, "invalid size parameter");
        }
                
        if(old_info.header->align != align) {
            LOG_FATAL("DEBUG", "debug allocator%s%s align does not match for allocation 0x%08llx: given:%lli actual:%lli", 
                self->name ? "name: " : "", self->name, (llu) old_ptr, (lli) align, (lli) old_info.header->align);
            _debug_allocator_panic(self, old_ptr, "invalid align parameter");
        }
    }
    else
    {
        if(old_size != 0) {
            LOG_FATAL("DEBUG", "debug allocator%s%s given NULL allocation pointer but size of %lliB", 
                self->name ? "name: " : "", self->name, (llu) old_size);
            _debug_allocator_panic(self, old_ptr, "invalid size parameter");
        }
    }

    u8* new_block_ptr = (u8*) allocator_try_reallocate(self->parent, new_total_size, old_info.block_start, old_total_size, DEF_ALIGN, error);
    void* new_ptr = NULL;

    //if hasnt failed
    if(new_block_ptr != NULL || new_size == 0)
    {
        //if allocated/reallocated (new block exists)
        if(new_size != 0)
        {
            isize fixed_align = MAX(align, DEF_ALIGN);
            new_ptr = (u8*) align_forward(new_block_ptr + preamble_size, fixed_align);
            new_info = _debug_allocator_get_block_info(self, new_ptr);

            new_info.header->align = (i32) align;
            new_info.header->size = new_size;
            new_info.header->offset = (i32) ((u8*) new_info.header - new_block_ptr);
            new_info.header->epoch_time = platform_epoch_time();

            if(self->capture_stack_frames_count > 0)
                platform_capture_call_stack(new_info.call_stack, self->capture_stack_frames_count, 1);

            memset(new_info.pre_dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) self->dead_zone_size);
            memset(new_info.post_dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) self->dead_zone_size);
            
            bool was_found = ptr_set_add(&self->alive_allocations, new_ptr);
            ASSERT(new_info.header->offset <= fixed_align && "must be less then align");
            ASSERT(was_found && "Must not be added already!");
            #ifdef DO_ASSERTS_SLOW
                debug_allocator_test_block(self, new_ptr);
            #endif
        }

        //if previous block existed remove it from controll structures
        if(old_ptr != NULL)
            ptr_set_remove_at(&self->alive_allocations, old_found);

        self->bytes_allocated += new_size - old_size;
        self->max_bytes_allocated = MAX(self->max_bytes_allocated, self->bytes_allocated);
        if(self->do_printing && self->is_within_allocation == false)
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
    
    bool debug_check = false;
    #ifdef DO_ASSERTS_SLOW
        debug_check = true;
    #endif

    if(self->do_continual_checks || debug_check)
        debug_allocator_test_all_blocks(self);

    PROFILE_STOP();
    return new_ptr;
}

EXTERNAL Debug_Allocation debug_allocator_get_allocation(const Debug_Allocator* self, void* ptr)
{
    Debug_Allocation allocation = {0};
    if(ptr_set_has(&self->alive_allocations, ptr))
    {
        Debug_Allocation_Info info = _debug_allocator_get_block_info(self, ptr);
        allocation.align = info.header->align;
        allocation.size = info.header->size;
        allocation.ptr = ptr;
        allocation.call_stack = info.call_stack;
        allocation.epoch_time = info.header->epoch_time;
    }

    return allocation;
} 

INTERNAL int _debug_allocation_alloc_time_compare(const void* a_, const void* b_)
{
    Debug_Allocation* a = (Debug_Allocation*) a_;
    Debug_Allocation* b = (Debug_Allocation*) b_;
    return (a->epoch_time > b->epoch_time) - (a->epoch_time < b->epoch_time);  
}

EXTERNAL Debug_Allocation* debug_allocator_get_alive_allocations(Allocator* result_alloc, const Debug_Allocator* self, isize get_max, isize* allocated) //If get_max == -1 returns all
{
    _debug_allocator_check_invariants(self);
    const Ptr_Set* hash = &self->alive_allocations;
    isize count = get_max;
    if(count == -1)
        count = self->alive_allocations.count;
    if(count >= self->alive_allocations.count)
        count = self->alive_allocations.count;

    Debug_Allocation* out = (Debug_Allocation*) allocator_allocate(result_alloc, sizeof(Debug_Allocation)*count, DEF_ALIGN);
    isize pushed_i = 0;
    for(isize k = 0; k < self->alive_allocations.capacity; k++)
    {
        Ptr_Set_Entry entry = self->alive_allocations.entries[k];
        if(entry.ptr != NULL)
        {
            Debug_Allocation_Info info = _debug_allocator_get_block_info(self, entry.ptr);
            Debug_Allocation allocation = {0};
            allocation.align = info.header->align;
            allocation.size = info.header->size;
            allocation.ptr = entry.ptr;
            allocation.call_stack = info.call_stack;
            allocation.epoch_time = info.header->epoch_time;

            ASSERT(pushed_i < count);
            out[pushed_i++] = allocation;
        }
    }
    
    qsort(out, (size_t) count, sizeof *out, _debug_allocation_alloc_time_compare);
    return out;
}

EXTERNAL void debug_allocator_print_alive_allocations(const char* name, Log_Type log_type, const Debug_Allocator* allocator, isize print_max, bool print_with_callstack)
{
    _debug_allocator_check_invariants(allocator);
    
    isize alive_count = 0;
    Debug_Allocation* alive = debug_allocator_get_alive_allocations(allocator->parent, allocator, print_max, &alive_count);
    if(print_max > 0)
        ASSERT(alive_count <= print_max);

    LOG(log_type, name, "printing ALIVE allocations (%lli) below:", (lli)alive_count);
    for(isize i = 0; i < alive_count; i++)
    {
        Debug_Allocation curr = alive[i];
        LOG(log_type, name, "[%3lli]: ptr:0x%08llx size:%5.2lfKB (%lliB) align:%lli",
            (lli) i, (llu) curr.ptr, (double)curr.size/1024, (lli) curr.size, (lli) curr.align);
    
        if(print_with_callstack && allocator->capture_stack_frames_count > 0) 
            log_captured_callstack(log_type, name, curr.call_stack, allocator->capture_stack_frames_count);
    }
    allocator_deallocate(allocator->parent, alive, sizeof(Debug_Allocation)*alive_count, DEF_ALIGN);
}

EXTERNAL Allocator_Stats debug_allocator_get_stats(Allocator* self_)
{
    Debug_Allocator* self = (Debug_Allocator*) (void*) self_;
    Allocator_Stats out = {0};
    out.type_name = "Debug_Allocator";
    out.name = self->name;
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


//Ptr set =====================
EXTERNAL uint64_t ptr_set_hash(void* ptr) 
{
    uint64_t x = (uint64_t) ptr; 
    x = (x ^ (x >> 31) ^ (x >> 62)) * (uint64_t) 0x319642b2d24d8ec3;
    x = (x ^ (x >> 27) ^ (x >> 54)) * (uint64_t) 0x96de1b173f119089;
    x = x ^ (x >> 30) ^ (x >> 60);
    return x;
}

EXTERNAL void ptr_set_init(Ptr_Set* set, Allocator* alloc)
{
    ptr_set_deinit(set);
    set->alloc = alloc;
}
EXTERNAL void ptr_set_deinit(Ptr_Set* set)
{
    if(set->entries && set->alloc)
        allocator_deallocate(set->alloc, set->entries, set->capacity*sizeof(Ptr_Set_Entry), sizeof(void*));
    memset(set, 0, sizeof *set);
}

INTERNAL bool _ptr_set_add(Ptr_Set_Entry* entries, uint64_t capacity, uint64_t hash, void* ptr)
{
    uint64_t mask = capacity - 1;
    uint64_t i = hash & mask;
    for(uint64_t dist = 0;; dist++) {
        ASSERT(dist < capacity);

        Ptr_Set_Entry* entry = &entries[i];
        if(entry->ptr == ptr)
            return false;

        if(entry->ptr == NULL) {
            entry->hash = hash; 
            entry->ptr = ptr; 
            break;
        }

        //If we are further then the current entry,
        // store into that entry and continue probing with 
        // that entry's value
        uint64_t entry_dist = (i - entry->hash) & mask;
        if(entry_dist < dist) {
            Ptr_Set_Entry temp = *entry;
            entry->hash = hash; 
            entry->ptr = ptr; 

            hash = temp.hash;
            ptr = temp.ptr;
            dist = entry_dist;
        }
        
        i = (i + 1) & mask;
    }
    return true;
}

EXTERNAL bool ptr_set_reserve(Ptr_Set* table, isize count)
{   
    uint64_t needed_cap = (uint64_t)count*4/3;
    if(table->capacity > needed_cap)
        return false;

    #ifdef DO_ASSERTS_SLOW
        ptr_set_test_invariants(table);
    #endif
    uint64_t new_cap = 16;
    while(new_cap < needed_cap)
        new_cap *= 2;

    Ptr_Set_Entry* new_entries = (Ptr_Set_Entry*) allocator_allocate(table->alloc, new_cap*sizeof(Ptr_Set_Entry), sizeof(void*));
    memset(new_entries, 0, new_cap*sizeof(Ptr_Set_Entry));

    for(uint32_t i = 0; i < table->capacity; i++)
    {
        Ptr_Set_Entry* entry = &table->entries[i];
        bool was_already_present = _ptr_set_add(new_entries, needed_cap, entry->hash, entry->ptr);
        ASSERT(was_already_present == false);
    }

    allocator_deallocate(table->alloc, table->entries, table->capacity*sizeof(Ptr_Set_Entry), sizeof(void*));
    table->entries = new_entries;
    table->capacity = new_cap;
    
    #ifdef DO_ASSERTS_SLOW
        ptr_set_test_invariants(table);
    #endif
    return true;
}

//returns index + 1 if found or 0 if not
EXTERNAL uint64_t ptr_set_find(const Ptr_Set* table, void* ptr)
{
    uint64_t hash = ptr_set_hash(ptr);
    if(table->count >= 0) {
        uint64_t mask = table->capacity - 1;
        uint64_t i = hash & mask;
        for(uint64_t dist = 0;;) {
            ASSERT(dist++ < table->capacity);

            Ptr_Set_Entry* entry = &table->entries[i];
            if(entry->ptr == ptr)
                return i + 1;

            if(entry->ptr == NULL)
                break;

            i = (i + 1) & mask;
        }
    }
    
    return 0;
}

EXTERNAL bool ptr_set_add(Ptr_Set* table, void* ptr)
{
    ptr_set_reserve(table, table->count + 1);

    uint64_t hash = ptr_set_hash(ptr);
    uint64_t mask = table->capacity - 1;

    bool added = _ptr_set_add(table->entries, mask, hash, ptr);
    table->count += added;
    
    ASSERT_SLOW(ptr_set_find(table, ptr));
    return added;
}

EXTERNAL bool ptr_set_remove_at(Ptr_Set* table, void* ptr, uint64_t found)
{
    uint64_t mask = table->capacity - 1;
    if(found > 0)
        return false;

    //remove entry
    ASSERT(found <= table->capacity);
    table->entries[found - 1].hash = 0;
    table->entries[found - 1].ptr = NULL;

    //keep shifting entries back until we find
    // one thats empty or thats precisely in its correct place
    uint64_t prev = found - 1;
    for(uint64_t dist = 0;;) {
        ASSERT(dist++ < table->capacity);

        uint64_t i = (prev + 1) & mask;
        Ptr_Set_Entry* entry = &table->entries[i];
        if(entry->ptr == NULL || (entry->hash & mask) == i)
            break;

        table->entries[prev] = *entry; 
        prev = i;
    }
    
    ASSERT_SLOW(ptr_set_find(table, ptr) == 0);
    return true;
}

EXTERNAL bool ptr_set_has(const Ptr_Set* table, void* ptr)
{
    return ptr_set_find(table, ptr) > 0;
}

EXTERNAL bool ptr_set_remove(Ptr_Set* table, void* ptr)
{
    return ptr_set_remove_at(table, ptr, ptr_set_find(table, ptr));
}

EXTERNAL void ptr_set_test_invariants(const Ptr_Set* table)
{
    if(table->capacity == 0) {
        TEST(table->count == 0);
        TEST(table->entries == NULL);
    }
    else {
        TEST(table->alloc);
        TEST(table->entries);
        TEST(is_power_of_two(table->capacity));
        TEST(table->count*4/3 < table->capacity);

        uint32_t found_nonzero = 0;
        for(uint64_t i = 0; i < table->capacity; i++) {
            Ptr_Set_Entry* entry = &table->entries[i];
            if(entry->ptr == NULL) 
                TEST(entry->hash == 0);
            else
            {
                uint64_t found = ptr_set_find(table, entry->ptr);
                TEST(found > 0 && found - 1 == i);
                found_nonzero += 1;
            }
        }
        TEST(table->count == found_nonzero);
    }
}


#endif