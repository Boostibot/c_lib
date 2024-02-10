#ifndef JOT_ARENA
#define JOT_ARENA

#include "defines.h"
#include "assert.h"
#include "platform.h"

typedef struct Allocator        Allocator;
typedef struct Allocator_Stats  Allocator_Stats;

typedef void* (*Allocator_Allocate_Func)(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
typedef Allocator_Stats (*Allocator_Get_Stats_Func)(Allocator* self);

typedef struct Allocator {
    Allocator_Allocate_Func allocate;
    Allocator_Get_Stats_Func get_stats;
} Allocator;

typedef struct Allocator_Stats {
    //The allocator used to obtain memory reisributed by this allocator.
    //If is_top_level is set this should probably be NULL
    Allocator* parent;
    //Human readable name of the type 
    const char* type_name;
    //Optional human readable name of this specific allocator
    const char* name;
    //if doesnt use any other allocator to obtain its memory. For example malloc allocator or VM memory allocator have this set.
    bool is_top_level; 

    //The number of bytes given out to the program by this allocator. (does NOT include book keeping bytes).
    //Might not be totally accurate but is required to be localy stable - if we allocate 100B and then deallocate 100B this should not change.
    //This can be used to accurately track memory leaks. (Note that if this field is simply not set and thus is 0 the above property is satisfied)
    isize bytes_allocated;
    isize max_bytes_allocated;  //maximum bytes_allocated during the enire lifetime of the allocator

    isize allocation_count;     //The number of allocation requests (old_ptr == NULL). Does not include reallocs!
    isize deallocation_count;   //The number of deallocation requests (new_size == 0). Does not include reallocs!
    isize reallocation_count;   //The number of reallocation requests (*else*).
} Allocator_Stats;

typedef isize (*Arena_Stack_Commit_Func)(void* addr, isize size, isize reserved_size, bool commit);

#define ARENA_DEF_STACK_SIZE   128
#define ARENA_DEF_RESERVE_SIZE (isize) 64 * 1024*1024*1024 //GB
#define ARENA_DEF_COMMIT_SIZE  (isize) 8 * 1024*1024 //MB

typedef struct Arena_Stack {
    //Info
    const char* name;
    isize max_release_from_size;
    isize acquasition_count;
    isize release_count;

    //Actually useful data
    i32 stack_depht;
    i32 stack_max_depth;
    isize used_to;

    u8* data;
    isize size;

    isize reserved_size;
    Arena_Stack_Commit_Func commit;
} Arena_Stack;

typedef struct Arena {
    Allocator allocator;
    Arena_Stack* stack;
    i32 level;
} Arena;

void arena_init(Arena_Stack* arena, isize reserve_size_or_zero, isize stack_max_depth_or_zero, const char* name_or_null);
void arena_init_custom(Arena_Stack* arena, void* data, isize size, isize reserved_size, Arena_Stack_Commit_Func commit_or_null, isize stack_max_depth_or_zero);
void arena_deinit(Arena_Stack* arena);
void arena_release(Arena* arena);

Arena arena_acquire(Arena_Stack* stack);
void* arena_push(Arena* arena, isize size, isize align);
void* arena_push_nonzero(Arena* arena, isize size, isize align);
MODIFIER_FORCE_INLINE void* arena_push_nonzero_inline(Arena* arena, isize size, isize align);

void* arena_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
Allocator_Stats arena_get_allocatator_stats(Allocator* self);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARENA_IMPL)) && !defined(JOT_ARENA_HAS_IMPL)
#define JOT_ARENA_HAS_IMPL

#ifdef NDEBUG
#define ARENA_DEBUG 0
#else
#define ARENA_DEBUG 1
#endif
#define ARENA_DEBUG_DATA_PATTERN 0x55
#define ARENA_DEBUG_DATA_SIZE 64
#define ARENA_DEBUG_STACK_PATTERN 0x66

void _arena_debug_check_invarinats(Arena_Stack* arena);
void _arena_debug_fill_stack(Arena_Stack* arena);
void _arena_debug_fill_data(Arena_Stack* arena, isize size);
void* _align_backward(const void* data, isize align);
void* _align_forward(const void* data, isize align);

