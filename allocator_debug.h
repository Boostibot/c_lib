#ifndef JOT_DEBUG_ALLOCATOR
#define JOT_DEBUG_ALLOCATOR

//It is extremely easy to mess up memory management in some way in C. Even when using hierarchical memory managemnt
// (local allocator tree) memory leeks are still localy possible which is often not idea. Thus we are need in
// of solid tooling to enable quick and reliable debugging of memory problems. 
// 
// This file attempts to create a simple
// allocator for just that using the Allocator interface. The advantage is that it can be swapped in even during runtime
// for maximum flexibility.
//
// From memory debugger we require the following functionality:
// 1) assert validity of all programmer given memory blocks without touching them
// 2) be able to list all currently active memory blocks along with some info to fascilitate debugging
// 3) assert that no overwrites (and ideally even overreads) happened 
//
// Additionally we would like the following:
// 4) to see some ammount of allocation history
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
// data about the alloction (is used to validate arguments and fasciliate debugging), dead zones which 
// are filled with 0x55 bytes (0 and 1 alteranting in binary), and some unspecified padding bytes which 
// may occur due to overaligned requirements for user data.
// 
// Prior to each access the block adress is looked up in the alive_allocations_hash. If it is found
// the dead zones and header is checked for validty (invalidty would indicate overwrites). Only then
// any allocation/deallocation takes place.

//@TODO: make simpler!
//@TODO: format size correctly
//@TODO: use the sots order
//@TODO: refactor prining to be more modern & figure out a way to prevent allocations from showing within prints

#include "allocator.h"
#include "array.h"
#include "hash_index.h"
#include "hash.h"
#include "time.h"
#include "log.h"
#include "profile.h"
#include "vformat.h"

typedef struct Debug_Allocator          Debug_Allocator;
typedef struct Debug_Allocation         Debug_Allocation;
typedef struct Debug_Allocator_Options  Debug_Allocator_Options;

DEFINE_ARRAY_TYPE(Debug_Allocation, Debug_Allocation_Array);

typedef enum Debug_Allocator_Panic_Reason {
    DEBUG_ALLOC_PANIC_NONE = 0, //no error
    DEBUG_ALLOC_PANIC_INVALID_PTR, //the provided pointer does not point to previously allocated block
    DEBUG_ALLOC_PANIC_INVALID_PARAMS, //size and/or alignment for the given allocation ptr do not macth or are invalid (less then zero, not power of two)
    DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK, //memory was written before valid user allocation segemnt
    DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK, //memory was written after valid user allocation segemnt
    DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED, //memory usage on startup doesnt match memory usage on deinit. Only used when initialized with do_deinit_leak_check = true
} Debug_Allocator_Panic_Reason;

typedef void (*Debug_Allocator_Panic)(Debug_Allocator* allocator, Debug_Allocator_Panic_Reason reason, Debug_Allocation allocation, isize penetration, Source_Info called_from, void* context);

typedef struct Debug_Allocator
{
    Allocator allocator;
    Allocator* parent;
    const char* name;
    
    Debug_Allocation_Array dead_allocations;
    Hash_Index64 alive_allocations_hash;

    bool do_printing;            //wheter each allocations/deallocations should be printed. can be safely togled during lifetime
    bool do_contnual_checks;     //wheter it should checks all allocations for overwrites after each allocation.
                                 //icurs huge performance costs. can be safely toggled during runtime.
    bool do_deinit_leak_check;   //If the memory use on initialization and deinitializtion does not match panics.
                                 //can be toggled during runtime. 

    isize captured_callstack_size; //number of stack frames to capture on each allocation. Defaults to 0.
                                   //If this is greater than 0 replaces passed source info in reports
    isize dead_zone_size;        //size in bytes of the dead zone. CANNOT be changed after creation!
    isize dead_allocation_max;   //size of dead_allocations circular buffer
    isize dead_allocation_index; //the total ammount of dead allocations (does not wrap)
    
    //alive_allocations.data[dead_allocation_index % dead_allocation_max - 1] 
    //is the most recent allocation
    Debug_Allocator_Panic panic_handler;
    void* panic_context;

    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    Allocator_Set allocator_backup;
    bool is_init; //prevents double init
    bool is_within_allocation;  //prevents infinite recursion on logging functions
} Debug_Allocator;

