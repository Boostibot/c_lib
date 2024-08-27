#ifndef JOT_ARENA
#define JOT_ARENA

#include "allocator.h"
#include "profile_defs.h"

#define ARENA_DEF_RESERVE_SIZE 64 * GB 
#define ARENA_DEF_COMMIT_SIZE  8 * MB 

//Contiguous chunk of virtual memory. 
// This struct is combination of two separate concepts (for simplicity of implementation):
//  1: Normal arena interface - push/pop/reset etc.
//  2: Allocator capable of storing a **SINGLE** growing/shrinking allocation.
//       Allows the allocation to be reallocated up or down within the arena.
//       Can be used to make certain data structures stable in memory without any change.
//       An example of this includes Array, Hash, String_Builder, Path...
typedef struct Arena {
    Allocator alloc[1];

    u8* data;
    u8* used_to;
    u8* commit_to;
    u8* reserved_to;
    isize commit_granularity;

    const char* name;
} Arena;

EXTERNAL Platform_Error arena_init(Arena* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero);
EXTERNAL void arena_deinit(Arena* arena);
EXTERNAL void* arena_push_nonzero(Arena* arena, isize size, isize align, Allocator_Error* error_or_null);
EXTERNAL void* arena_push(Arena* arena, isize size, isize align);
EXTERNAL void arena_reset_ptr(Arena* arena, const void* position);
EXTERNAL void arena_commit_ptr(Arena* arena, const void* position, Allocator_Error* error_or_null);
EXTERNAL void arena_reset(Arena* arena, isize to);
EXTERNAL void arena_commit(Arena* arena, isize to);

EXTERNAL void* arena_single_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* stats);
EXTERNAL Allocator_Stats arena_single_allocator_get_stats(Allocator* self);

#define ARENA_PUSH(arena_ptr, count, Type) ((Type*) arena_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARENA_IMPL)) && !defined(JOT_ARENA_HAS_IMPL)
#define JOT_ARENA_HAS_IMPL

EXTERNAL Platform_Error arena_init(Arena* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero)
{
    arena_deinit(arena);
    isize alloc_granularity = platform_allocation_granularity();
    
    ASSERT(reserve_size_or_zero >= 0);
    ASSERT(commit_granularity_or_zero >= 0);
    ASSERT(alloc_granularity >= 0);

    isize reserve_size = reserve_size_or_zero > 0 ? reserve_size_or_zero : ARENA_DEF_RESERVE_SIZE;
    isize commit_granularity = commit_granularity_or_zero > 0 ? commit_granularity_or_zero : ARENA_DEF_COMMIT_SIZE;

    reserve_size = DIV_CEIL(reserve_size, alloc_granularity)*alloc_granularity;
    commit_granularity = DIV_CEIL(commit_granularity, alloc_granularity)*alloc_granularity;

    u8* data = NULL;
    Platform_Error error = platform_virtual_reallocate((void**) &data, NULL, reserve_size, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_NO_ACCESS);
    if(error == 0)
    {
        arena->alloc[0].func = arena_single_allocator_func;
        arena->alloc[0].get_stats = arena_single_allocator_get_stats;

        arena->data = data;
        arena->used_to = data;
        arena->commit_to = data;
        arena->reserved_to = data + reserve_size;
        arena->commit_granularity = commit_granularity;
        arena->name = name;
    }
    return error;
}

EXTERNAL void arena_deinit(Arena* arena)
{
    if(arena->data)
        platform_virtual_reallocate(NULL, arena->data, arena->reserved_to - arena->data, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);

    memset(arena, 0, sizeof *arena);
}

INTERNAL ATTRIBUTE_INLINE_NEVER void _arena_commit_no_inline(Arena* arena, const void* to, Allocator_Error* error_or_null)
{
    PROFILE_START();
    {
        isize size = (u8*) to - arena->commit_to;
        isize commit = DIV_CEIL(size, arena->commit_granularity)*arena->commit_granularity;

        u8* new_commit_to = arena->used_to + commit;
        if(new_commit_to > arena->reserved_to)
        {
            allocator_error(error_or_null, ALLOCATOR_ERROR_OUT_OF_MEM, arena->alloc, size, NULL, 0, 1, 
                "More memory is needed then reserved! Reserved: %.2lf MB, commit: %.2lf MB", 
                (double) (arena->reserved_to - arena->data)/MB, (double) (arena->commit_to - arena->data)/MB);
            goto end;
        }
            
        Platform_Error platform_error = platform_virtual_reallocate(NULL, arena->commit_to, new_commit_to - arena->commit_to, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
        if(platform_error)
        {
            allocator_error(error_or_null, ALLOCATOR_ERROR_OUT_OF_MEM, arena->alloc, size, NULL, 0, 1, 
                "Virtual memory commit failed! Error: %s", platform_translate_error(platform_error));
            goto end;
        }

        arena->commit_to = new_commit_to;
    }
    end:
    PROFILE_END();
}

EXTERNAL void arena_commit_ptr(Arena* arena, const void* to, Allocator_Error* error_or_null)
{
    //If + function call to not pollute the call site with code that will 
    // get executed extremely rarely (probably about twice)
    if((u8*) to > arena->commit_to)
        _arena_commit_no_inline(arena, to, error_or_null);
}

EXTERNAL void* arena_push_nonzero(Arena* arena, isize size, isize align, Allocator_Error* error_or_null)
{
    u8* out = (u8*) align_forward(arena->used_to, align);
    arena_commit_ptr(arena, out + size, error_or_null);
    arena->used_to = out + size;
    return out;
}

EXTERNAL void* arena_push(Arena* arena, isize size, isize align)
{
    void* out = arena_push_nonzero(arena, size, align, NULL);
    memset(out, 0, size);
    return out;
}

EXTERNAL void arena_reset_ptr(Arena* arena, const void* position)
{
    arena->used_to = (u8*) position;
}

EXTERNAL void arena_reset(Arena* arena, isize to)
{
    arena_reset_ptr(arena, arena->data + to);
}

EXTERNAL void arena_commit(Arena* arena, isize to)
{
    arena_commit_ptr(arena, arena->data + to, NULL);
}

EXTERNAL void* arena_single_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
{
    Arena* arena = (Arena*) (void*) self;

    ASSERT(old_ptr == arena->data);
    ASSERT(old_size == arena->used_to - arena->data);
    ASSERT(is_power_of_two(align));

    arena_reset_ptr(arena, arena->data);
    return arena_push_nonzero(arena, new_size, align, error);
}

EXTERNAL Allocator_Stats arena_single_allocator_get_stats(Allocator* self)
{
    Arena* arena = (Arena*) (void*) self;
    Allocator_Stats stats = {0};
    stats.is_top_level = true;
    stats.type_name = "Arena";
    stats.name = arena->name;
    stats.fixed_memory_pool_size = arena->reserved_to - arena->data; 
    stats.bytes_allocated = arena->used_to - arena->data;
    return stats;
}
#endif