#ifndef JOT_DEBUG_ALLOCATOR
#define JOT_DEBUG_ALLOCATOR

//It is extremely easy to mess up memory management in some way in C. Even when using hierarchical memory management
// (local allocator tree) memory leeks are still locally possible which is often not idea. Thus we are need in
// of solid tooling to enable quick and reliable debugging of memory problems. 
// 
// This file attempts to create a simple
// allocator for just that using the Allocator interface. The advantage is that it can be swapped in even during runtime
// for maximum flexibility.
//
// From memory debugger we require the following functionality:
// 1) assert validity of all programmer given memory blocks without touching them
// 2) be able to list all currently active memory blocks along with some info to facilitate debugging
// 3) assert that no overwrites (and ideally even overreads) happened 
//
// Additionally we would like the following:
// 4) to see some amount of allocation history
// 5) runtime customize what will happen should a memory panic be raised
// 6) the allocator should be as fast as possible
//
// The approach this file takes is on the following schema:
//
//  Debug_Allocator
//  |-------------------------|
//  | Allocator* parent       |                        |-----------------------------------------------------|
//  | ...                     |           0----------->| XXX | header | call stack | dead | USER| dead | XXX |
//  | alive_allocations_hash: |           |            |-----------------------------------------------------|
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
#include "array.h"
#include "hash_index.h"
#include "hash.h"
#include "log.h"

#include "profile_defs.h"
#include "vformat.h"

typedef struct Debug_Allocator          Debug_Allocator;
typedef struct Debug_Allocation         Debug_Allocation;
typedef struct Debug_Allocator_Options  Debug_Allocator_Options;

typedef Array_Aligned(Debug_Allocation, DEF_ALIGN) Debug_Allocation_Array;

typedef enum Debug_Allocator_Panic_Reason {
    DEBUG_ALLOC_PANIC_NONE = 0, //no error
    DEBUG_ALLOC_PANIC_INVALID_PTR, //the provided pointer does not point to previously allocated block
    DEBUG_ALLOC_PANIC_INVALID_PARAMS, //size and/or alignment for the given allocation ptr do not match or are invalid (less then zero, not power of two)
    DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK, //memory was written before valid user allocation segment
    DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK, //memory was written after valid user allocation segment
    DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED, //memory usage on startup doesnt match memory usage on deinit. Only used when initialized with do_deinit_leak_check = true
} Debug_Allocator_Panic_Reason;

typedef void (*Debug_Allocator_Panic)(Debug_Allocator* allocator, Debug_Allocator_Panic_Reason reason, Debug_Allocation allocation, isize penetration, void* context);

typedef struct Debug_Allocator
{
    Allocator alloc[1];
    Allocator* parent;
    const char* name;
    
    Hash_Index alive_allocations_hash;

    bool do_printing;            //whether each allocations/deallocations should be printed. can be safely toggled during lifetime
    bool do_continual_checks;     //whether it should checks all allocations for overwrites after each allocation.
                                 //incurs huge performance costs. can be safely toggled during runtime.
    bool do_deinit_leak_check;   //If the memory use on initialization and deinitialization does not match panics.
                                 //can be toggled during runtime. 
    bool is_init;                //prevents double init
    b32 is_within_allocation;   //prevents infinite recursion on logging functions

    isize captured_callstack_size; //number of stack frames to capture on each allocation. Defaults to 0.
                                   //If this is greater than 0 replaces passed source info in reports
    isize dead_zone_size;        //size in bytes of the dead zone. CANNOT be changed after creation!
    
    Debug_Allocator_Panic panic_handler;
    void* panic_context;

    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    Allocator_Set allocator_backup;
} Debug_Allocator;

#define DEBUG_ALLOCATOR_CONTINUOUS          (u64) 1  /* do_continual_checks = true */
#define DEBUG_ALLOCATOR_PRINT               (u64) 2  /* do_printing = true */
#define DEBUG_ALLOCATOR_LARGE_DEAD_ZONE     (u64) 4  /* dead_zone_size = 64 */
#define DEBUG_ALLOCATOR_NO_DEAD_ZONE        (u64) 8  /* dead_zone_size = 0 */
#define DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK   (u64) 16 /* do_deinit_leak_check = true */
#define DEBUG_ALLOCATOR_CAPTURE_CALLSTACK   (u64) 32 /* captured_callstack_size = 16 */