typedef enum Debug_Allocation_Sort_Criteria {
    DEBUG_ALLOCATOR_SORT_RECENT_ALLOCATED_FIRST,
    DEBUG_ALLOCATOR_SORT_RECENT_ALLOCATED_LAST,

    DEBUG_ALLOCATOR_SORT_RECENT_DEALLOCATED_FIRST,
    DEBUG_ALLOCATOR_SORT_RECENT_DEALLOCATED_LAST,
    
    DEBUG_ALLOCATOR_SORT_BIGGER_FIRST,
    DEBUG_ALLOCATOR_SORT_BIGGER_LAST,
    
    DEBUG_ALLOCATOR_SORT_ALIGNED_FIRST,
    DEBUG_ALLOCATOR_SORT_ALIGNED_LAST,
} Debug_Allocation_Sort_Criteria;


#define DEBUG_ALLOCATOR_CONTINUOUS          (u64) 1  /* do_contnual_checks = true */
#define DEBUG_ALLOCATOR_PRINT               (u64) 2  /* do_printing = true */
#define DEBUG_ALLOCATOR_LARGE_DEAD_ZONE     (u64) 4  /* dead_zone_size = 64 */
#define DEBUG_ALLOCATOR_NO_DEAD_ZONE        (u64) 8  /* dead_zone_size = 0 */
#define DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK   (u64) 16 /* do_deinit_leak_check = true */
#define DEBUG_ALLOCATOR_CAPTURE_CALLSTACK   (u64) 32 /* captured_callstack_size = 16 */
#define DEBUG_ALLOCATOR_KEEP_HISTORY        (u64) 64 /* max_dead_allocations = 1000 */

//Initalizes the debug allocator using a parent and options. 
//Many options cannot be changed during the life of the debug allocator.
EXPORT void debug_allocator_init_custom(Debug_Allocator* allocator, Allocator* parent, Debug_Allocator_Options options);
EXPORT void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, u64 flags);
//Initalizes the debug allocator and makes it the dafault and scratch global allocator.
//Additional flags defined above can be passed to quickly tweak the allocator.
//Restores the old allocators on deinit. 
EXPORT void debug_allocator_init_use(Debug_Allocator* allocator, Allocator* parent, u64 flags);
//Deinits the debug allocator
EXPORT void debug_allocator_deinit(Debug_Allocator* allocator);

EXPORT void* debug_allocator_allocate(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from);
EXPORT Allocator_Stats debug_allocator_get_stats(Allocator* self_);

//Returns info about the specific alive debug allocation @TODO
EXPORT Debug_Allocation       debug_allocator_get_allocation(const Debug_Allocator* allocator, void* ptr); 
//Returns up to get_max currectly alive allocations sorted by their time of allocation. If get_max <= 0 returns all
EXPORT Debug_Allocation_Array debug_allocator_get_alive_allocations(const Debug_Allocator allocator, isize print_max);
//Returns up to get_max last dead allocations sorted by their time of allocation. If get_max <= 0 returns up to dead_allocation_max (specified during construction)
EXPORT Debug_Allocation_Array debug_allocator_get_dead_allocations(const Debug_Allocator allocator, isize get_max);
EXPORT void debug_allocator_deinit_allocation(Debug_Allocation* allocation);
EXPORT void debug_allocator_deinit_allocation_array(Debug_Allocation_Array* allocations);

//Prints up to get_max currectly alive allocations sorted by their time of allocation. If get_max <= 0 returns all
EXPORT void debug_allocator_print_alive_allocations(const char* log_module, Log_Type log_type, const Debug_Allocator allocator, isize print_max); 
//Returns up to get_max last dead allocations sorted by their time of allocation. If get_max <= 0 returns up to dead_allocation_max (specified during construction)
EXPORT void debug_allocator_print_dead_allocations(const char* log_module, Log_Type log_type, const Debug_Allocator allocator, isize print_max);

//Default panic handler for debug allocators. Prints if printing is enbaled and then aborts the program 
EXPORT void debug_allocator_panic_func(Debug_Allocator* allocator, Debug_Allocator_Panic_Reason reason, Debug_Allocation allocation, isize penetration, Source_Info called_from, void* context);
//Converts a panic reason to string
EXPORT const char* debug_allocator_panic_reason_to_string(Debug_Allocator_Panic_Reason reason);

typedef struct Debug_Allocation
{
    void* ptr;                     
    isize size;
    isize align;
    Source_Info allocation_source;
    Source_Info deallocation_source;
    f64 allocation_time_s;
    f64 deallocation_time_s;
    ptr_Array allocation_trace;
    ptr_Array deallocation_trace;
} Debug_Allocation;

