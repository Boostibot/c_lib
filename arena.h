#ifndef JOT_ARENA
#define JOT_ARENA

// This is a 'safe' implementation of the arena concept. It maintains the stack like order of allocations 
// on its own without the possibility of accidental invalidation of allocations from 'lower' levels. 
// (Read more for proper explanations).
// 
// Arena_Frame is a allocator used to conglomerate individual allocations to a continuous buffer, which allows for
// extremely quick free-all/reset operation (just move the index back). It cannot be implemented on top of
// the allocator interface (see allocator.h or below) because it does not know its maximum size up front.
// (ie. we couldnt reserve and had to hard allocate 64GB of space). 
// 
// Note that Arenas except for their perf and cache locality dont provide *any* benefits over a tracking 
// allocator with linked list of allocations. As such they should mostly be use for scratch allocator 
// functionality where the quick free-all is a major advantage.
// 
// ===================================== REASONING =================================================
// 
// This implementation views two concepts: 
// 1) Arena_Stack - which holds the actual reserved memory and allocates individual Arenas
// 2) Arena_Frame - which allocates the user memory
// 
// The user memory acquired from Arena_Frame does NOT need to be freed because it gets recycled when the Arena_Frame
// does get freed, as such both Arena_Frame and Arena_Stack do NEED to be freed.
//
// We make this Arena_Stack/Arena_Frame distinction because it allows us to reason about the conglomerated lifetimes
// and provide the stack order guarantees. The problem at hand is deciding to what furthest point we are able to
// to rewind inside Arena_Stack on each release of Arena_Frame. If we did the usual rewinding to hard set index we would 
// invalidate the stack order. Consider the following scenario (using the names of the functions defined below):
// 
// 
// //A fresh new Arena_Stack with used_to = 0 
// Arena_Stack stack = {0};
// arena_stack_init(&stack, ...);
// 
// //saves restore point as arena1.restore_to = stack.used_to = 0 
// Arena_Frame arena1 = arena_frame_acquire(&stack);
// void* alloc1 = arena_frame_push(&arena1, 256, 8); //allocate 256B aligned to 8B boundary
// 
// //saves restore point as arena2.restore_to = stack.used_to = 256
// Arena_Frame arena2 = arena_frame_acquire(&stack);
// void* alloc2 = arena_frame_push(&arena2, 256, 8); //another allocation
// 
// arena_frame_release(&arena1); //Release arena1 thus setting stack.used_to = arena1.restore_to = 0 
// 
// //Now alloc2 is past the used to index, meaning it can be overriden by subsequent allocation!
// Arena_Frame arena3 = arena_frame_acquire(&stack);
// void* alloc3 = arena_frame_push(&arena3, 512, 8);
// 
// //alloc3 shares the same memory as alloc2! 
// 
// arena_frame_release(alloc3);
// 
// 
// Note that this situation does occur indeed occur in practice, typically while implicitly passing arena across 
// a function boundary, for example by passing a dynamic Array to function that will push to it (thus potentially 
// triggering realloc). This can happen even in a case when both the caller and function called are 'well behaved'
// and handle arenas correctly.
// Also note that this situation does happen when switching between any finite amount of backing memory regions. 
// We switch whenever we acquire arena thus in the example above arena1 would reside in memory 'A' while arena2 
// in memory 'B'. This would prevent that specific case above from breaking but not even two arenas (Ryan Flurry 
// style) will save us if we are not careful. I will be presuming two memory regions A and B in the examples 
// below but the examples trivially extend to N arenas.
// 
// To illustrate the point we will need to start talking about *levels*.
// Level is a positive number starting at 1 that gets incremented every time we acquire Arena_Frame from Arena_Stack
// and decremented whenever we release the acquired Arena_Frame. This corresponds to a depth in a stack.
//
// The diagrams show level on the Y axis along with the memory region A, B where the level resides in. The X axis
// shows the order of allocations. ### is symbol marking the alive region of an allocation. It is preceeded by a 
// number corresponding to the level it was allocated from.
//
// First we illustrate the problem above with two memory regions A and B in diagram form.
// 
// level
//   ^
// A |         3### [1]### //here we allocate at level one from A
// B |     2###
// A | 1###
//   +--------------------------> time
// 
// After lifetime of 3 ends
// 
//   ^
// B |     2### //Missing the last allocation thus we reached error state!
// A | 1###
//   +--------------------------> time
// 
// One potential fix is to enforce the stack like nesting by flattening out the arena_frame_acquire()/arena_frame_release() 
// on problematic allocations 'from below' ([1]). We dont actually have to do anything besides ignoring calls 
// to arena_frame_release of levels 2 and 3 (somehow). In diagram form:
// 
//   ^
// A |         3### 
// B |     2###     
// A | 1###         1### //from below allocation
//   +--------------------------> time
// 
//                | Flatten
//                V 
//   ^
//   |         
//   |     
// A | 1###2###3###1###  //We completely ignore the 2, 3 Allocations and are treating them as a part of 1
//   +--------------------------> time
//
// Now of course we are having a level 2 and level 3 worth of wasted memory inside the level 1 allocation. 
// This is suboptimal but clearly better then having a hard to track down error.
//
// ===================================== IMPLEMENTATION =================================================
// 
// We achieve this flattening by storing the list of restore points within the arena. For simplicity we store
// some max number of restore points which are just integers to the used_to index. When a problematic allocation
// 'from bellow' arises we set these restore indices to values so that they will not result in error. In practice
// this results in only one extra if with branch thats almost never taken. We further stop the compiler from 
// inlining the unusual case code thus having extremely low impact on the resulting speed of the arena for the
// simple arena_frame_push() case. The acquire and release functions are a tiny bit more expensive but those are not 
// of primary concern.