void arena_deinit(Arena_Stack* arena)
{
    _arena_debug_check_invarinats(arena);
    if(arena->commit && arena->data)
       arena->commit(arena->data, arena->size, arena->reserved_size, false);
       
    memset(arena, 0, sizeof *arena);
}
void arena_init_custom(Arena_Stack* arena, void* data, isize size, isize reserved_size, Arena_Stack_Commit_Func commit_or_null, isize stack_max_depth_or_zero)
{
    arena_deinit(arena);
    isize stack_max_depth = stack_max_depth_or_zero;

    isize* stack = (isize*) data;
    u8* aligned_data = (u8*) _align_forward(data, sizeof *stack);
    isize aligned_size = size - (aligned_data - (u8*) data);
    isize stack_max_depth_that_fits = aligned_size / sizeof *stack;

    if(stack_max_depth <= 0)
        stack_max_depth = ARENA_DEF_STACK_SIZE;
    if(stack_max_depth > stack_max_depth_that_fits)
        stack_max_depth = stack_max_depth_that_fits;

    arena->used_to = stack_max_depth * sizeof *stack;
    arena->data = aligned_data;
    arena->size = aligned_size;
    arena->stack_max_depth = (i32) stack_max_depth;
    arena->commit = commit_or_null;
    arena->reserved_size = reserved_size;

    _arena_debug_fill_stack(arena);
    _arena_debug_fill_data(arena, INT64_MAX);
    _arena_debug_check_invarinats(arena);
}

//TODO: remove needless generality
isize arena_def_commit_func(void* addr, isize size, isize reserved_size, bool commit)
{
    isize commit_to = 0;
    if(commit == false)
    {
        platform_virtual_reallocate((u8*) addr, reserved_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_READ_WRITE);
    }
    else
    {
        commit_to = MIN(size + ARENA_DEF_COMMIT_SIZE, reserved_size);
        if(commit_to - size > 0)
            platform_virtual_reallocate((u8*) addr + size, commit_to - size, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);

    }
    return commit_to;
}

void arena_init(Arena_Stack* arena, isize reserve_size_or_zero, isize stack_max_depth_or_zero, const char* name_or_null)
{
    if(reserve_size_or_zero <= 0)
        reserve_size_or_zero = ARENA_DEF_RESERVE_SIZE;

    void* data = platform_virtual_reallocate(NULL, reserve_size_or_zero, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_READ_WRITE);
    isize size = arena_def_commit_func(data, 0, reserve_size_or_zero, true);
    arena_init_custom(arena, data, size, reserve_size_or_zero, arena_def_commit_func, stack_max_depth_or_zero);
    arena->name = name_or_null;
}

MODIFIER_NO_INLINE void* arena_unusual_push(Arena_Stack* arena, i32 depth, isize size, isize align)
{
    if(arena->stack_depht != depth)
    {
        ASSERT(arena->stack_depht < arena->stack_max_depth);

        i32 to_depth = MIN(depth, arena->stack_max_depth);
        isize* stack = (isize*) (void*) arena->data;
        for(i32 i = arena->stack_depht; i < to_depth; i++)
            stack[i] = arena->used_to;

        arena->stack_depht = to_depth;
        _arena_debug_fill_stack(arena);
        _arena_debug_check_invarinats(arena);
    }

    u8* data = (u8*) _align_forward(arena->data + arena->used_to, align);
    u8* data_end = data + size;
    if(data_end > arena->data + arena->size)
    {
        if(arena->commit)
        {
            isize new_size = arena->commit(arena->data, arena->size, arena->reserved_size, true);
            if(new_size > arena->size)
            {
                _arena_debug_fill_data(arena, new_size - arena->size);
                arena->size = new_size;
                ASSERT(new_size < arena->reserved_size);
                return arena_unusual_push(arena, depth, size, align);
            }
        }

        return NULL;
    }
    
    arena->used_to = (isize) data_end - (isize) arena->data;
    _arena_debug_check_invarinats(arena);
    return data;
}

MODIFIER_FORCE_INLINE void* arena_push_nonzero_inline(Arena* arena, isize size, isize align)
{
    ASSERT(arena->stack && arena->level > 0);
    Arena_Stack* stack = arena->stack;
    _arena_debug_check_invarinats(stack);
    if(stack->stack_depht != arena->level || stack->used_to + size + align > stack->size)
        return arena_unusual_push(stack, arena->level, size, align);
    
    u8* data = (u8*) _align_forward(stack->data + stack->used_to, align);
    stack->used_to = (isize) (data + size) - (isize) stack->data;
    ASSERT(stack->used_to <= stack->size);
    
    return data;
}

void* arena_push_nonzero(Arena* arena, isize size, isize align)
{
    return arena_push_nonzero_inline(arena, size, align);
}

void* arena_push(Arena* arena, isize size, isize align)
{
    void* ptr = arena_push_nonzero(arena, size, align);
    memset(ptr, 0, size);
    return ptr;
}

void arena_release(Arena* arena)
{
    Arena_Stack* stack = arena->stack;
    ASSERT(arena->stack && arena->level > 0);
    _arena_debug_check_invarinats(stack);
    if(arena->level <= 0 || stack->stack_depht < arena->level)
        return;

    isize* levels = (isize*) (void*) stack->data;
    isize new_used_to = levels[arena->level - 1];
    isize old_used_to = stack->used_to;

    isize stack_bytes = stack->stack_max_depth * (isize) sizeof *levels;
    ASSERT(stack_bytes <= new_used_to && new_used_to <= stack->used_to);

    if(stack->max_release_from_size < stack->used_to)
        stack->max_release_from_size = stack->used_to;

    stack->used_to = new_used_to;
    stack->stack_depht = arena->level - 1;
    stack->release_count += 1;

    _arena_debug_fill_data(stack, old_used_to - new_used_to);
    _arena_debug_check_invarinats(stack);
    memset(arena, 0, sizeof* arena);
}