typedef struct Debug_Allocator_Options
{
    //size in bytes of overwite prevention dead zone. If 0 then default 16 is used. If less then zero no dead zone is used.
    isize dead_zone_size; 
    //size of history to keep. If 0 then default 1000 is used. If less then zero no history is kept.
    isize max_dead_allocations;
    
    isize captured_callstack_size; //number of stack frames to capture on each allocation. Defaults to 0.

    //Pointer to Debug_Allocator_Panic. If none is set uses debug_allocator_panic_func
    Debug_Allocator_Panic panic_handler; 
    //Context for panic_handler. No default is set.
    void* panic_context;

    bool do_printing;        //prints all allocations/deallocation
    bool do_contnual_checks; //continually checks all allocations
    bool do_deinit_leak_check;   //If the memory use on initialization and deinitializtion does not match panics.

    //Optional name of this allocator for printing and debugging. No defualt is set
    const char* name;
} Debug_Allocator_Options;

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_DEBUG_ALLOCATOR_IMPL)) && !defined(JOT_DEBUG_ALLOCATOR_HAS_IMPL)
#define JOT_DEBUG_ALLOCATOR_HAS_IMPL

#define DEBUG_ALLOCATOR_MAGIC_NUM8  (u8)  0x55

#include <string.h>
#include <stdlib.h> //qsort

typedef struct Debug_Allocation_Header
{
    isize size;
    i32 align;
    i32 block_start_offset;
    Source_Info allocation_source;
    f64 allocation_time_s;
} Debug_Allocation_Header;

EXPORT void debug_allocator_init_custom(Debug_Allocator* debug, Allocator* parent, Debug_Allocator_Options options)
{
    ASSERT(debug->is_init == false && "must not be init!");

    array_init(&debug->dead_allocations, parent);
    hash_index64_init(&debug->alive_allocations_hash, parent);

    if(options.dead_zone_size == 0)
        options.dead_zone_size = 16;
    if(options.dead_zone_size < 0)
        options.dead_zone_size = 0;
    options.dead_zone_size = DIV_ROUND_UP(options.dead_zone_size, DEF_ALIGN)*DEF_ALIGN;

    //if(options.max_dead_allocations == 0)
    //    options.max_dead_allocations = 1000;
    if(options.max_dead_allocations < 0)
        options.max_dead_allocations = 0;

    if(options.panic_handler == NULL)
        options.panic_handler = debug_allocator_panic_func;

    debug->captured_callstack_size = options.captured_callstack_size;
    debug->do_deinit_leak_check = options.do_deinit_leak_check;
    debug->name = options.name;
    debug->do_contnual_checks = options.do_contnual_checks;
    debug->dead_zone_size = options.dead_zone_size;
    debug->do_printing = options.do_printing;
    debug->parent = parent;
    debug->allocator.allocate = debug_allocator_allocate;
    debug->allocator.get_stats = debug_allocator_get_stats;
    debug->panic_handler = options.panic_handler;
    debug->panic_context = options.panic_context;
    debug->dead_allocation_max = options.max_dead_allocations;
    debug->dead_allocation_index = 0;
    debug->is_init = true;

    array_resize(&debug->dead_allocations, options.max_dead_allocations);
}

EXPORT void debug_allocator_init(Debug_Allocator* allocator, Allocator* parent, u64 flags)
{
    Debug_Allocator_Options options = {0};
    if(flags & DEBUG_ALLOCATOR_CONTINUOUS)
        options.do_contnual_checks = true;
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
    if(flags & DEBUG_ALLOCATOR_KEEP_HISTORY)
        options.max_dead_allocations = 1000;

    debug_allocator_init_custom(allocator, parent, options);
}
EXPORT void debug_allocator_init_use(Debug_Allocator* debug, Allocator* parent, u64 flags)
{
    debug_allocator_init(debug, parent, flags);
    debug->allocator_backup = allocator_set_both(&debug->allocator, &debug->allocator);
}

INTERNAL void* _debug_allocator_panic(Debug_Allocator* self, Debug_Allocator_Panic_Reason reason, void* ptr, Source_Info called_from);


EXPORT const char* debug_allocator_panic_reason_to_string(Debug_Allocator_Panic_Reason reason)
{
    switch(reason)
    {
        case DEBUG_ALLOC_PANIC_NONE: return "DEBUG_ALLOC_PANIC_NONE";

        case DEBUG_ALLOC_PANIC_INVALID_PTR: return "DEBUG_ALLOC_PANIC_INVALID_PTR";

        case DEBUG_ALLOC_PANIC_INVALID_PARAMS: return "DEBUG_ALLOC_PANIC_INVALID_PARAMS"; 

        case DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK: return "DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK";
        case DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK: return "DEBUG_ALLOC_PANIC_OVERWRITE_AFTER_BLOCK";
        case DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED: return "DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED";

        default: return "DEBUG_ALLOC_PANIC_NONE";
    }
}