#include "allocator.h"
#include "profile_defs.h"

#define ARENA_DEF_STACK_SIZE   256
#define ARENA_DEF_RESERVE_SIZE 64 * GB 
#define ARENA_DEF_COMMIT_SIZE  8 * MB 

typedef struct Arena Arena;

//Contiguous chunk of virtual memory. 
// This struct is combination of 3 separate concepts (for simplicity of implementation):
//  1: Normal arena interface - push/pop/reset etc.
//  2: Allocator capable of storing a **SINGLE** growing/shrinking allocation.
//       Allows the allocation to be reallocated up or down within the arena.
//       Can be used to make certain data structures stable in memory without any change.
//       An example of this includes Array, Hash_Index, String_Builder, Path...
//  3: Arena Stack - An "Allocator allocator". Wraps around Arena and is used to 
//      acquire Arena_Frame from which individual allocations are made.
//      This allows us to catch & prevent certain bugs associated with Arena reseting
//      This is detailed in the long comment at the top of this file. 
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

typedef struct Arena_Stack {
    Arena arena;
    i32 frame_level;
    i32 write_level;
    i32 max_level;
    i32 padding;
} Arena_Stack;

//Models a single lifetime of allocations done from an arena. 
// Also can be though of as representing a ScratchBegin()/ScratchEnd() pair.
typedef struct Arena_Frame {
    Allocator alloc[1];
    Arena_Stack* stack;
    i32 level;
    i32 pad;
} Arena_Frame;

EXTERNAL Arena_Frame arena_frame_acquire(Arena_Stack* arena);
EXTERNAL Platform_Error arena_stack_init(Arena_Stack* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize stack_max_depth_or_zero);
EXTERNAL void arena_stack_test_invariants(Arena_Stack* stack);
EXTERNAL void arena_stack_deinit(Arena_Stack* arena);
EXTERNAL void arena_frame_release(Arena_Frame* arena);
EXTERNAL void* arena_frame_push(Arena_Frame* arena, isize size, isize align);
EXTERNAL void* arena_frame_push_nonzero(Arena_Frame* arena, isize size, isize align);

#define ARENA_PUSH(arena_ptr, count, Type) ((Type*) arena_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))
#define ARENA_FRAME_PUSH(arena_ptr, count, Type) ((Type*) arena_frame_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))

EXTERNAL void* arena_frame_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
EXTERNAL Allocator_Stats arena_frame_allocator_get_stats(Allocator* self);

