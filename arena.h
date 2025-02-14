#ifndef MODULE_ARENA
#define MODULE_ARENA

#include "allocator.h"
#include "profile.h"
#include "platform.h"
#include "assert.h"
#include "defines.h"
#include <string.h>

#define ARENA_DEF_RESERVE_SIZE (16*GB)
#define ARENA_DEF_COMMIT_SIZE  ( 4*MB) 

//Contiguous chunk of virtual memory. 
// This struct is combination of two separate concepts (for simplicity of implementation):
//  1: Normal arena interface - push/pop/reset etc.
//  2: Allocator capable of storing a **SINGLE** growing/shrinking allocation.
//       Allows the allocation to be reallocated up or down within the arena.
//       Can be used to make certain data structures stable in memory without any change.
//       An example of this includes Array, Hash, String_Builder, Path...
typedef struct Arena {
    Allocator alloc[1];

    uint8_t* data;
    uint8_t* used_to;
    uint8_t* commit_to;
    uint8_t* reserved_to;
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


EXTERNAL void* arena_allocator_func(void* self, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest);

#define ARENA_PUSH(arena_ptr, count, Type) ((Type*) arena_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_ARENA)) && !defined(MODULE_HAS_IMPL_ARENA)
#define MODULE_HAS_IMPL_ARENA

EXTERNAL Platform_Error arena_init(Arena* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero)
{
    arena_deinit(arena);
    isize alloc_granularity = platform_allocation_granularity();
    
    REQUIRE(reserve_size_or_zero >= 0);
    REQUIRE(commit_granularity_or_zero >= 0);
    REQUIRE(alloc_granularity >= 0);

    isize reserve_size = reserve_size_or_zero > 0 ? reserve_size_or_zero : ARENA_DEF_RESERVE_SIZE;
    isize commit_granularity = commit_granularity_or_zero > 0 ? commit_granularity_or_zero : ARENA_DEF_COMMIT_SIZE;

    reserve_size = DIV_CEIL(reserve_size, alloc_granularity)*alloc_granularity;
    commit_granularity = DIV_CEIL(commit_granularity, alloc_granularity)*alloc_granularity;

    uint8_t* data = NULL;
    Platform_Error error = platform_virtual_reallocate((void**) &data, NULL, reserve_size, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_NO_ACCESS);
    if(error == 0)
    {
        arena->alloc[0] = arena_allocator_func;

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
        isize size = (uint8_t*) to - arena->commit_to;
        isize commit = DIV_CEIL(size, arena->commit_granularity)*arena->commit_granularity;

        uint8_t* new_commit_to = arena->used_to + commit;
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
            char buffer[4096];
            platform_translate_error(platform_error, buffer, sizeof buffer);
            allocator_error(error_or_null, ALLOCATOR_ERROR_OUT_OF_MEM, arena->alloc, size, NULL, 0, 1, 
                "Virtual memory commit failed! Error: %s", buffer);
            goto end;
        }

        arena->commit_to = new_commit_to;
    }
    end:
    PROFILE_STOP();
}

EXTERNAL void arena_commit_ptr(Arena* arena, const void* to, Allocator_Error* error_or_null)
{
    //If + function call to not pollute the call site with code that will 
    // get executed extremely rarely (probably about twice)
    if((uint8_t*) to > arena->commit_to)
        _arena_commit_no_inline(arena, to, error_or_null);
}

EXTERNAL void* arena_push_nonzero(Arena* arena, isize size, isize align, Allocator_Error* error_or_null)
{
    uint8_t* out = (uint8_t*) align_forward(arena->used_to, align);
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
    arena->used_to = (uint8_t*) position;
}

EXTERNAL void arena_reset(Arena* arena, isize to)
{
    arena_reset_ptr(arena, arena->data + to);
}

EXTERNAL void arena_commit(Arena* arena, isize to)
{
    arena_commit_ptr(arena, arena->data + to, NULL);
}

EXTERNAL void* arena_allocator_func(void* self, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest)
{
    if(mode == ALLOCATOR_MODE_ALLOC) {
        Arena* arena = (Arena*) (void*) self;

        REQUIRE(old_ptr == arena->data);
        REQUIRE(old_size == arena->used_to - arena->data);
        REQUIRE(is_power_of_two(align));

        arena_reset_ptr(arena, arena->data);
        return arena_push_nonzero(arena, new_size, align, (Allocator_Error*) rest);
    }
    if(mode == ALLOCATOR_MODE_GET_STATS) {
        Arena* arena = (Arena*) (void*) self;
        Allocator_Stats stats = {0};
        stats.is_top_level = true;
        stats.type_name = "Arena";
        stats.name = arena->name;
        stats.fixed_memory_pool_size = arena->reserved_to - arena->data; 
        stats.bytes_allocated = arena->used_to - arena->data;
        *(Allocator_Stats*) rest = stats;
    }
    return NULL;
}

#endif