//Initalizes the debug allocator using a parent and options. 
//Many options cannot be changed during the life of the debug allocator.
EXTERNAL void debug_allocator_init_custom(Debug_Allocator* allocator, Allocator* parent, Debug_Allocator_Options options);
EXTERNAL void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, u64 flags);
//Initalizes the debug allocator and makes it the dafault and scratch global allocator.
//Additional flags defined above can be passed to quickly tweak the allocator.
//Restores the old allocators on deinit. 
EXTERNAL void debug_allocator_init_use(Debug_Allocator* allocator, Allocator* parent, u64 flags);
//Deinits the debug allocator
EXTERNAL void debug_allocator_deinit(Debug_Allocator* allocator);

EXTERNAL void* debug_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
EXTERNAL Allocator_Stats debug_allocator_get_stats(Allocator* self_);

//Returns info about the specific alive debug allocation @TODO
EXTERNAL Debug_Allocation       debug_allocator_get_allocation(const Debug_Allocator* allocator, void* ptr); 
//Returns up to get_max currectly alive allocations sorted by their time of allocation. If get_max <= 0 returns all
EXTERNAL Debug_Allocation_Array debug_allocator_get_alive_allocations(const Debug_Allocator allocator, isize print_max);

//Prints up to get_max currectly alive allocations sorted by their time of allocation. If get_max <= 0 returns all
EXTERNAL void debug_allocator_print_alive_allocations(Log log, const Debug_Allocator allocator, isize print_max); 

//Converts a panic reason to string
EXTERNAL const char* debug_allocator_panic_reason_to_string(Debug_Allocator_Panic_Reason reason);

typedef struct Debug_Allocation {
    void* ptr;                     
    isize size;
    isize align;
    i64 allocation_epoch_time;
    void** allocation_trace;
} Debug_Allocation;

typedef struct Debug_Allocator_Options
{
    //size in bytes of overwite prevention dead zone. If 0 then default 16 is used. If less then zero no dead zone is used.
    isize dead_zone_size; 
    //size of history to keep. If 0 then default 1000 is used. If less then zero no history is kept.
    
    isize captured_callstack_size; //number of stack frames to capture on each allocation. Defaults to 0.

    //Pointer to Debug_Allocator_Panic. If none is set uses debug_allocator_panic_func
    Debug_Allocator_Panic panic_handler; 
    //Context for panic_handler. No default is set.
    void* panic_context;

    bool do_printing;        //prints all allocations/deallocation
    bool do_continual_checks; //continually checks all allocations
    bool do_deinit_leak_check;   //If the memory use on initialization and deinitialization does not match panics.
    bool _padding[5];
    //Optional name of this allocator for printing and debugging. No default is set
    const char* name;
} Debug_Allocator_Options;

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_DEBUG_ALLOCATOR_IMPL)) && !defined(JOT_DEBUG_ALLOCATOR_HAS_IMPL)
#define JOT_DEBUG_ALLOCATOR_HAS_IMPL

#define DEBUG_ALLOCATOR_MAGIC_NUM8  (u8)  0x55

#include <string.h>
#include <stdlib.h> //qsort


EXTERNAL void debug_allocator_init_custom(Debug_Allocator* debug, Allocator* parent, Debug_Allocator_Options options)
{
    debug_allocator_deinit(debug);
    hash_index_init(&debug->alive_allocations_hash, parent);

    if(options.dead_zone_size == 0)
        options.dead_zone_size = 16;
    if(options.dead_zone_size < 0)
        options.dead_zone_size = 0;
    options.dead_zone_size = DIV_CEIL(options.dead_zone_size, DEF_ALIGN)*DEF_ALIGN;

    debug->captured_callstack_size = options.captured_callstack_size;
    debug->do_deinit_leak_check = options.do_deinit_leak_check;
    debug->name = options.name;
    debug->do_continual_checks = options.do_continual_checks;
    debug->dead_zone_size = options.dead_zone_size;
    debug->do_printing = options.do_printing;
    debug->parent = parent;
    debug->alloc[0].func = debug_allocator_func;
    debug->alloc[0].get_stats = debug_allocator_get_stats;
    debug->panic_handler = options.panic_handler;
    debug->panic_context = options.panic_context;

    debug->alive_allocations_hash.do_in_place_rehash = true;
    debug->is_init = true;
}