EXTERNAL Arena_Stack* scratch_arena();
EXTERNAL Arena_Frame scratch_arena_acquire();
#define SCRATCH_ARENA(frame_name) \
    /* Because the compiler cannot really know the value returned by scratch_arena_acquire(), it does a poor job */ \
    /* of eliminating the defer loop. We need to ensure that it has all the variables used in the logic of the defer */ \
    /* loop right here. Since we cannot declare another data type for this purpose we declare another Arena_Frame called */ \
    /* _frame_counter_[LINE] whose one field is used for this. */ \
    for(Arena_Frame frame_name = scratch_arena_acquire(), PP_UNIQ(_frame_counter_) = {0}; PP_UNIQ(_frame_counter_).pad == 0; arena_frame_release(&frame_name), PP_UNIQ(_frame_counter_).pad = 1)

#endif

#define JOT_ALL_IMPL
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

#ifndef ARENA_DEBUG
    #ifdef NDEBUG
        #define ARENA_DEBUG 0
    #else
        #define ARENA_DEBUG 1
    #endif
#endif

#define ARENA_DEBUG_DATA_SIZE     128
#define ARENA_DEBUG_DATA_PATTERN  0x55
#define ARENA_DEBUG_STACK_PATTERN 0x6666666666666666

INTERNAL void _arena_debug_check_invariants(Arena_Stack* arena);
INTERNAL void _arena_debug_fill_stack(Arena_Stack* arena);
INTERNAL void _arena_debug_fill_data(Arena_Stack* arena, isize size);

EXTERNAL void arena_stack_deinit(Arena_Stack* arena)
{
    _arena_debug_check_invariants(arena);
    arena_deinit(&arena->arena);
    memset(arena, 0, sizeof *arena);
}

EXTERNAL Platform_Error arena_stack_init(Arena_Stack* stack, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize stack_max_depth_or_zero)
{
    arena_stack_deinit(stack);
    Platform_Error error = arena_init(&stack->arena, name, reserve_size_or_zero, commit_granularity_or_zero);
    if(error == 0)
    {
        isize stack_max_depth = stack_max_depth_or_zero;
        isize stack_max_depth_that_fits = (stack->arena.reserved_to - stack->arena.data) / isizeof(isize);

        if(stack_max_depth <= 0)
            stack_max_depth = ARENA_DEF_STACK_SIZE;
        if(stack_max_depth > stack_max_depth_that_fits)
            stack_max_depth = stack_max_depth_that_fits;

        arena_push_nonzero(&stack->arena, stack_max_depth*isizeof(isize), isizeof(isize), NULL); 
        stack->max_level = (i32) stack_max_depth;

        _arena_debug_fill_stack(stack);
        _arena_debug_fill_data(stack, INT64_MAX);
        _arena_debug_check_invariants(stack);
    }
    return error;
}

EXTERNAL ATTRIBUTE_INLINE_NEVER void* _arena_handle_unusual_push(Arena_Stack* stack, i32 frame_level, isize size, isize align, Allocator_Error* error)
{
    PROFILE_START();
    _arena_debug_check_invariants(stack);
        
    u8* out = (u8*) align_forward(stack->arena.used_to, align);
    u8* commit_to = out + size;
    if((u8*) commit_to > stack->arena.commit_to)
    {
        Arena* arena = &stack->arena;
        isize commit = DIV_CEIL(size, arena->commit_granularity)*arena->commit_granularity;

        u8* new_commit_to = arena->used_to + commit;
        if(new_commit_to > arena->reserved_to)
        {
            out = NULL;
            allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, stack->arena.alloc, size, NULL, 0, align, 
                "More memory is needed then reserved! Reserved: %.2lf MB, commit: %.2lf MB", 
                (double) (arena->reserved_to - arena->data)/MB, (double) (arena->commit_to - arena->data)/MB);
            goto end;
        }
            
        Platform_Error platform_error = platform_virtual_reallocate(NULL, arena->commit_to, new_commit_to - arena->commit_to, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
        if(platform_error)
        {
            out = NULL;
            allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, stack->arena.alloc, size, NULL, 0, align, 
                "Virtual memory commit failed! Error: %s", platform_translate_error(platform_error));
            goto end;
        }

        arena->commit_to = new_commit_to;
    }

    {
        void** levels = (void**) stack->arena.data;
        for(i32 i = stack->write_level; i < frame_level; i++)
            levels[i] = stack->arena.used_to;

        stack->write_level = frame_level;
    }
    
    stack->arena.used_to = out + size;
    _arena_debug_fill_data(stack, INT64_MAX);
    _arena_debug_fill_stack(stack);
    _arena_debug_check_invariants(stack);

    end:
    PROFILE_END();
    return out;
}

EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push_nonzero_error(Arena_Frame* frame, isize size, isize align, Allocator_Error* error)
{
    PROFILE_START();
    ASSERT(frame->stack && 0 < frame->level && frame->level <= frame->stack->frame_level, 
        "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");
    Arena_Stack* stack = frame->stack;
    _arena_debug_check_invariants(stack);

    u8* out = (u8*) align_forward(stack->arena.used_to, align);
    if(stack->write_level != frame->level || out + size > stack->arena.commit_to)
        return _arena_handle_unusual_push(stack, frame->level, size, align, error);
        
    stack->arena.used_to = out + size;
    _arena_debug_check_invariants(stack);
    PROFILE_END();
    return out;
}

EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push_nonzero(Arena_Frame* frame, isize size, isize align)
{
    return arena_frame_push_nonzero_error(frame, size, align, NULL);
}

EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push(Arena_Frame* frame, isize size, isize align)
{
    void* ptr = arena_frame_push_nonzero(frame, size, align);
    memset(ptr, 0, (size_t) size);
    return ptr;
}

EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Frame arena_frame_acquire(Arena_Stack* stack)
{
    PROFILE_START();
    ASSERT(stack->frame_level < stack->max_level, "Too many arena levels! Max %i", (int) stack->max_level);
    _arena_debug_check_invariants(stack);

    //Here we could do a full for loop setting all levels affected by the 'rise' 
    // similar to the one in "_arena_handle_unusual_push"
    // However, the situation that requires the full for loop is very unlikely. 
    // Thus we only do one level (which is the usual case) and let the rest be 
    // handled in "_arena_handle_unusual_push".
    void** levels = (void**) stack->arena.data;
    stack->frame_level += 1;
    levels[stack->write_level++] = stack->arena.used_to;

    Arena_Frame out = {0};
    //Assign the arena frame. Fill the allocator part. 
    // For simple cases (acquire frame, push, release frame) this gets optimized away.
    out.alloc[0].func = arena_frame_allocator_func;
    out.alloc[0].get_stats = arena_frame_allocator_get_stats;
    out.level = stack->frame_level;
    out.stack = stack;

    _arena_debug_check_invariants(stack);
    PROFILE_END();
    return out;
}

EXTERNAL ATTRIBUTE_INLINE_ALWAYS void arena_frame_release(Arena_Frame* frame)
{
    PROFILE_START();
    ASSERT(frame->stack && 0 < frame->level && frame->level <= frame->stack->frame_level, 
        "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");

    Arena_Stack* stack = frame->stack;
    _arena_debug_check_invariants(stack);

    if(stack->write_level >= frame->level)
    {
        void** levels = (void**) stack->arena.data;
        u8* new_used_to = (u8*) levels[frame->level - 1];
        u8* old_used_to = stack->arena.used_to;

        isize stack_bytes = stack->max_level * isizeof *levels;
        ASSERT(stack->arena.data + stack_bytes <= new_used_to && new_used_to <= stack->arena.used_to);

        stack->arena.used_to = new_used_to;
        stack->write_level = frame->level - 1;
        _arena_debug_fill_data(stack, old_used_to - new_used_to + ARENA_DEBUG_DATA_SIZE);
    }
    stack->frame_level = frame->level - 1;
    _arena_debug_fill_stack(stack);
    _arena_debug_check_invariants(stack);
    PROFILE_END();
    
    //Set the frame to zero so that if someone tries to allocate from this frame
    // it will trigger an assert
    if(ARENA_DEBUG)
        memset(frame, 0, sizeof* frame);
}

EXTERNAL void* arena_frame_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
{
    Arena_Frame* frame = (Arena_Frame*) (void*) self;
    void* out = arena_frame_push_nonzero_error(frame, new_size, align, error);
    if(out)
        memcpy(out, old_ptr, (size_t) MIN(old_size, new_size));
    
    return out;
}

