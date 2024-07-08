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
// and provide the stack order guarantees. The problem at hand is deciding to what furtherest point we are able to
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
// a function boundary, for example by passing a dynamic Array to function that will push to it (thus pontentially 
// triggering realloc). This can happen even in a case when both the caller and function called are 'well behaved'
// and handle arenas correctly.
// Also note that this situation does happen when switching between any finite amount of backing memory regions. 
// We switch whenever we acquire arena thus in the exmaple above arena1 would reside in memory 'A' while arena2 
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
#include "profile.h"

#define ARENA_DEF_STACK_SIZE   256
#define ARENA_DEF_RESERVE_SIZE 64 * GIBI_BYTE 
#define ARENA_DEF_COMMIT_SIZE  8 * MEBI_BYTE 

//Contiguous chunk of virtual memory.
typedef struct Arena {
    u8* data;
    isize size;
    isize commit;
    isize reserved;
    isize commit_granularity;
} Arena;

EXPORT bool arena_init(Arena* arena, isize reserve_size_or_zero, isize commit_granularity_or_zero);
EXPORT void arena_deinit(Arena* arena);
EXPORT void* arena_push_nonzero_unaligned(Arena* arena, isize size);
EXPORT void* arena_push_nonzero(Arena* arena, isize size, isize align);
EXPORT void* arena_push(Arena* arena, isize size, isize align);
EXPORT void arena_reset(Arena* arena, isize position);

typedef struct Arena_Stack Arena_Stack; 

typedef bool (*Arena_Stack_On_Bad_Push)(Arena_Stack* stack, i32 bad_depth, isize size, isize align);

//An "Allocator allocator". Wraps around Arena and is used to 
// acquire Arena_Frame from which individual allocations are made.
//This allows us to catch & prevent certain bugs associated with Arena reseting
typedef struct Arena_Stack {
    Arena arena;
    i32 stack_depth;
    i32 stack_max_depth;

    const char* name;
    isize max_size;
    isize acquasition_count;
    isize release_count;
    isize bad_pushes;

    //If is not null gets called when bad push is detected. Can be used to log or assert. 
    //If returns false aborts at call site.
    Arena_Stack_On_Bad_Push on_bad_push;
} Arena_Stack;

//Models a single lifetime of allocations done from an arena. 
// Also can be though of as representing a ScratchBegin()/ScratchEnd() pair.
typedef struct Arena_Frame {
    Allocator allocator;
    Arena_Stack* stack;
    i32 level;
    i32 _padding;
} Arena_Frame;

EXPORT bool arena_stack_init(Arena_Stack* arena, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize stack_max_depth_or_zero, Arena_Stack_On_Bad_Push on_bad_push_or_null, const char* name_or_null);
EXPORT void arena_stack_deinit(Arena_Stack* arena);

EXPORT void arena_frame_release(Arena_Frame* arena);
EXPORT Arena_Frame arena_frame_acquire(Arena_Stack* stack);
EXPORT void* arena_frame_push(Arena_Frame* arena, isize size, isize align);
EXPORT void* arena_frame_push_nonzero(Arena_Frame* arena, isize size, isize align);

#define ARENA_PUSH(arena_ptr, count, Type) ((Type*) arena_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))
#define ARENA_FRAME_PUSH(arena_ptr, count, Type) ((Type*) arena_frame_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))

EXPORT void* arena_frame_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
EXPORT Allocator_Stats arena_frame_get_allocatator_stats(Allocator* self);

EXPORT Arena_Stack* scratch_arena_stack();
EXPORT Arena_Frame scratch_arena_acquire();

//An allocator capable of storing only a **SINGLE** allocation at a time. 
// Allows the allocation to be reallocated up or down within the arena.
//Can be used to make certain data structures stable in memory without any change.
// An example of this includes Array, Hash_Index, String_Builder, Path... 
typedef struct Arena_Single_Allocator {
    Allocator allocator;
    Arena arena;
    
    const char* name;
    isize max_size;
} Arena_Single_Allocator;

EXPORT void* arena_single_allocator_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
EXPORT Allocator_Stats arena_single_allocator_get_allocatator_stats(Allocator* self);
EXPORT bool arena_single_allocator_init(Arena_Single_Allocator* arena, isize reserve_size_or_zero, isize commit_granularity_or_zero, const char* name);
EXPORT void arena_single_allocator_deinit(Arena_Single_Allocator* arena);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARENA_IMPL)) && !defined(JOT_ARENA_HAS_IMPL)
#define JOT_ARENA_HAS_IMPL