EXTERNAL void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, u64 flags)
{
    Debug_Allocator_Options options = {0};
    if(flags & DEBUG_ALLOCATOR_CONTINUOUS)
        options.do_continual_checks = true;
    if(flags & DEBUG_ALLOCATOR_PRINT)
        options.do_printing = true;
    if(flags & DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK)
        options.do_deinit_leak_check = true;
    if(flags & DEBUG_ALLOCATOR_LARGE_DEAD_ZONE)
        options.dead_zone_size = 64;
    if(flags & DEBUG_ALLOCATOR_NO_DEAD_ZONE)
        options.dead_zone_size = 0;
    if(flags & DEBUG_ALLOCATOR_CAPTURE_CALLSTACK)
        options.captured_callstack_size = 16;

    debug_allocator_init_custom(allocator, parent, options);
}
EXTERNAL void debug_allocator_init_use(Debug_Allocator* debug, Allocator* parent, u64 flags)
{
    debug_allocator_init(debug, parent, flags);
    debug->allocator_backup = allocator_set_default(debug->alloc);
}

typedef struct Debug_Allocation_Header {
    isize size;
    i32 align;
    i32 block_start_offset;
    i64 allocation_epoch_time;
} Debug_Allocation_Header;

typedef struct Debug_Allocation_Pre_Block {
    Debug_Allocation_Header* header;
    void* user_ptr;
    void** call_stack;
    u8* dead_zone;
    isize dead_zone_size;
    isize call_stack_size;
} Debug_Allocation_Pre_Block;

typedef struct Debug_Allocation_Post_Block {
    u8* dead_zone;
    isize dead_zone_size;
} Debug_Allocation_Post_Block;

INTERNAL Debug_Allocation_Pre_Block _debug_allocator_get_pre_block(const Debug_Allocator* self, void* user_ptr)
{
    Debug_Allocation_Pre_Block pre_block = {0};
    pre_block.user_ptr = user_ptr;
    pre_block.dead_zone_size = self->dead_zone_size;
    pre_block.call_stack_size = self->captured_callstack_size;
    pre_block.dead_zone = (u8*) user_ptr - self->dead_zone_size;
    pre_block.call_stack = (void**) pre_block.dead_zone - self->captured_callstack_size;
    pre_block.header = (Debug_Allocation_Header*) pre_block.call_stack - 1;
    return pre_block;
}

INTERNAL Debug_Allocation_Post_Block _debug_allocator_get_post_block(const Debug_Allocator* self, void* user_ptr, isize size)
{
    Debug_Allocation_Post_Block post_block = {0};
    post_block.dead_zone = (u8*) user_ptr + size;
    post_block.dead_zone_size = self->dead_zone_size;
    return post_block;
}
typedef struct Debug_Alloc_Sizes {
    isize preamble_size;
    isize postamble_size;
    isize total_size;
} Debug_Alloc_Sizes;

INTERNAL Debug_Alloc_Sizes _debug_allocator_allocation_sizes(const Debug_Allocator* self, isize size, isize align)
{
    isize preamble_size = isizeof(Debug_Allocation_Header) + self->dead_zone_size + self->captured_callstack_size * isizeof(void*);
    isize postamble_size = self->dead_zone_size;
    isize total_size = preamble_size + postamble_size + align + size;

    //Kinda rude check but alas we dont allow 0 sized blocks so this is okay
    if(size == 0)
        total_size = 0;

    Debug_Alloc_Sizes out = {preamble_size, postamble_size, total_size};
    return out;
}