//@TODO: penetration handling!
EXPORT void debug_allocator_panic_func(Debug_Allocator* allocator, Debug_Allocator_Panic_Reason reason, Debug_Allocation allocation, isize penetration, Source_Info called_from, void* context)
{   
    (void) context;
    (void) penetration;
    const char* reason_str = debug_allocator_panic_reason_to_string(reason);

    LOG_FATAL("MEMORY", "PANIC because of %s at pointer 0x%08llx " SOURCE_INFO_FMT, reason_str, (lli) allocation.ptr, SOURCE_INFO_PRINT(called_from));
    debug_allocator_print_alive_allocations("MEMORY", LOG_TRACE, *allocator, 0);
    
    log_flush();
    platform_debug_break();
    abort();
}

typedef struct Debug_Allocation_Pre_Block
{
    Debug_Allocation_Header* header;
    void* user_ptr;
    void** call_stack;
    u8* dead_zone;
    isize dead_zone_size;
    isize call_stack_size;
} Debug_Allocation_Pre_Block;

typedef struct Debug_Allocation_Post_Block
{
    u8* dead_zone;
    isize dead_zone_size;
} Debug_Allocation_Post_Block;

typedef struct Debug_Allocation_Block
{
    Debug_Allocation_Pre_Block pre;
    Debug_Allocation_Post_Block post;
} Debug_Allocation_Block;

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

//if block == NULL
//  doesnt actually place anything just calculates needed size, saves it and return NULL
//else
//  places block and saves info to block
//  Fills in just the necessary info for orientation (size align and start offset)
INTERNAL isize _debug_allocator_place_block(const Debug_Allocator* self, void* block_ptr, isize size, isize align, Debug_Allocation_Block* block)
{
    isize preamble_size = (isize) sizeof(Debug_Allocation_Header) + self->dead_zone_size + self->captured_callstack_size * (isize) sizeof(void*);
    isize postamble_size = self->dead_zone_size;
    isize total_size = preamble_size + postamble_size + align + size;

    //Kinda rude check but alas we dont allow 0 sized blocks so this is okay
    if(size == 0)
        return 0;

    if(block == NULL)
        return total_size;

    isize fixed_align = MAX(align, DEF_ALIGN);
    u8* user_ptr = (u8*) align_forward((u8*) block_ptr + preamble_size, fixed_align);

    Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, user_ptr);
    Debug_Allocation_Post_Block post = _debug_allocator_get_post_block(self, user_ptr, size);

    pre.header->align = (i32) align;
    pre.header->size = size;
    pre.header->block_start_offset = (i32) ((u8*) pre.header - (u8*) block_ptr);
    ASSERT(pre.header->block_start_offset <= align && "must be less then align");

    block->pre = pre;
    block->post = post;
    
    return total_size;
}

INTERNAL void* _debug_allocator_get_placed_block(const Debug_Allocator* self, void* user_ptr)
{
    Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, user_ptr);
    u8* block = (u8*) pre.header - pre.header->block_start_offset;
    return block;
}

EXPORT Debug_Allocation debug_allocator_get_allocation(const Debug_Allocator* self, void* user_ptr)
{
    Debug_Allocation allocation = {self->parent};
    Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, user_ptr);
    allocation.align = pre.header->align;
    allocation.size = pre.header->size;

    allocation.allocation_source = pre.header->allocation_source;
    allocation.allocation_time_s = pre.header->allocation_time_s;
    allocation.ptr = pre.user_ptr;
    array_init(&allocation.allocation_trace, self->parent);
    array_reserve(&allocation.allocation_trace, pre.call_stack_size);

    for(isize i = 0; i < pre.call_stack_size; i++)
    {
        if(pre.call_stack[i] == NULL)
            break;

        array_push(&allocation.allocation_trace, pre.call_stack[i]);
    }

    return allocation;
}

INTERNAL int _debug_allocation_alloc_time_compare(const void* a, const void* b)
{
    Debug_Allocation* a_ = (Debug_Allocation*) a;
    Debug_Allocation* b_ = (Debug_Allocation*) b;

    if(a_->allocation_time_s < b_->allocation_time_s)
        return -1;
    else
        return 1;
}

INTERNAL isize _debug_allocator_find_allocation(const Debug_Allocator* self, void* ptr)
{
    u64 hashed = hash64((u64) ptr);
    isize hash_found = hash_index64_find(self->alive_allocations_hash, hashed);
    
    for(isize counter = 0; counter < self->alive_allocations_hash.entries_count; counter ++)
    {
        if(hash_found == -1)
            break;
        void* found_ptr = (void*) self->alive_allocations_hash.entries[hash_found].value;
        if(found_ptr == ptr)
            break;

        isize finished_at = 0;
        hash_found = hash_index64_find_next(self->alive_allocations_hash, hashed, hash_found, &finished_at);
    }

    return hash_found;
}