//Compatibility function for the allocator interface
void* arena_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
{
    (void) old_ptr;
    (void) old_size;
    Arena* arena = (Arena*) (void*) self;
    return arena_push(arena, new_size, align);
}

Allocator_Stats arena_get_allocatator_stats(Allocator* self)
{
    Arena* arena = (Arena*) (void*) self;
    Arena_Stack* stack = arena->stack;

    isize max_used_to = MAX(stack->used_to, stack->max_release_from_size);
    isize stack_to = stack->stack_max_depth * (isize) sizeof(isize);

    Allocator_Stats stats = {0};
    stats.type_name = "Arena";
    stats.name = stack->name;
    stats.is_top_level = true;
    stats.bytes_allocated = stack->used_to - stack_to;
    stats.max_bytes_allocated = max_used_to - stack_to;
    stats.allocation_count = stack->acquasition_count;
    stats.deallocation_count = stack->release_count;

    return stats;
}

Arena arena_acquire(Arena_Stack* stack)
{
    _arena_debug_check_invarinats(stack);
    i32 new_depth = stack->stack_depht + 1;
    isize* levels = (isize*) (void*) stack->data;
    if(new_depth <= stack->stack_max_depth)
    {
        levels[new_depth - 1] = stack->used_to;
        stack->stack_depht = new_depth;
    }

    stack->acquasition_count += 1;

    Arena out = {0};
    out.allocator.allocate = arena_reallocate;
    out.allocator.get_stats = arena_get_allocatator_stats;
    out.level = new_depth;
    out.stack = stack;
    return out;
}

int memcmp_byte(const void* ptr, int byte, isize size)
{
    isize i = 0;
    char* text = (char*) ptr;

    if((isize) ptr % 8 == 0)
    {
        //pattern is 8 repeats of byte
        u64 pattern = 0x0101010101010101ULL * (u64) byte;
        for(isize k = 0; k < size/8; k++)
            if(*(u64*) ptr != pattern)
                return (int) (k*8);
        
        i = size/8*8;
    }

    for(; i < size; i++)
        if(text[i] != (char) byte)
            return (int) i;
        
    return 0;
}

void _arena_debug_check_invarinats(Arena_Stack* arena)
{
    if(ARENA_DEBUG)
    {
        isize* stack = (isize*) (void*) arena->data;
        ASSERT_MSG(arena->stack_max_depth * (isize)sizeof *stack <= arena->used_to && arena->used_to <= arena->size, "used_to needs to be within [0, size]");
        ASSERT_MSG(0 <= arena->stack_depht && arena->stack_depht <= arena->stack_max_depth, "used_to stack_depht to be within [0, stack_max_depth]");

        ASSERT_MSG((arena->data == NULL) == (arena->size == 0), "only permitted to be NULL when zero sized");

        isize till_end = arena->size - arena->used_to;
        isize check_size = MIN(till_end, ARENA_DEBUG_DATA_SIZE);
        ASSERT_MSG(memcmp_byte(arena->data + arena->used_to, ARENA_DEBUG_DATA_PATTERN, check_size) == 0, "The memory after the arena needs not be corrupted!");
        ASSERT_MSG(memcmp_byte(stack + arena->stack_depht, ARENA_DEBUG_STACK_PATTERN, (arena->stack_max_depth - arena->stack_depht) * sizeof *stack) == 0, "The memory after stack needs to be valid");
    }
}

void _arena_debug_fill_stack(Arena_Stack* arena)
{
    isize* stack = (isize*) (void*) arena->data;
    if(ARENA_DEBUG)
        memset(stack + arena->stack_depht, ARENA_DEBUG_STACK_PATTERN, (arena->stack_max_depth - arena->stack_depht) * sizeof *stack);
}

void _arena_debug_fill_data(Arena_Stack* arena, isize size)
{
    if(ARENA_DEBUG)
    {
        isize till_end = arena->size - arena->used_to;
        isize check_size = MIN(till_end, size);
        memset(arena->data + arena->used_to, ARENA_DEBUG_DATA_PATTERN, check_size);
    }
}

void* _align_backward(const void* data, isize align)
{
    u64 mask = (u64) (align - 1);
    u64 aligned = (u64) data & ~mask;
    return (void*) aligned;
}

void* _align_forward(const void* data, isize align)
{
    return _align_backward((u8*) data + align - 1, align);
}
#endif