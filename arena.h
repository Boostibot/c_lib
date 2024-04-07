#ifndef JOT_ARENA
#define JOT_ARENA

// This is a 'safe' implementation of the arena concept. It maintains the stack like order of allocations 
// on its own without the possibility of accidental invalidation of allocations from 'lower' levels. 
// (Read more for proper explanations).
// 
// Arena is a allocator used to coglomerate individual allocations to a continuous buffer, which allows for
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
// 2) Arena - which allocates the user memory
// 
// The user memory acquired from Arena does NOT need to be freed because it gets recycled when the Arena
// does get freed, as such both Arena and Arena_Stack do NEED to be freed.
//
// We make this Arena_Stack/Arena distinction because it allows us to reason about the coglomerated lifetimes
// and provide the stack order guarantees. The problem at hand is deciding to what furtherst point we are able to
// to rewind inside Arena_Stack on each release of Arena. If we did the usual rewing to hard set index we would 
// invalidate the stack order. Consider the following scenario (using the names of the functions defined below):
// 
// 
// //A fresh new Arena_Stack with used_to = 0 
// Arena_Stack stack = {0};
// arena_stack_init(&stack, ...);
// 
// //saves restore point as arena1.restore_to = stack.used_to = 0 
// Arena arena1 = arena_acquire(&stack);
// void* alloc1 = arena_push(&arena1, 256, 8); //allocate 256B aligend to 8B boundary
// 
// //saves restore point as arena2.restore_to = stack.used_to = 256
// Arena arena2 = arena_acquire(&stack);
// void* alloc2 = arena_push(&arena2, 256, 8); //another allocation
// 
// arena_release(&arena1); //Release arena1 thus setting stack.used_to = arena1.restore_to = 0 
// 
// //Now alloc2 is past the used to index, meaning it can be overriden by subsequent allocation!
// Arena arena3 = arena_acquire(&stack);
// void* alloc3 = arena_push(&arena3, 512, 8);
// 
// //alloc3 shares the same emmory as alloc2! 
// 
// arena_release(alloc3);
// 
// 
// Note that this situation does occur indeed occur in practice, typically while implicitly passing arena across 
// a function boundary, for example by passing a dynamic Array to function that will push to it (thus pontetially 
// triggering realloc). This can happen even in a case when both the caller and function called are 'well behaved'
// and handle arenas corrrectly.
// Also note that this situation does happen when switching between any finite ammount of backing memory regions. 
// We switch whenever we acquire arena thus in the exmaple above arena1 would reside in memory 'A' while arena2 
// in memory 'B'. This would prevent that specific case above from breaking but not even two arenas (Ryan Flurry 
// style) will save us if we are not careful. I will be presuming two memory regions A and B in the examples 
// below but the examples trivially extend to N arenas.
// 
// To illustrate the point we will need to start talking about *levels*.
// Level is a positive number starting at 1 that gets incremented every time we acquire Arena from Arena_Stack
// and decremeted whenever we release the acquired Arena. This coresponds to a depth in a stack.
//
// The diagrams show level on the Y axis along with the memory region A, B where the level resides in. The X axis
// shows the order of allocations. ### is symbol marking the alive region of an allocation. It is preceeded by a 
// number coresponding to the level it was allocated from.
//
// First we illustarte the problem above with two memory regions A and B in diagram form.
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
// One potential fix is to enforce the stack like nesting by flattening out the arena_acquire()/arena_release() 
// on problematic allocations 'from below' ([1]). We dont actually have to do anything besides ignoring calls 
// to arena_release of levels 2 and 3 (somehow). In diagram form:
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
// We achive this flattening by storing the list of restore points within the arena. For simplicity we store
// some max number of restore points which are just integers to the used_to index. When a problematic allocation
// 'from bellow' arises we set these restore indeces to values so that they will not result in error. In practice
// this results in only one extra if with branch thats almost never taken. We further stop the compielr from 
// inlining the unusual case code thus having extremely low impact on the resulting speed of the arena for the
// simple arena_push() case. The acquire and release functions are a tiny bit more expensive but those are not 
// of primary concern.

#include "allocator.h"
#include "profile.h"

typedef isize (*Arena_Stack_Commit_Func)(void* addr, isize size, isize reserved_size, bool commit);

#define ARENA_DEF_STACK_SIZE   256
#define ARENA_DEF_RESERVE_SIZE 64 * GIBI_BYTE 
#define ARENA_DEF_COMMIT_SIZE  8 * MEBI_BYTE 

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
    i32 _padding;
} Arena;

EXPORT void arena_init(Arena_Stack* arena, isize reserve_size_or_zero, isize stack_max_depth_or_zero, const char* name_or_null);
EXPORT void arena_init_custom(Arena_Stack* arena, void* data, isize size, isize reserved_size, Arena_Stack_Commit_Func commit_or_null, isize stack_max_depth_or_zero);
EXPORT void arena_deinit(Arena_Stack* arena);
EXPORT void arena_release(Arena* arena);