INTERNAL Debug_Allocator_Panic_Reason _debug_allocator_check_block(const Debug_Allocator* self, void* user_ptr, isize* interpenetration, isize* hash_found)
{
    *interpenetration = 0;

    *hash_found = _debug_allocator_find_allocation(self, user_ptr);
    if(*hash_found == -1)
    {
        return DEBUG_ALLOC_PANIC_INVALID_PTR;
    }
        
    Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, user_ptr);
    for(isize i = pre.dead_zone_size; i-- > 0; )
    {
        if(pre.dead_zone[i] != DEBUG_ALLOCATOR_MAGIC_NUM8)
        {
            *interpenetration = i;
            return DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK;
        }
    }
        
    if(pre.header->align <= 0 
        || pre.header->size <= 0)
        return DEBUG_ALLOC_PANIC_OVERWRITE_BEFORE_BLOCK;
        
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
    (void) user_ptr;
    (void) allocator;
    #ifndef NDEBUG
        isize interpenetration = 0;
        isize found = 0;
        Debug_Allocator_Panic_Reason reason = _debug_allocator_check_block(allocator, user_ptr, &interpenetration, &found);
        ASSERT(reason == DEBUG_ALLOC_PANIC_NONE);
    #endif // !NDEBUG
}

INTERNAL bool _debug_allocator_is_invariant(const Debug_Allocator* allocator)
{
    ASSERT(allocator->dead_zone_size/DEF_ALIGN*DEF_ALIGN == allocator->dead_zone_size 
        && "dead zone size must be a valid multiple of alignment"
        && "this is so that the pointers within the header will be properly aligned!");

    // ASSERT(allocator->is_within_allocation == false);
    //All alive allocations must be in hash
    if(allocator->do_contnual_checks)
    {
        isize size_sum = 0;
        for(isize i = 0; i < allocator->alive_allocations_hash.entries_count; i ++)
        {
            Hash_Index64_Entry curr = allocator->alive_allocations_hash.entries[i];
            if(hash_index64_is_entry_used(curr) == false)
                continue;
        
            isize interpenetration = 0;
            isize hash_found = 0;
            void* user_ptr = (void*) curr.value;
            Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(allocator, user_ptr);
            Debug_Allocator_Panic_Reason reason = _debug_allocator_check_block(allocator, user_ptr, &interpenetration, &hash_found);
            ASSERT(reason == DEBUG_ALLOC_PANIC_NONE);

            size_sum += pre.header->size;
        }

        ASSERT(size_sum == allocator->bytes_allocated);
        ASSERT(size_sum <= allocator->max_bytes_allocated);
    }

    return true;
}


EXPORT void debug_allocator_deinit(Debug_Allocator* allocator)
{
    if(allocator->bytes_allocated != 0 && allocator->do_deinit_leak_check)
        _debug_allocator_panic(allocator, DEBUG_ALLOC_PANIC_DEINIT_MEMORY_LEAKED, NULL, SOURCE_INFO());

    for(isize i = 0; i < allocator->alive_allocations_hash.entries_count; i++)
    {
        Hash_Index64_Entry entry = allocator->alive_allocations_hash.entries[i];
        if(hash_index64_is_entry_used(entry))
        {
            void* ptr = (void*) entry.value;
            
            Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(allocator, ptr);
            debug_allocator_allocate(&allocator->allocator, 0, ptr, pre.header->size, pre.header->align, SOURCE_INFO());
        }
    }

    allocator_set(allocator->allocator_backup);
    debug_allocator_deinit_allocation_array(&allocator->dead_allocations);
    hash_index64_deinit(&allocator->alive_allocations_hash);
    
    Debug_Allocator null = {0};
    *allocator = null;
}

EXPORT Debug_Allocation_Array debug_allocator_get_alive_allocations(const Debug_Allocator allocator, isize print_max)
{
    isize count = print_max;
    const Hash_Index64* hash = &allocator.alive_allocations_hash;
    if(count <= 0)
        count = hash->size;
        
    if(count >= hash->size)
        count = hash->size;
        
    Debug_Allocation_Array out = {allocator.parent};
    for(isize i = 0; i < hash->entries_count; i++)
    {
        if(hash_index64_is_entry_used(hash->entries[i]))
        {
            void* user_ptr = (void*) hash->entries[i].value;
            _debug_allocator_assert_block(&allocator, user_ptr);
            Debug_Allocation allocation = debug_allocator_get_allocation(&allocator, user_ptr);
            array_push(&out, allocation);
        }
    }
    
    qsort(out.data, (size_t) out.size, sizeof *out.data, _debug_allocation_alloc_time_compare);

    //ASSERT(out.size <= count);
    array_resize(&out, count);

    return out;
}