INTERNAL int _debug_allocation_alloc_time_compare(const void* a_, const void* b_)
{
    Debug_Allocation* a = (Debug_Allocation*) a_;
    Debug_Allocation* b = (Debug_Allocation*) b_;

    if(a->allocation_epoch_time < b->allocation_epoch_time)
        return -1;
    else
        return 1;
}

INTERNAL Debug_Allocator_Panic_Reason _debug_allocator_check_block(const Debug_Allocator* self, void* user_ptr, isize* interpenetration, isize* hash_found, isize size_or_zero, isize align_or_zero)
{
    *interpenetration = 0;
    
    u64 hashed = hash64((u64) user_ptr);
    *hash_found = hash_index_find(self->alive_allocations_hash, hashed);
    if(*hash_found == -1)
        return DEBUG_ALLOC_PANIC_INVALID_PTR;
        
    Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, user_ptr);
    for(isize i = pre.dead_zone_size; i-- > 0; )
    {
        if(pre.dead_zone[i] != DEBUG_ALLOCATOR_MAGIC_NUM8)
        {
            *interpenetration = i;
            return DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK;
        }
    }
        
    if(is_power_of_two(pre.header->align) == false || pre.header->size <= 0)
        return DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK;

    if(size_or_zero > 0 && pre.header->size != size_or_zero)
        return DEBUG_ALLOC_PANIC_INVALID_PARAMS;
        
    if(align_or_zero > 0 && pre.header->align != align_or_zero)
        return DEBUG_ALLOC_PANIC_INVALID_PARAMS;
        
    if((size_t) user_ptr % (size_t) pre.header->align != 0)
        return DEBUG_ALLOC_PANIC_INVALID_PARAMS;

    Debug_Allocation_Post_Block post = _debug_allocator_get_post_block(self, user_ptr, pre.header->size);
    for(isize i = 0; i < post.dead_zone_size; i++)
    {
        if(post.dead_zone[i] != DEBUG_ALLOCATOR_MAGIC_NUM8)
        {
            *interpenetration = i;
            return DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK;
        }
    }

    return DEBUG_ALLOC_PANIC_NONE;
}

INTERNAL void _debug_allocator_assert_block(const Debug_Allocator* allocator, void* user_ptr)
{
    bool check = false;
    #ifndef NDEBUG
        check = true;
    #endif // !NDEBUG

    if(check)
    {
        isize interpenetration = 0;
        isize found = 0;
        Debug_Allocator_Panic_Reason reason = _debug_allocator_check_block(allocator, user_ptr, &interpenetration, &found, 0, 0);
        ASSERT(reason == DEBUG_ALLOC_PANIC_NONE);
    }
}

INTERNAL bool _debug_allocator_is_invariant(const Debug_Allocator* allocator)
{
    ASSERT(allocator->dead_zone_size/DEF_ALIGN*DEF_ALIGN == allocator->dead_zone_size 
        && "dead zone size must be a valid multiple of alignment"
        && "this is so that the pointers within the header will be properly aligned!");

    //All alive allocations must be in hash
    if(allocator->do_continual_checks)
    {
        isize size_sum = 0;
        for(isize i = 0; i < allocator->alive_allocations_hash.entries_count; i ++)
        {
            Hash_Index_Entry curr = allocator->alive_allocations_hash.entries[i];
            if(hash_index_is_entry_used(curr) == false)
                continue;
        
            void* user_ptr = hash_index_restore_ptr(curr.value);
            _debug_allocator_assert_block(allocator, user_ptr);

            Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(allocator, user_ptr);
            size_sum += pre.header->size;
        }

        ASSERT(size_sum == allocator->bytes_allocated);
        ASSERT(size_sum <= allocator->max_bytes_allocated);
    }

    return true;
}