EXPORT Arena arena_acquire(Arena_Stack* stack);
EXPORT void* arena_push(Arena* arena, isize size, isize align);
EXPORT void* arena_push_nonzero(Arena* arena, isize size, isize align);
static ATTRIBUTE_INLINE_ALWAYS void* arena_push_nonzero_inline(Arena* arena, isize size, isize align);

#define ARENA_PUSH(arena_ptr, count, Type) ((Type*) arena_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))

EXPORT void* arena_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
EXPORT Allocator_Stats arena_get_allocatator_stats(Allocator* self);

EXPORT Arena_Stack* scratch_arena_stack();
EXPORT Arena scratch_arena_acquire();

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARENA_IMPL)) && !defined(JOT_ARENA_HAS_IMPL)
#define JOT_ARENA_HAS_IMPL

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

INTERNAL void _arena_debug_check_invarinats(Arena_Stack* arena);
INTERNAL void _arena_debug_fill_stack(Arena_Stack* arena);
INTERNAL void _arena_debug_fill_data(Arena_Stack* arena, isize size);

EXPORT void arena_deinit(Arena_Stack* arena)
{
    _arena_debug_check_invarinats(arena);
    if(arena->commit && arena->data)
       arena->commit(arena->data, arena->size, arena->reserved_size, false);
       
    memset(arena, 0, sizeof *arena);
}
EXPORT void arena_init_custom(Arena_Stack* arena, void* data, isize size, isize reserved_size, Arena_Stack_Commit_Func commit_or_null, isize stack_max_depth_or_zero)
{
    arena_deinit(arena);
    isize stack_max_depth = stack_max_depth_or_zero;

    isize* stack = (isize*) data;
    u8* aligned_data = (u8*) align_forward(data, sizeof *stack);
    isize aligned_size = size - (aligned_data - (u8*) data);
    isize stack_max_depth_that_fits = aligned_size / isizeof *stack;

    if(stack_max_depth <= 0)
        stack_max_depth = ARENA_DEF_STACK_SIZE;
    if(stack_max_depth > stack_max_depth_that_fits)
        stack_max_depth = stack_max_depth_that_fits;

    arena->used_to = stack_max_depth * isizeof *stack;
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
EXPORT isize arena_def_commit_func(void* addr, isize size, isize reserved_size, bool commit)
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

EXPORT void arena_init(Arena_Stack* arena, isize reserve_size_or_zero, isize stack_max_depth_or_zero, const char* name_or_null)
{
    if(reserve_size_or_zero <= 0)
        reserve_size_or_zero = ARENA_DEF_RESERVE_SIZE;

    void* data = platform_virtual_reallocate(NULL, reserve_size_or_zero, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_READ_WRITE);
    isize size = arena_def_commit_func(data, 0, reserve_size_or_zero, true);
    arena_init_custom(arena, data, size, reserve_size_or_zero, arena_def_commit_func, stack_max_depth_or_zero);
    arena->name = name_or_null;
}

ATTRIBUTE_INLINE_NEVER void* arena_unusual_push(Arena_Stack* arena, i32 depth, isize size, isize align)
{
    PERF_COUNTER_START();
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

    u8* data = (u8*) align_forward(arena->data + arena->used_to, align);
    u8* data_end = data + size;
    if(data_end > arena->data + arena->size)
    {
        data = NULL;
        if(arena->commit)
        {
            PERF_COUNTER_START(commit);
            isize new_size = arena->commit(arena->data, arena->size, arena->reserved_size, true);
            PERF_COUNTER_END(commit);
            if(new_size > arena->size)
            {
                _arena_debug_fill_data(arena, new_size - arena->size);
                arena->size = new_size;
                ASSERT(new_size < arena->reserved_size);
                data = arena_unusual_push(arena, depth, size, align);
            }
        }
    }
    else
    {
        arena->used_to = (isize) data_end - (isize) arena->data;
        _arena_debug_check_invarinats(arena);
    }
    
    PERF_COUNTER_END();
    return data;
}

static ATTRIBUTE_INLINE_ALWAYS void* arena_push_nonzero_inline(Arena* arena, isize size, isize align)
{
    PERF_COUNTER_START();
    ASSERT(arena->stack && arena->level > 0);
    Arena_Stack* stack = arena->stack;
    _arena_debug_check_invarinats(stack);

    u8* data = NULL;
    if(stack->stack_depht != arena->level || stack->used_to + size + align > stack->size)
        data = arena_unusual_push(stack, arena->level, size, align);
    else
    {
        data = (u8*) align_forward(stack->data + stack->used_to, align);
        stack->used_to = (isize) (data + size) - (isize) stack->data;
        ASSERT(stack->used_to <= stack->size);
    }
    PERF_COUNTER_END();
    return data;
}

EXPORT void* arena_push_nonzero(Arena* arena, isize size, isize align)
{
    return arena_push_nonzero_inline(arena, size, align);
}

EXPORT void* arena_push(Arena* arena, isize size, isize align)
{
    void* ptr = arena_push_nonzero(arena, size, align);
    memset(ptr, 0, (size_t) size);
    return ptr;
}

EXPORT void arena_release(Arena* arena)
{
    PERF_COUNTER_START();

    Arena_Stack* stack = arena->stack;
    ASSERT(arena->stack && arena->level > 0);
    _arena_debug_check_invarinats(stack);
    if(arena->level > 0 && stack->stack_depht >= arena->level)
    {
        isize* levels = (isize*) (void*) stack->data;
        isize new_used_to = levels[arena->level - 1];
        isize old_used_to = stack->used_to;

        isize stack_bytes = stack->stack_max_depth * isizeof *levels;
        ASSERT(stack_bytes <= new_used_to && new_used_to <= stack->used_to);

        if(stack->max_release_from_size < stack->used_to)
            stack->max_release_from_size = stack->used_to;

        stack->used_to = new_used_to;
        stack->stack_depht = arena->level - 1;
        stack->release_count += 1;

        //@TODO: we have memory corruption in file logger somewhere as this check
        //keeps firing but only for it. FIX THIS!
        //_arena_debug_fill_data(stack, old_used_to - new_used_to);
        _arena_debug_fill_data(stack, old_used_to - new_used_to + ARENA_DEBUG_DATA_SIZE);
        _arena_debug_check_invarinats(stack);
        memset(arena, 0, sizeof* arena);
    }

    PERF_COUNTER_END();
}

//Compatibility function for the allocator interface
EXPORT void* arena_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align)
{
    void* out = NULL;
    if(new_size > old_size)
    {
        Arena* arena = (Arena*) (void*) self;
        out = arena_push_nonzero_inline(arena, new_size, align);
        memcpy(out, old_ptr, (size_t) old_size);
    }

    return out;
}