EXPORT Debug_Allocation_Array debug_allocator_get_dead_allocations(const Debug_Allocator allocator, isize get_max)
{   
    if(get_max <= 0)
        get_max = allocator.dead_allocation_index;

    isize count = MIN(get_max, allocator.dead_allocation_max);
    count = MIN(count, allocator.dead_allocation_index);

    Debug_Allocation_Array out = {allocator.parent};
    for(isize i = 0; i < count; i++)
    {
        isize index = (allocator.dead_allocation_index - i - 1) % allocator.dead_allocation_max;
        CHECK_BOUNDS(index, allocator.dead_allocations.size);
        Debug_Allocation curr = allocator.dead_allocations.data[index];

        array_push(&out, curr);
    }

    for(isize i = 0; i < out.size - 1; i++)
    {
        CHECK_BOUNDS(i,     out.size);
        CHECK_BOUNDS(i + 1, out.size);
        ASSERT(out.data[i].deallocation_time_s > out.data[i + 1].deallocation_time_s && "deallocations should be already sorted so that the most recent one is first!");
    }
    
    return out;
}
EXPORT void debug_allocator_deinit_allocation(Debug_Allocation* allocation)
{
    array_deinit(&allocation->allocation_trace);
    array_deinit(&allocation->deallocation_trace);
}

EXPORT void debug_allocator_deinit_allocation_array(Debug_Allocation_Array* allocations)
{
    for(isize i = 0; i < allocations->size; i++)
        debug_allocator_deinit_allocation(&allocations->data[i]);

    array_deinit(allocations);
}

EXPORT void debug_allocator_print_alive_allocations(const char* log_module, Log_Type log_type, const Debug_Allocator allocator, isize print_max)
{
    _debug_allocator_is_invariant(&allocator);
    
    Debug_Allocation_Array alive = debug_allocator_get_alive_allocations(allocator, print_max);
    if(print_max > 0)
        ASSERT(alive.size <= print_max);

    LOG(log_module, log_type, "printing ALIVE allocations (%lli) below:", (lli)alive.size);
    log_group_push();

    for(isize i = 0; i < alive.size; i++)
    {
        Debug_Allocation curr = alive.data[i];
        LOG(log_module, log_type, "%-3lli - size %-8lli ptr: 0x%08llx align: %-2lli" SOURCE_INFO_FMT,
            (lli) i, (lli) curr.size, (lli) curr.ptr, (lli) curr.align, SOURCE_INFO_PRINT(curr.allocation_source));
     
        if(allocator.captured_callstack_size > 0)
        {
            log_group_push();
            log_captured_callstack(log_module, log_type, curr.allocation_trace.data, curr.allocation_trace.size);
            log_group_pop();
        }
    }

    log_group_pop();
    debug_allocator_deinit_allocation_array(&alive);
}



EXPORT void debug_allocator_print_dead_allocations(const char* log_module, Log_Type log_type, const Debug_Allocator allocator, isize print_max)
{
    Debug_Allocation_Array dead = debug_allocator_get_dead_allocations(allocator, print_max);
    if(print_max > 0)
        ASSERT(dead.size <= print_max);

    LOG(log_module, log_type, "printing DEAD allocations (%lli) below:", (lli)dead.size);
    
    for(isize i = 0; i < dead.size; i++)
    {
        Debug_Allocation curr = dead.data[i];
        Source_Info from_source = curr.allocation_source;
        Source_Info to_source = curr.deallocation_source;
        bool files_match = false;
        if(from_source.file != NULL && to_source.file != NULL)
            files_match = strcmp(from_source.file, to_source.file) == 0;

        if(files_match)
        {
            LOG(log_module, log_type, "%-3lli - size %-8lli ptr: 0x%08llx align: %-2lli (%s : %3lli -> %3lli)",
                (lli) i, (lli) curr.size, (lli) curr.ptr, curr.align,
                to_source.file, (lli) from_source.line, (lli) to_source.line);
        }
        else
        {
            LOG(log_module, log_type, "%-3lli - size %-8lli ptr: 0x%08llx align: %-2lli\n"
                "[%-3lli] " SOURCE_INFO_FMT " -> " SOURCE_INFO_FMT,
                (lli) i, (lli) curr.size, (lli) curr.ptr, (lli) curr.align,
                (lli) i, SOURCE_INFO_PRINT(from_source), SOURCE_INFO_PRINT(to_source));
        }
        
        if(allocator.captured_callstack_size > 0)
        {
            log_group_push();
                LOG(log_module, log_type, "allocation callstack (%lli):", (lli) curr.allocation_trace.size);
                log_group_push();
                log_captured_callstack(log_module, log_type, (const void**) curr.allocation_trace.data, curr.allocation_trace.size);
                log_group_pop();

                LOG(log_module, log_type, "deallocation callstack (%lli):", (lli) curr.deallocation_trace.size);
                log_group_push();
                log_captured_callstack(log_module, log_type, (const void**) curr.deallocation_trace.data, curr.deallocation_trace.size);
                log_group_pop();
            log_group_pop();
        }
    }
    
    debug_allocator_deinit_allocation_array(&dead);
}