EXTERNAL const char* debug_allocator_panic_reason_to_string(Debug_Allocator_Panic_Reason reason)
{
    switch(reason)
    {
        case DEBUG_ALLOC_PANIC_NONE:                    return "DEBUG_ALLOC_PANIC_NONE";
        case DEBUG_ALLOC_PANIC_INVALID_PTR:             return "DEBUG_ALLOC_PANIC_INVALID_PTR";
        case DEBUG_ALLOC_PANIC_INVALID_PARAMS:          return "DEBUG_ALLOC_PANIC_INVALID_PARAMS"; 
        case DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK:  return "DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK";
        case DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK:   return "DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK";
        case DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED:    return "DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED";
        default: return "DEBUG_ALLOC_PANIC_NONE";
    }
}

INTERNAL void* _debug_allocator_panic(Debug_Allocator* self, Debug_Allocator_Panic_Reason reason, void* ptr, isize interpenetration)
{
    Debug_Allocation allocation = {0};
    allocation.ptr = ptr;

    if(self->panic_handler != NULL)
        self->panic_handler(self, reason, allocation, interpenetration, self->panic_context);
    else
    {
        const char* reason_str = debug_allocator_panic_reason_to_string(reason);

        LOG_FATAL("MEMORY", "PANIC because of %s at pointer 0x%08llx (penetration: %lli)", reason_str, (lli) allocation.ptr, interpenetration);
        debug_allocator_print_alive_allocations(log_trace("MEMORY"), *self, 0);
    
        log_flush_all();
        abort();
    }

    return NULL;
}
EXTERNAL void debug_allocator_deinit(Debug_Allocator* allocator)
{
    if(allocator->bytes_allocated != 0 && allocator->do_deinit_leak_check)
        _debug_allocator_panic(allocator, DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED, NULL, 0);

    for(isize i = 0; i < allocator->alive_allocations_hash.entries_count; i++)
    {
        Hash_Index_Entry entry = allocator->alive_allocations_hash.entries[i];
        if(hash_index_is_entry_used(entry))
        {
            void* ptr = hash_index_restore_ptr(entry.value);
            
            Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(allocator, ptr);
            debug_allocator_func(allocator->alloc, 0, ptr, pre.header->size, pre.header->align, NULL);
        }
    }

    allocator_set(allocator->allocator_backup);
    hash_index_deinit(&allocator->alive_allocations_hash);
    
    Debug_Allocator null = {0};
    *allocator = null;
}

EXTERNAL Debug_Allocation_Array debug_allocator_get_alive_allocations(const Debug_Allocator allocator, isize print_max)
{
    isize count = print_max;
    const Hash_Index* hash = &allocator.alive_allocations_hash;
    if(count <= 0)
        count = hash->len;
        
    if(count >= hash->len)
        count = hash->len;
        
    Debug_Allocation_Array out = {allocator.parent};
    for(isize k = 0; k < hash->entries_count; k++)
    {
        if(hash_index_is_entry_used(hash->entries[k]))
        {
            void* user_ptr = hash_index_restore_ptr(hash->entries[k].value);
            _debug_allocator_assert_block(&allocator, user_ptr);
            
            Debug_Allocation allocation = {allocator.parent};
            Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(&allocator, user_ptr);
            allocation.align = pre.header->align;
            allocation.size = pre.header->size;

            allocation.allocation_epoch_time = pre.header->allocation_epoch_time;
            allocation.ptr = pre.user_ptr;
            allocation.allocation_trace = pre.call_stack;

            array_push(&out, allocation);
        }
    }
    
    qsort(out.data, (size_t) out.len, sizeof *out.data, _debug_allocation_alloc_time_compare);

    array_resize(&out, count);
    return out;
}

EXTERNAL void debug_allocator_print_alive_allocations(Log log, const Debug_Allocator allocator, isize print_max)
{
    _debug_allocator_is_invariant(&allocator);
    
    Debug_Allocation_Array alive = debug_allocator_get_alive_allocations(allocator, print_max);
    if(print_max > 0)
        ASSERT(alive.len <= print_max);

    LOG(log, "printing ALIVE allocations (%lli) below:", (lli)alive.len);
    for(isize i = 0; i < alive.len; i++)
    {
        Debug_Allocation curr = alive.data[i];
        LOG(log, "%-3lli - size %-8lli ptr: 0x%08llx align: %-2lli",
            (lli) i, (lli) curr.size, (lli) curr.ptr, (lli) curr.align);
     
        if(allocator.captured_callstack_size > 0)
            log_captured_callstack(log_indented(log), curr.allocation_trace, allocator.captured_callstack_size);
    }

    array_deinit(&alive);
}