EXPORT bool arena_init(Arena* arena, isize reserve_size_or_zero, isize commit_granularity_or_zero)
{
    arena_deinit(arena);

    isize page_size = platform_page_size();
    
    ASSERT(reserve_size_or_zero >= 0);
    ASSERT(commit_granularity_or_zero >= 0);
    ASSERT(page_size >= 0);

    isize reserve_size = reserve_size_or_zero > 0 ? reserve_size_or_zero : ARENA_DEF_RESERVE_SIZE;
    isize commit_granularity = commit_granularity_or_zero > 0 ? commit_granularity_or_zero : ARENA_DEF_COMMIT_SIZE;

    reserve_size = DIV_CEIL(reserve_size, page_size)*page_size;
    commit_granularity = DIV_CEIL(commit_granularity, page_size)*page_size;

    u8* data = (u8*) platform_virtual_reallocate(NULL, reserve_size, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_NO_ACCESS);
    if(data == NULL)
        return false;

    arena->data = data;
    arena->size = 0;
    arena->commit = 0;
    arena->reserved = reserve_size;
    arena->commit_granularity = commit_granularity;
    return true;
}

EXPORT void arena_deinit(Arena* arena)
{
    if(arena->data)
        platform_virtual_reallocate(arena->data, arena->reserved, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);

    memset(arena, 0, sizeof *arena);
}