INTERNAL void* _debug_allocator_panic(Debug_Allocator* self, Debug_Allocator_Panic_Reason reason, void* ptr, Source_Info called_from)
{
    //#error @TODO: add reason and interpenetration
    Debug_Allocation allocation = {0};
    allocation.ptr = ptr;

    if(self->panic_handler != NULL)
        self->panic_handler(self, reason, allocation, 0, called_from, self->panic_context);
    else
        debug_allocator_panic_func(self, reason, allocation, 0, called_from, self->panic_context);

    return NULL;
}

INTERNAL bool _debug_allocator_is_aligned(void* ptr, isize alignment)
{
    return ptr == align_backward(ptr, alignment);
}



void print_pre(Debug_Allocation_Pre_Block pre, const char* c)
{
    LOG_DEBUG("DEBUG", "printing pre block \"%s\" from: " SOURCE_INFO_FMT, c, SOURCE_INFO_PRINT(pre.header->allocation_source));
        LOG_DEBUG(">DEBUG", "header:             0x%08llx", (lli) pre.header);
        LOG_DEBUG(">DEBUG", "user_ptr:           0x%08llx", (lli) pre.user_ptr);
        LOG_DEBUG(">DEBUG", "call_stack:         0x%08llx", (lli) pre.call_stack);
        LOG_DEBUG(">DEBUG", "dead_zone:          0x%08llx", (lli) pre.dead_zone);
        LOG_DEBUG(">DEBUG", "dead_zone_size:     %lli", (lli) pre.dead_zone_size);
        LOG_DEBUG(">DEBUG", "call_stack_size:    %lli", (lli) pre.call_stack_size);
            LOG_DEBUG(">>DEBUG", "header.size:                %lli", (lli) pre.header->size);
            LOG_DEBUG(">>DEBUG", "header.align:               %lli", (lli) pre.header->align);
            LOG_DEBUG(">>DEBUG", "header.block_start_offset:  %lli", (lli) pre.header->block_start_offset);
            LOG_DEBUG(">>DEBUG", "header.allocation_time_s:   %lf", pre.header->allocation_time_s);
}