EXPORT Allocator_Stats arena_get_allocatator_stats(Allocator* self)
{
    Arena* arena = (Arena*) (void*) self;
    Arena_Stack* stack = arena->stack;

    isize max_used_to = MAX(stack->used_to, stack->max_release_from_size);
    isize stack_to = stack->stack_max_depth * isizeof(isize);

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

EXPORT Arena arena_acquire(Arena_Stack* stack)
{
    PERF_COUNTER_START();

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
EXPORT Arena scratch_arena_acquire()
{
    if(_scratch_arena_stack.data == NULL)
        arena_init(&_scratch_arena_stack, 0, 0, "scratch arena stack");

    return arena_acquire(&_scratch_arena_stack);
}

INTERNAL void _arena_debug_check_invarinats(Arena_Stack* arena)
{
    if(ARENA_DEBUG)
    {
        isize* stack = (isize*) (void*) arena->data;
        ASSERT(arena->stack_max_depth * (isize)sizeof *stack <= arena->used_to && arena->used_to <= arena->size, "used_to needs to be within [0, size]");
        ASSERT(0 <= arena->stack_depht && arena->stack_depht <= arena->stack_max_depth, "used_to stack_depht to be within [0, stack_max_depth]");

        ASSERT((arena->data == NULL) == (arena->size == 0), "only permitted to be NULL when zero sized");

        isize till_end = arena->size - arena->used_to;
        isize check_size = MIN(till_end, ARENA_DEBUG_DATA_SIZE);
        ASSERT(memcmp_byte(arena->data + arena->used_to, ARENA_DEBUG_DATA_PATTERN, check_size) == 0, "The memory after the arena needs not be corrupted!");
        ASSERT(memcmp_byte(stack + arena->stack_depht, ARENA_DEBUG_STACK_PATTERN, (arena->stack_max_depth - arena->stack_depht) * isizeof *stack) == 0, "The memory after stack needs to be valid");
    }
}

INTERNAL void _arena_debug_fill_stack(Arena_Stack* arena)
{
    isize* stack = (isize*) (void*) arena->data;
    if(ARENA_DEBUG)
        memset(stack + arena->stack_depht, ARENA_DEBUG_STACK_PATTERN, (size_t) (arena->stack_max_depth - arena->stack_depht) * sizeof *stack);
}

INTERNAL void _arena_debug_fill_data(Arena_Stack* arena, isize size)
{
    if(ARENA_DEBUG)
    {
        isize till_end = arena->size - arena->used_to;
        isize check_size = MIN(till_end, size);
        memset(arena->data + arena->used_to, ARENA_DEBUG_DATA_PATTERN, (size_t) check_size);
    }
}
#endif