EXTERNAL void* debug_allocator_func(Allocator* self_, isize new_size, void* old_ptr_, isize old_size, isize align, Allocator_Error* error)
{
    Debug_Allocator* self = (Debug_Allocator*) (void*) self_;
    //If is arena just use it and return
    if((u64) self->parent & 1)
    {
        return allocator_reallocate(self->parent, new_size, old_ptr_, old_size, align);
    }
    
    PROFILE_START();
    _debug_allocator_is_invariant(self);

    Debug_Alloc_Sizes new_sizes = _debug_allocator_allocation_sizes(self, new_size, align);
    Debug_Alloc_Sizes old_sizes = _debug_allocator_allocation_sizes(self, old_size, align);

    u8* old_block_ptr = NULL;
    u8* new_block_ptr = NULL;
    u8* old_ptr = (u8*) old_ptr_;
    u8* new_ptr = NULL;

    isize hash_found = -1;

    //Check old_ptr if any for correctness
    if(old_ptr != NULL)
    {
        isize interpenetration = 0;
        Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, old_ptr);
        Debug_Allocator_Panic_Reason reason = _debug_allocator_check_block(self, old_ptr, &interpenetration, &hash_found, old_size, align);
        if(reason != DEBUG_ALLOC_PANIC_NONE)
        {
            PROFILE_END();
            return _debug_allocator_panic(self, reason, old_ptr, interpenetration);
        }
        
        old_block_ptr = (u8*) pre.header - pre.header->block_start_offset;
    }

    new_block_ptr = (u8*) self->parent->func(self->parent, new_sizes.total_size, old_block_ptr, old_sizes.total_size, DEF_ALIGN, error);
    
    //if failed return failiure and do nothing
    if(new_block_ptr == NULL && new_size != 0)
    {
        PROFILE_END();
        return NULL;
    }
    
    //if previous block existed remove it from controll structures
    if(old_ptr != NULL)
    {
        ASSERT(hash_found != -1 && "must be found!");
        hash_index_remove(&self->alive_allocations_hash, hash_found);
    }

    //if allocated/reallocated (new block exists)
    if(new_size != 0)
    {
        isize fixed_align = MAX(align, DEF_ALIGN);
        u8* user_ptr = (u8*) align_forward(new_block_ptr + new_sizes.preamble_size, fixed_align);

        Debug_Allocation_Pre_Block new_pre = _debug_allocator_get_pre_block(self, user_ptr);
        Debug_Allocation_Post_Block new_post = _debug_allocator_get_post_block(self, user_ptr, new_size);

        new_pre.header->align = (i32) align;
        new_pre.header->size = new_size;
        new_pre.header->block_start_offset = (i32) ((u8*) new_pre.header - new_block_ptr);
        new_pre.header->allocation_epoch_time = platform_epoch_time();
        ASSERT(new_pre.header->block_start_offset <= fixed_align && "must be less then align");

        if(self->captured_callstack_size > 0)
            platform_capture_call_stack(new_pre.call_stack, new_pre.call_stack_size, 1);

        memset(new_pre.dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_pre.dead_zone_size);
        memset(new_post.dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_post.dead_zone_size);
        
        new_ptr = (u8*) new_pre.user_ptr;
        u64 hashed = hash64((u64) new_ptr);
        ASSERT(hash_index_find(self->alive_allocations_hash, hashed) == -1 && "Must not be added already!");

        hash_index_insert(&self->alive_allocations_hash, hashed, (u64) new_ptr);
        _debug_allocator_assert_block(self, new_ptr);
    }

    self->bytes_allocated -= old_size;
    self->bytes_allocated += new_size;
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
    
    _debug_allocator_is_invariant(self);
    PROFILE_END();
    return new_ptr;
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

    return out;
}
#endif