EXPORT void* debug_allocator_allocate(Allocator* self_, isize new_size, void* old_ptr_, isize old_size, isize align, Source_Info called_from)
{
    PERF_COUNTER_START(c);
    //This function executes the following steps. 
    // The order is crucial because we need to ensure that: 
    //  1: all inputs are checked
    //  2: no changes are commit before the allocation because it might fail
    //  3: there is ever only one block with the given adress => block needs to be removed then added back 
    // 
    // STEPS:
    // if old_ptr is given: 
    //    check its validty 
    //    check the block it points to for overwrites etc.
    // 
    // reallocate using parent allocator
    // 
    // if failed: 
    //    return NULL (error!)
    // 
    // if deallocating: 
    //    remove from control structure
    // 
    // if allocating: 
    //    init new memory block (if is reallocating simply overrides)
    //    add to control structures
    //
    // prints the allocation (optional)
    // updates statistics

    f64 curr_time = clock_s();
    Debug_Allocator* self = (Debug_Allocator*) (void*) self_;
    _debug_allocator_is_invariant(self);

    isize total_new_size = _debug_allocator_place_block(self, NULL, new_size, align, NULL);
    isize total_old_size = _debug_allocator_place_block(self, NULL, old_size, align, NULL);

    u8* old_block_ptr = NULL;
    u8* new_block_ptr = NULL;
    u8* old_ptr = (u8*) old_ptr_;
    u8* new_ptr = NULL;
    Debug_Allocation old_allocation = {0};

    isize hash_found = -1;

    //Check old_ptr if any for correctness
    if(old_ptr != NULL)
    {
        Debug_Allocation_Pre_Block pre = _debug_allocator_get_pre_block(self, old_ptr);
        //print_pre(pre, "before");
        ASSERT(_debug_allocator_is_aligned(pre.header, DEF_ALIGN));

        if(self->dead_allocation_max > 0)
        {
            old_allocation = debug_allocator_get_allocation(self, old_ptr);
            old_allocation.deallocation_time_s = curr_time;
            old_allocation.deallocation_source = called_from;

            if(self->captured_callstack_size > 0)
            {
                array_init(&old_allocation.deallocation_trace, self->parent);
                array_resize(&old_allocation.deallocation_trace, self->captured_callstack_size);
                isize captured = platform_capture_call_stack(old_allocation.deallocation_trace.data, old_allocation.deallocation_trace.size, 1);
                array_resize(&old_allocation.deallocation_trace, captured);
            }
        }

        isize interpenetration = 0;
        Debug_Allocator_Panic_Reason reason = _debug_allocator_check_block(self, old_ptr, &interpenetration, &hash_found);
        if(reason != DEBUG_ALLOC_PANIC_NONE)
        {
            PERF_COUNTER_END(c);
            return _debug_allocator_panic(self, reason, old_ptr, called_from);
        }

        if(pre.header->size != old_size 
            || pre.header->align != align)
        {    
            PERF_COUNTER_END(c);
            return _debug_allocator_panic(self, DEBUG_ALLOC_PANIC_INVALID_PARAMS, old_ptr, called_from);
        }

        old_block_ptr = (u8*) _debug_allocator_get_placed_block(self, old_ptr);
    }

    new_block_ptr = (u8*) self->parent->allocate(self->parent, total_new_size, old_block_ptr, total_old_size, DEF_ALIGN, called_from);
    //new_block_ptr = (u8*) self->parent->allocate(self->parent, total_new_size, old_block_ptr, total_old_size, DEF_ALIGN, called_from);
    
    //if failed return failiure and do nothing
    if(new_block_ptr == NULL && new_size != 0)
    {
        debug_allocator_deinit_allocation(&old_allocation);
        PERF_COUNTER_END(c);
        return NULL;
    }
    
    //if previous block existed remove it from controll structures
    if(old_ptr != NULL)
    {
        ASSERT(hash_found != -1 && "must be found!");

        hash_index64_remove(&self->alive_allocations_hash, hash_found);

        if(self->dead_allocation_max > 0)
        {
            isize index = self->dead_allocation_index % self->dead_allocation_max;
            CHECK_BOUNDS(index, self->dead_allocations.size);
            
            debug_allocator_deinit_allocation(&self->dead_allocations.data[index]);

            self->dead_allocations.data[index] = old_allocation;
            self->dead_allocation_index += 1;
        }
    }

    //if allocated/reallocated (new block exists)
    if(new_size != 0)
    {
        Debug_Allocation_Block new_block = {0};
        _debug_allocator_place_block(self, new_block_ptr, new_size, align, &new_block);
        new_ptr = (u8*) new_block.pre.user_ptr;

        if(self->captured_callstack_size > 0)
            platform_capture_call_stack(new_block.pre.call_stack, new_block.pre.call_stack_size, 1);

        new_block.pre.header->allocation_source = called_from;
        new_block.pre.header->allocation_time_s = curr_time;
        memset(new_block.pre.dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_block.pre.dead_zone_size);
        memset(new_block.post.dead_zone, DEBUG_ALLOCATOR_MAGIC_NUM8, (size_t) new_block.post.dead_zone_size);
        
        ASSERT(_debug_allocator_find_allocation(self, new_ptr) == -1 && "Must not be added already!");

        u64 hashed = hash64((u64) new_ptr);

        hash_index64_insert(&self->alive_allocations_hash, hashed, (u64) new_ptr);
        _debug_allocator_assert_block(self, new_ptr);

        //print_pre(new_block.pre, "after");
    
    }

    self->bytes_allocated -= old_size;
    self->bytes_allocated += new_size;
    self->max_bytes_allocated = MAX(self->max_bytes_allocated, self->bytes_allocated);
    
    if(self->do_printing && self->is_within_allocation == false)
    {
        self->is_within_allocation = true;
        LOG_DEBUG("MEMORY", "size %6lli -> %-6lli ptr: 0x%08llx -> 0x%08llx align: %lli " SOURCE_INFO_FMT,
            (lli) old_size, (lli) new_size, (lli) old_ptr, (lli) new_ptr, (lli) align, SOURCE_INFO_PRINT(called_from));
        self->is_within_allocation = false;
    }

    if(old_ptr == NULL)
        self->allocation_count += 1;
    else if(new_size == 0)
        self->deallocation_count += 1;
    else
        self->reallocation_count += 1;
    
    self->is_within_allocation = false; //just to be sure...
    _debug_allocator_is_invariant(self);
    PERF_COUNTER_END(c);
    return new_ptr;
}

EXPORT Allocator_Stats debug_allocator_get_stats(Allocator* self_)
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