EXTERNAL Allocator_Stats arena_frame_allocator_get_stats(Allocator* self)
{
    Arena_Frame* frame = (Arena_Frame*) (void*) self;
    Allocator_Stats stats = {0};

    Arena* arena = &frame->stack->arena;
    void** levels = (void**) arena->data;
    u8* start = (u8*) levels[frame->level - 1];
    stats.type_name = "Arena_Frame";
    stats.name = arena->name;
    stats.is_capable_of_free_all = true;
    stats.fixed_memory_pool_size = arena->reserved_to - start;
    stats.parent = &arena->alloc[1];
    stats.bytes_allocated = arena->used_to - start;
    stats.max_bytes_allocated = arena->reserved_to - start;
    return stats;
}

INTERNAL int memcmp_byte(const void* ptr, int byte, isize size)
{
    isize i = 0;
    char* text = (char*) ptr;

    if((isize) ptr % 8 == 0)
    {
        //pattern is 8 repeats of byte
        u64 pattern = (u64) 0x0101010101010101ULL * (u64) byte;
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

EXTERNAL void arena_stack_test_invariants(Arena_Stack* stack)
{
    void** levels = (void**) stack->arena.data;
    u8* levels_end = (u8*) (levels + stack->max_level);

    TEST(stack->arena.data <= stack->arena.used_to);
    TEST(stack->arena.used_to <= stack->arena.commit_to);
    TEST(stack->arena.commit_to <= stack->arena.reserved_to);
    TEST(0 <= stack->max_level);
    TEST(0 <= stack->write_level && stack->write_level <= stack->frame_level && stack->frame_level <= stack->max_level, "levels need to be ordered 0 <= write_level <= frame_level <= max_level");

    TEST(levels_end <= stack->arena.used_to && stack->arena.used_to <= stack->arena.commit_to, "used_to needs to be within [levels_end, commit_to]");
        
    for(i32 i = 0; i < stack->write_level; i++)
        TEST(levels_end <= (u8*) levels[i] && (u8*) levels[i] <= stack->arena.commit_to, "level pointers need to be within [levels_end, commit_to]");
            
    if(stack->arena.data == NULL)
        TEST(stack->arena.commit_to == 0 && stack->arena.reserved_to == 0, "only permitted to be NULL when zero sized");
            
    if(ARENA_DEBUG)
    {
        for(i32 i = stack->write_level; i < stack->max_level; i++)
            TEST(levels[i] == (void*) ARENA_DEBUG_STACK_PATTERN);

        isize till_end = stack->arena.commit_to - stack->arena.used_to;
        isize check_size = MIN(till_end, ARENA_DEBUG_DATA_SIZE);
        TEST(memcmp_byte(stack->arena.used_to, ARENA_DEBUG_DATA_PATTERN, check_size) == 0, "The memory after the arena needs not be corrupted!");
    }
}

INTERNAL void _arena_debug_check_invariants(Arena_Stack* stack)
{
    if(ARENA_DEBUG)
        arena_stack_test_invariants(stack);
}

INTERNAL void _arena_debug_fill_stack(Arena_Stack* stack)
{
    if(ARENA_DEBUG)
    {
        void** levels = (void**) stack->arena.data;
        for(i32 i = stack->write_level; i < stack->max_level; i++)
            levels[i] = (void*) ARENA_DEBUG_STACK_PATTERN;
    }
}

INTERNAL void _arena_debug_fill_data(Arena_Stack* stack, isize size)
{
    if(ARENA_DEBUG)
    {
        isize till_end = stack->arena.commit_to - stack->arena.used_to;
        isize check_size = MIN(till_end, size);
        memset(stack->arena.used_to, ARENA_DEBUG_DATA_PATTERN, (size_t) check_size);
        int k = 1; k = 5;
    }
}

ATTRIBUTE_THREAD_LOCAL Arena_Stack _scratch_arena;
EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Stack* scratch_arena()
{
    return &_scratch_arena;
}
EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Frame scratch_arena_acquire()
{
    ASSERT(_scratch_arena.arena.data != NULL, "Must be already init!");
    return arena_frame_acquire(&_scratch_arena);
}
#endif