EXPORT void arena_commit(Arena* arena, isize size)
{
    if(size > arena->commit)
    {
        isize commit_new = DIV_CEIL(size, arena->commit_granularity)*arena->commit_granularity;
        if(commit_new > arena->reserved)
            abort(); //@TODO: something more proper but still keeping things fast
            
        PERF_COUNTER_START(commit);
        void* state = platform_virtual_reallocate(arena->data + arena->commit, commit_new - arena->commit, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
        PERF_COUNTER_END(commit);
        if(state == NULL)
            abort();

        arena->commit = commit_new;
    }
}
EXPORT void* arena_push_nonzero_unaligned(Arena* arena, isize size)
{
    arena_commit(arena, arena->size + size);
    void* out = arena->data + arena->size;
    arena->size += size;
    return out;
}

EXPORT void* arena_push_nonzero(Arena* arena, isize size, isize align)
{
    //We just kinda refuse to handle the case where we overflow. 
    //We should really make that into abort or something.
    u8* curr = arena->data + arena->size;
    u8* out = (u8*) align_forward(curr, align);
    isize needed_size = out - curr + size;
    arena_commit(arena, arena->size + needed_size);
    arena->size += needed_size;
    return out;
}

EXPORT void* arena_push(Arena* arena, isize size, isize align)
{
    void* out = arena_push_nonzero(arena, size, align);
    memset(out, 0, size);
    return out;
}

EXPORT void arena_reset(Arena* arena, isize position)
{
    arena->size = position;
}

//Arena stack things....

#ifndef ARENA_DEBUG
    #ifdef NDEBUG
        #define ARENA_DEBUG 0
    #else
        #define ARENA_DEBUG 1
    #endif
#endif

#define ARENA_DEBUG_DATA_SIZE     64*2
#define ARENA_DEBUG_DATA_PATTERN  0x55
#define ARENA_DEBUG_STACK_PATTERN 0x66

INTERNAL void _arena_debug_check_invariants(Arena_Stack* arena);
INTERNAL void _arena_debug_fill_stack(Arena_Stack* arena);
INTERNAL void _arena_debug_fill_data(Arena_Stack* arena, isize size);

EXPORT void arena_stack_deinit(Arena_Stack* arena)
{
    _arena_debug_check_invariants(arena);
    arena_deinit(&arena->arena);
    memset(arena, 0, sizeof *arena);
}

EXPORT bool arena_stack_init(Arena_Stack* arena, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize stack_max_depth_or_zero, Arena_Stack_On_Bad_Push on_bad_push_or_null, const char* name_or_null)
{
    arena_stack_deinit(arena);
    if(arena_init(&arena->arena, reserve_size_or_zero, commit_granularity_or_zero) == false)
        return false;

    isize stack_max_depth = stack_max_depth_or_zero;
    isize stack_max_depth_that_fits = arena->arena.reserved / isizeof(isize);

    if(stack_max_depth <= 0)
        stack_max_depth = ARENA_DEF_STACK_SIZE;
    if(stack_max_depth > stack_max_depth_that_fits)
        stack_max_depth = stack_max_depth_that_fits;

    arena_push_nonzero(&arena->arena, stack_max_depth*isizeof(isize), isizeof(isize)); 
    arena->on_bad_push = on_bad_push_or_null;
    arena->name = name_or_null;
    arena->stack_max_depth = (i32) stack_max_depth;
    arena->stack_depth = 0;
    
    _arena_debug_fill_stack(arena);
    _arena_debug_fill_data(arena, INT64_MAX);
    _arena_debug_check_invariants(arena);
    return true;
}

ATTRIBUTE_INLINE_NEVER void _arena_handle_unusual_push(Arena_Stack* stack, i32 depth, isize size, isize align)
{
    PERF_COUNTER_START();
    _arena_debug_check_invariants(stack);
    if(stack->stack_depth > depth)
    {
        stack->bad_pushes += 1;
        if(stack->on_bad_push && stack->on_bad_push(stack, depth, size, align) == false)
            abort();
    }
    ASSERT(stack->stack_depth < stack->stack_max_depth);

    i32 to_depth = MIN(depth, stack->stack_max_depth);
    isize* levels = (isize*) (void*) stack->arena.data;
    for(i32 i = stack->stack_depth; i < to_depth; i++)
        levels[i] = stack->arena.size;

    stack->stack_depth = to_depth;
    arena_commit(&stack->arena, stack->arena.size + size + align);
    _arena_debug_fill_data(stack, INT64_MAX);
    _arena_debug_fill_stack(stack);
    _arena_debug_check_invariants(stack);

    PERF_COUNTER_END();
}

INTERNAL ATTRIBUTE_INLINE_ALWAYS void* _arena_frame_push_nonzero_inline(Arena_Frame* arena, isize size, isize align)
{
    PERF_COUNTER_START();
    ASSERT(arena->stack && arena->level > 0);
    Arena_Stack* stack = arena->stack;
    _arena_debug_check_invariants(stack);

    if(stack->stack_depth != arena->level || stack->arena.size + size > stack->arena.commit)
        _arena_handle_unusual_push(stack, arena->level, size, align);

    u8* out = (u8*) align_forward(stack->arena.data + stack->arena.size, align);
    stack->arena.size = out - stack->arena.data + size;

    _arena_debug_check_invariants(stack);
    PERF_COUNTER_END();
    return out;
}

EXPORT void* arena_frame_push_nonzero(Arena_Frame* arena, isize size, isize align)
{
    return _arena_frame_push_nonzero_inline(arena, size, align);
}

EXPORT void* arena_frame_push(Arena_Frame* arena, isize size, isize align)
{
    void* ptr = arena_frame_push_nonzero(arena, size, align);
    memset(ptr, 0, (size_t) size);
    return ptr;
}

EXPORT void arena_frame_release(Arena_Frame* arena)
{
    PERF_COUNTER_START();

    Arena_Stack* stack = arena->stack;
    ASSERT(arena->stack && arena->level > 0);
    _arena_debug_check_invariants(stack);
    if(arena->level > 0 && stack->stack_depth >= arena->level)
    {
        isize* levels = (isize*) (void*) stack->arena.data;
        isize new_used_to = levels[arena->level - 1];
        isize old_used_to = stack->arena.size;

        isize stack_bytes = stack->stack_max_depth * isizeof *levels;
        ASSERT(stack_bytes <= new_used_to && new_used_to <= stack->arena.size);

        if(stack->max_size < stack->arena.size)
            stack->max_size = stack->arena.size;

        stack->arena.size = new_used_to;
        stack->stack_depth = arena->level - 1;
        stack->release_count += 1;

        //@TODO: we have memory corruption in file logger somewhere as this check
        //keeps firing but only for it. FIX THIS!
        _arena_debug_fill_data(stack, old_used_to - new_used_to);
        //_arena_debug_fill_data(stack, old_used_to - new_used_to + ARENA_DEBUG_DATA_SIZE);
        _arena_debug_check_invariants(stack);
        memset(arena, 0, sizeof* arena);
    }

    PERF_COUNTER_END();
}

//Compatibility function for the allocator interface
EXPORT void* arena_frame_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
{
    void* out = NULL;
    if(new_size > old_size)
    {
        Arena_Frame* arena = (Arena_Frame*) (void*) self;
        out = _arena_frame_push_nonzero_inline(arena, new_size, align);
        memcpy(out, old_ptr, (size_t) old_size);
    }

    return out;
}

EXPORT Allocator_Stats arena_frame_get_allocatator_stats(Allocator* self)
{
    Arena_Frame* arena = (Arena_Frame*) (void*) self;
    Arena_Stack* stack = arena->stack;

    isize max_size = MAX(stack->arena.size, stack->max_size);
    isize stack_to = stack->stack_max_depth * isizeof(isize);

    Allocator_Stats stats = {0};
    stats.type_name = "Arena_Frame";
    stats.name = stack->name;
    stats.is_top_level = true;
    stats.bytes_allocated = stack->arena.size - stack_to;
    stats.max_bytes_allocated = max_size - stack_to;
    stats.allocation_count = stack->acquasition_count;
    stats.deallocation_count = stack->release_count;

    return stats;
}

EXPORT Arena_Frame arena_frame_acquire(Arena_Stack* stack)
{
    PERF_COUNTER_START();

    _arena_debug_check_invariants(stack);
    i32 new_depth = stack->stack_depth + 1;
    isize* levels = (isize*) (void*) stack->arena.data;
    if(new_depth <= stack->stack_max_depth)
    {
        levels[new_depth - 1] = stack->arena.size;
        stack->stack_depth = new_depth;
    }

    stack->acquasition_count += 1;

    Arena_Frame out = {0};
    out.allocator.allocate = arena_frame_reallocate;
    out.allocator.get_stats = arena_frame_get_allocatator_stats;
    out.level = new_depth;
    out.stack = stack;

    PERF_COUNTER_END();
    return out;
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

ATTRIBUTE_THREAD_LOCAL Arena_Stack _scratch_arena_stack;
EXPORT Arena_Stack* scratch_arena_stack()
{
    return &_scratch_arena_stack;
}
EXPORT Arena_Frame scratch_arena_acquire()
{
    if(_scratch_arena_stack.arena.data == NULL)
        arena_stack_init(&_scratch_arena_stack, 0, 0, 0, NULL, "scratch arena stack");

    return arena_frame_acquire(&_scratch_arena_stack);
}

INTERNAL void _arena_debug_check_invariants(Arena_Stack* stack)
{
    if(ARENA_DEBUG)
    {
        isize* levels = (isize*) (void*) stack->arena.data;
        ASSERT(stack->stack_max_depth * (isize)sizeof *levels <= stack->arena.size && stack->arena.size <= stack->arena.commit, "used_to needs to be within [0, size]");
        ASSERT(0 <= stack->stack_depth && stack->stack_depth <= stack->stack_max_depth, "used_to stack_depth to be within [0, stack_max_depth]");

        if(stack->arena.data == NULL)
            ASSERT(stack->arena.commit == 0, "only permitted to be NULL when zero sized");

        isize till_end = stack->arena.commit - stack->arena.size;
        isize check_size = MIN(till_end, ARENA_DEBUG_DATA_SIZE);
        ASSERT(memcmp_byte(stack->arena.data + stack->arena.size, ARENA_DEBUG_DATA_PATTERN, check_size) == 0, "The memory after the arena needs not be corrupted!");
        ASSERT(memcmp_byte(levels + stack->stack_depth, ARENA_DEBUG_STACK_PATTERN, (stack->stack_max_depth - stack->stack_depth) * isizeof *levels) == 0, "The memory after stack needs to be valid");
    }
}

INTERNAL void _arena_debug_fill_stack(Arena_Stack* stack)
{
    isize* levels = (isize*) (void*) stack->arena.data;
    if(ARENA_DEBUG)
        memset(levels + stack->stack_depth, ARENA_DEBUG_STACK_PATTERN, (size_t) (stack->stack_max_depth - stack->stack_depth) * sizeof *levels);
}

INTERNAL void _arena_debug_fill_data(Arena_Stack* stack, isize size)
{
    if(ARENA_DEBUG)
    {
        isize till_end = stack->arena.commit - stack->arena.size;
        isize check_size = MIN(till_end, size);
        memset(stack->arena.data + stack->arena.size, ARENA_DEBUG_DATA_PATTERN, (size_t) check_size);
    }
}

EXPORT void* arena_single_allocator_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
{
    Arena_Single_Allocator* alloc = (Arena_Single_Allocator*) (void*) self;
    ASSERT(old_ptr == alloc->arena.data);
    ASSERT(old_size == alloc->arena.size);
    ASSERT(alloc->arena.size <= alloc->max_size);
    ASSERT(is_power_of_two(align));

    arena_reset(&alloc->arena, 0);
    void* out = arena_push_nonzero_unaligned(&alloc->arena, new_size);
    alloc->max_size = MAX(alloc->max_size, new_size);

    return out;
}

EXPORT Allocator_Stats arena_single_allocator_get_allocatator_stats(Allocator* self)
{
    Arena_Single_Allocator* alloc = (Arena_Single_Allocator*) (void*) self;
    Allocator_Stats out = {0};
    out.is_top_level = true;
    out.type_name = "Arena_Single_Allocator";
    out.name = alloc->name;
    out.max_bytes_allocated = alloc->max_size;

    return out;
}
EXPORT bool arena_single_allocator_init(Arena_Single_Allocator* arena, isize reserve_size_or_zero, isize commit_granularity_or_zero, const char* name)
{
    if(arena_init(&arena->arena, reserve_size_or_zero, commit_granularity_or_zero) == false)
        return false;

    arena->allocator.allocate = arena_single_allocator_reallocate;
    arena->allocator.get_stats = arena_single_allocator_get_allocatator_stats;
    arena->max_size = 0;
    arena->name = name;
    return true;
}
EXPORT void arena_single_allocator_deinit(Arena_Single_Allocator* arena)
{
    arena_deinit(&arena->arena);
    memset(arena, 0, sizeof *arena);
}
#endif