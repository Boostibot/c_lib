#ifndef JOT_ARENA_STACK
#define JOT_ARENA_STACK

// This is a "safe" implementation of the arena concept. It maintains the stack like order of allocations 
// on its own without the possibility of accidental overwriting of allocations from nested 
// scratch_acquire/scratch_release pairs.
// 
// This implementation views two concepts: 
// 1) Scratch_Arena - which holds the actual reserved memory and manages individual Scratch'es
// 2) Scratch - which represents scratch_acquire/scratch_release pair and allocates memory
// 
// Note that arenas (more broadly) except for their perf and cache locality dont provide 
// *any* benefits over a tracking allocator with linked list of allocations. As such they should
// mostly be use for scratch allocator functionality where the quick free-all is a major advantage.

#include "allocator.h"
#include "profile.h"

#ifndef ARENA_STACK_DEBUG
    #ifdef DO_ASSERTS_SLOW
        #define ARENA_STACK_DEBUG 1
    #else
        #define ARENA_STACK_DEBUG 0
    #endif
#endif

#ifndef ARENA_STACK_CUSTOM
    #define ARENA_STACK_CHANNELS         2
    #define ARENA_STACK_DEF_STACK_SIZE   256
    #define ARENA_STACK_DEF_RESERVE_SIZE (16*GB)
    #define ARENA_STACK_DEF_COMMIT_SIZE  ( 4*MB) 
#endif

typedef struct Scratch_Stack {
    union {
        u8* reserved_from;
        u8** frames;
    };
    u8** curr_frame; 
    u8*  commit_to; 
    u8*  reserved_to;
} Scratch_Stack;

typedef struct Scratch_Arena {
    Scratch_Stack channels[ARENA_STACK_CHANNELS];
    u32 frame_count;
    u32 frame_capacity;

    u8* reserved_from;
    isize reserved_size;
    isize commit_granularity;

    //purely informative
    const char* name;
    isize fall_count;
    isize rise_count;
    isize commit_count;
} Scratch_Arena;

//Models a single lifetime of allocations done from an arena. 
// Also can be though of as representing a scratch_acquire()/scratch_release() pair.
typedef struct Scratch {
    Allocator alloc[1];
    Scratch_Arena* arena;
    Scratch_Stack* stack;
    u8** frame_ptr;
    u32 level;
    u32 _;
} Scratch;

EXTERNAL Platform_Error scratch_arena_init(Scratch_Arena* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize stack_max_depth_or_zero);
EXTERNAL void scratch_arena_test_invariants(Scratch_Arena* arena);
EXTERNAL void scratch_arena_deinit(Scratch_Arena* arena);

EXTERNAL Scratch scratch_acquire(Scratch_Arena* arena);
EXTERNAL void  scratch_release(Scratch* arena);
EXTERNAL void* scratch_push_generic(Scratch* scratch, isize size, isize align, Allocator_Error* error);
EXTERNAL void* scratch_push_nonzero_generic(Scratch* scratch, isize size, isize align, Allocator_Error* error);
#define scratch_push(arena_ptr, count, Type)         ((Type*) scratch_push_generic((arena_ptr), (count) * sizeof(Type), __alignof(Type), NULL))
#define scratch_push_nonzero(arena_ptr, count, Type) ((Type*) scratch_push_nonzero_generic((arena_ptr), (count) * sizeof(Type), __alignof(Type), NULL))

EXTERNAL void* scratch_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
EXTERNAL Allocator_Stats scratch_allocator_get_stats(Allocator* self);

EXTERNAL Scratch_Arena* global_scratch_arena();
EXTERNAL Scratch        global_scratch_acquire();

#define SCRATCH_SCOPE(scratch) SCRATCH_SCOPE_FROM(scratch, global_scratch_arena())
#define SCRATCH_SCOPE_FROM(scratch, arena_ptr) \
    for(Scratch scratch = scratch_acquire(arena_ptr); scratch._ == 0; scratch_release(&scratch), scratch._ = 1)

#endif

// ===================================== REASONING =================================================
//
// We make this Scratch_Arena/Scratch distinction because it allows us to reason about the conglomerated lifetimes
// and provide the arena order guarantees. The problem at hand is deciding to what furthest point we are able to
// to rewind inside Scratch_Arena on each release of Scratch. If we did the usual rewinding to hard set index we could 
// invalidate the stack order of Scratches and override past allocations. 
//
// Consider the following scenario (using the names of the functions defined below):
// 
// SCRATCH_SCOPE(arena1) {
//     char* alloc1 = scratch_push(&arena1, 256, char); 
//     memcpy(alloc1, "hello world", 13);
//
//     SCRATCH_SCOPE(arena2) {
//         char* alloc2 = scratch_push(&arena2, 256, char); 
//     }
//    
//     puts(alloc1);
// }
//
// On each opening brace of SCRATCH_SCOPE(...) we mark an index in the arena linear memory to rewind to upon 
// reaching the closing brace. Thus, assuming nothing was allocated before, arena1 marks index 0, allocates some
// memory, arena 2 marks index after allocated memory, allocates some memory, rewinds back, and then the 
// previous allocation is printed. All good so far.
//
// Now consider the small change (annotated with comments for brevity):
//
// SCRATCH_SCOPE(arena1) {
//     SCRATCH_SCOPE(arena2) {
//         char* alloc1 = scratch_push(&arena1, 256, char); 
//         memcpy(alloc1, "hello world", 13);
//   
//         char* alloc2 = scratch_push(&arena2, 256, char); 
//     } //rewind *before* alloc1 was done!
//    
//     //allocate again overwriting the previous alloc1
//     SCRATCH_SCOPE(arena3) {
//         char* alloc3 = scratch_push(&arena2, 256, char); 
//     }
//
//     puts(alloc1); //where is my string?!
// }
//
// Obviously alloc1 gets overriden and we are unhappy. This example seems trivially avoidable, but its not.
// This situation does occur in practice, typically while implicitly passing arena across 
// a function boundary, for example by passing a dynamic array to function that will push to it (thus potentially 
// triggering realloc and the exact stuation shown above). This can happen even in a case when both the caller 
// and function called are 'well behaved' and handle arenas correctly - always clean up after themselves, 
// dont override their results etc. For example:
//
// void push_dec_string(String_Builder* builder, int i) {
//     SCRATCH_SCOPE(arena) {
//         char* temp = scratch_push(&arena, 100, char);
//         //fill temp ....
//         builder_append(builder, string_make(temp, 100));
//     }
// }
//
// void print_123() {
//     SCRATCH_SCOPE(arena) {
//         String_Builder builder = builder_make(arena.alloc, 100);
//         push_dec_string(&builder, 123);
//
//         //allocate some more...
//         char* temp = scratch_push(&arena, 100, char);
//         puts(builder.data); //where is my string?!
//     }
// }
//
// I hope you believe me that we do this thing *all the time* be it pushing to linked list, error log...
//
// Also note that this situation does happen when switching between any finite amount of backing memory regions. 
// We switch whenever we acquire arena thus in the example above arena1 would reside in memory 'A' while arena2 
// in memory 'B'. This would prevent that specific case above from breaking but not even two arenas (Ryan Fleury 
// style) will save us if we are not careful. I will be presuming two memory regions A and B in the examples 
// below but the examples trivially extend to N arenas.
// 
// To illustrate the point we will need to start talking about *frames*.
// Frame is a positive number starting at 1 that gets incremented every time we acquire Scratch from Scratch_Arena
// and decremented whenever we release the acquired Scratch. This corresponds to a depth in a stack.
//
// The diagrams show frame on the Y axis along with the memory region A, B where the frame resides in. The X axis
// shows the order of allocations. ### is symbol marking the alive space (but also time) region of an allocation. 
// It is preceeded by a number corresponding to the frame it was allocated from.
//
// First we illustrate the problem above with two memory regions A and B in diagram form.
// 
// frame
//   ^
// A |         3### [1]### //here we allocate at frame one from A
// B |     2###
// A | 1###
//   +--------------------------> time
// 
// After lifetime of 3 ends and we rewind to the start...
// 
//   ^
// B |     2### //Missing the last allocation thus we reached error state!
// A | 1###
//   +--------------------------> time
// 
// One potential fix is to enforce the arena like nesting by flattening out the scratch_acquire()/scratch_release() 
// on problematic allocations (I call this "fall"). We dont actually have to do anything besides ignoring calls 
// to scratch_release of frames 2 and 3. In diagram form:
// 
//   ^
// A |         3### 
// B |     2###     
// A | 1###         1### //fall
//   +--------------------------> time
// 
//                | Flatten
//                V 
//   ^
//   |         
//   |     
// A | 1###########1###  //We completely ignore the 2, 3 Allocations and are treating them as a part of 1
//   +--------------------------> time
//
// Now of course we are having a frame 2 and frame 3 worth of wasted memory inside the frame 1 allocation. 
// This is suboptimal but clearly better then having a hard to track down error.
//
// Moving on lets say that after frame 3 allocated again. This triggers a "rise" 
//
//            *prev state*
//                 | Rise
//                 V
//   ^                 
// A |                 3######
// B |                 2 
// A | 1###############  
//   +--------------------------> time
// 
// In other words we simply continued allocating from the end but before doing so set all levels 
// between us an the previous fall such that they form a valid stack.
// 
// ===================================== MULTIPLE STACKS =================================================
// Even though having N backing arenas does not solve the issue it makes it dramatically less likely.
// We can incorporate this (experimental) into our design by instead of having single stack, we have multiple
// and switch betwen them. The fall/rise events are only tracked within a single stack, but the current max depth 
// of the stack is shared. Practically speaking its extremely rare to need more than 2 so we have 2
// (although one can set the define below to any value).


#if (defined(JOT_ALL_IMPL) || defined(JOT_ARENA_STACK_IMPL)) && !defined(JOT_ARENA_STACK_HAS_IMPL)
#define JOT_ARENA_STACK_HAS_IMPL

    INTERNAL void _scratch_arena_check_invariants(Scratch_Arena* arena);
    INTERNAL void _scratch_arena_fill_garbage(Scratch_Arena* arena, isize content_size);

    EXTERNAL void scratch_arena_deinit(Scratch_Arena* arena)
    {
        _scratch_arena_check_invariants(arena);
        if(arena->reserved_from)
            platform_virtual_reallocate(NULL, arena->reserved_from, arena->reserved_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        memset(arena, 0, sizeof *arena);
    }

    EXTERNAL Platform_Error scratch_arena_init(Scratch_Arena* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize level_count_or_zero)
    {
        scratch_arena_deinit(arena);
        isize alloc_granularity = platform_allocation_granularity();
    
        //validate and normalize args
        REQUIRE(reserve_size_or_zero >= 0);
        REQUIRE(commit_granularity_or_zero >= 0);
        REQUIRE(level_count_or_zero >= 0);
        REQUIRE(alloc_granularity >= 1);
    
        isize commit_granularity = commit_granularity_or_zero > 0 ? commit_granularity_or_zero : ARENA_STACK_DEF_COMMIT_SIZE;
        isize reserve_size = reserve_size_or_zero > 0 ? reserve_size_or_zero : ARENA_STACK_DEF_RESERVE_SIZE;
        isize level_count = level_count_or_zero > 0 ? level_count_or_zero : ARENA_STACK_DEF_STACK_SIZE;
        
        #define _ROUND_UP(val, to) (((val) + (to) - 1)/(to)*(to))

        commit_granularity = _ROUND_UP(commit_granularity, alloc_granularity);
        reserve_size = _ROUND_UP(reserve_size, alloc_granularity*ARENA_STACK_CHANNELS);
        level_count = MIN(level_count, reserve_size/isizeof(u8*));
        level_count = _ROUND_UP(level_count, ARENA_STACK_CHANNELS);

        //reserve eveyrthing
        u8* reserved_from = 0;
        Platform_Error error = platform_virtual_reallocate((void**) &reserved_from, NULL, reserve_size, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_NO_ACCESS);
            
        //commit levels
        u8* datas[ARENA_STACK_CHANNELS] = {NULL};
        isize frames_commit_size = _ROUND_UP(level_count*isizeof(u8*)/ARENA_STACK_CHANNELS, commit_granularity);
        for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
        {
            if(error != 0)
                break;

            datas[i] = reserved_from + reserve_size/ARENA_STACK_CHANNELS*i;
            error = platform_virtual_reallocate(NULL, datas[i], frames_commit_size, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
        }
    
        //fill struct
        if(error == 0)
        {
            for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
            {
                Scratch_Stack* stack = &arena->channels[i];
                stack->reserved_from = datas[i];
                stack->reserved_to = datas[i] + reserve_size/ARENA_STACK_CHANNELS;
                stack->commit_to = datas[i] + frames_commit_size;
                stack->curr_frame = stack->frames;
                *stack->curr_frame = (u8*) (stack->frames + level_count/ARENA_STACK_CHANNELS);
            }
        
            arena->commit_granularity = commit_granularity;
            arena->frame_capacity = (u32) level_count;
            arena->reserved_size = reserve_size;
            arena->name = name;
            arena->frame_count = 0;
        
            _scratch_arena_fill_garbage(arena, frames_commit_size);
        }
        else
        {  
            if(reserved_from)
                platform_virtual_reallocate(NULL, reserved_from, reserve_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        }
    
        _scratch_arena_check_invariants(arena);
        return error;
    }

    EXTERNAL ATTRIBUTE_INLINE_NEVER void* _scratch_handle_unusual_push(Scratch_Arena* arena, Scratch_Stack* stack, u8** frame_ptr, isize size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        _scratch_arena_check_invariants(arena);
        
        u8* used_to = *stack->curr_frame;
    
        //fall
        if(frame_ptr < stack->curr_frame)
        {
            *frame_ptr = used_to;
            arena->fall_count += 1;
        }

        //rise
        if(frame_ptr > stack->curr_frame)
        {
            for(u8** ptr = stack->curr_frame + 1; ptr <= frame_ptr; ptr++)
                *ptr = used_to;
            arena->rise_count += 1;
        }

        u8* out = (u8*) align_forward(*frame_ptr, align);
        u8* new_used_to = out + size;
        isize commit = 0;
        if((u8*) new_used_to > stack->commit_to)
        {
            commit = DIV_CEIL(size, arena->commit_granularity)*arena->commit_granularity;
            ASSERT((size_t) stack->commit_to % platform_allocation_granularity() == 0);

            if(stack->commit_to + commit > stack->reserved_to)
            {
                out = NULL;
                allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, size, NULL, 0, align, 
                    "More memory is needed then reserved! Reserved: %.2lf MB, commit: %.2lf MB", 
                    (double) (stack->reserved_to - stack->reserved_from)/MB, (double) (stack->commit_to - stack->reserved_from)/MB);
                goto end;
            }
            
            Platform_Error platform_error = platform_virtual_reallocate(NULL, stack->commit_to, commit, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
            if(platform_error)
            {
                out = NULL;
                char buffer[4096];
                platform_translate_error(platform_error, buffer, sizeof buffer);
                allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, size, NULL, 0, align, 
                    "Virtual memory commit failed! Error: %s", buffer);
                goto end;
            }

            arena->commit_count += 1;
            stack->commit_to += commit;
        }

        stack->curr_frame = frame_ptr;
        *frame_ptr = new_used_to;

        _scratch_arena_fill_garbage(arena, commit);
        _scratch_arena_check_invariants(arena);

        end:
        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* scratch_push_nonzero_generic(Scratch* scratch, isize size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        REQUIRE(scratch->arena && scratch->stack && 0 <= scratch->level && scratch->level <= scratch->arena->frame_count, 
            "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");
        Scratch_Stack* stack = scratch->stack;
        _scratch_arena_check_invariants(scratch->arena);

        u8* out = (u8*) align_forward(*scratch->frame_ptr, align);
        if(stack->curr_frame != scratch->frame_ptr || out + size > stack->commit_to)
            out = (u8*) _scratch_handle_unusual_push(scratch->arena, scratch->stack, scratch->frame_ptr, size, align, error);
        else
        {
            *scratch->frame_ptr = out + size;
            _scratch_arena_check_invariants(scratch->arena);
        }

        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* scratch_push_generic(Scratch* scratch, isize size, isize align, Allocator_Error* error)
    {
        void* frame_ptr = scratch_push_nonzero_generic(scratch, size, align, error);
        memset(frame_ptr, 0, (size_t) size);
        return frame_ptr;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Scratch scratch_acquire(Scratch_Arena* arena)
    {
        PROFILE_START();
        REQUIRE(arena->frame_count < arena->frame_capacity, "Too many arena frames or uninit");
        _scratch_arena_check_invariants(arena);

        u32 level_i   = arena->frame_count / ARENA_STACK_CHANNELS;
        u32 channel_i = arena->frame_count % ARENA_STACK_CHANNELS;
        Scratch_Stack* stack = &arena->channels[channel_i];
    
        //Here we could do a full for loop setting all frames affected by the 'rise' 
        // similar to the one in "_scratch_handle_unusual_push"
        // However, the situation that requires the full for loop is very unlikely. 
        // Thus we only do one frame_ptr (which is the usual case) and let the rest be 
        // handled in "_scratch_handle_unusual_push".
        *(stack->curr_frame + 1) = *stack->curr_frame;
        stack->curr_frame += 1;

        Scratch out = {0};
        out.frame_ptr = stack->frames + level_i + 1;
        out.level = arena->frame_count;
        out.arena = arena;
        out.stack = stack;

        //Assign the arena frame. Fill the allocator part. 
        // For simple cases (acquire frame, push, release frame) this gets optimized away.
        out.alloc[0].func = scratch_allocator_func;
        out.alloc[0].get_stats = scratch_allocator_get_stats;

        arena->frame_count += 1;

        _scratch_arena_check_invariants(arena);
        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void scratch_release(Scratch* scratch)
    {
        PROFILE_START();
        REQUIRE(scratch->arena && 0 <= scratch->level && scratch->level <= scratch->arena->frame_count, 
            "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");

        Scratch_Arena* arena = scratch->arena;
        Scratch_Stack* stack = scratch->stack;
        _scratch_arena_check_invariants(arena);
    
        u8* old_used_to = *stack->curr_frame;
        stack->curr_frame = MIN(stack->curr_frame, scratch->frame_ptr - 1); 
        arena->frame_count = scratch->level;

        _scratch_arena_fill_garbage(arena, old_used_to - *stack->curr_frame);
        _scratch_arena_check_invariants(arena);
    
        //Set the frame to zero so that if someone tries to allocate from this frame
        // it will trigger an assert
        if(ARENA_STACK_DEBUG)
            memset(scratch, 0, sizeof* scratch);

        PROFILE_STOP();
    }

    EXTERNAL void* scratch_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
    {
        Scratch* scratch = (Scratch*) (void*) self;
        void* out = scratch_push_generic(scratch, new_size, align, error);
        if(out)
            memcpy(out, old_ptr, (size_t) MIN(old_size, new_size));
    
        return out;
    }

    EXTERNAL Allocator_Stats scratch_allocator_get_stats(Allocator* self)
    {
        Scratch* scratch = (Scratch*) (void*) self;
        Allocator_Stats stats = {0};
    
        Scratch_Arena* arena = scratch->arena;
        Scratch_Stack* stack = scratch->stack;
        u8* start = *(scratch->frame_ptr - 1);
        stats.type_name = "Scratch";
        stats.name = arena->name;
        stats.is_top_level = true;
        stats.is_capable_of_free_all = true;
        stats.fixed_memory_pool_size = stack->reserved_to - start;
        stats.bytes_allocated = *scratch->frame_ptr - start;
        stats.max_bytes_allocated = *scratch->frame_ptr - start;
        return stats;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Scratch_Arena* global_scratch_arena()
    {
        static ATTRIBUTE_THREAD_LOCAL Scratch_Arena _scratch_stack = {0};
        return &_scratch_stack;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Scratch global_scratch_acquire()
    {
        return scratch_acquire(global_scratch_arena());
    }

    #define ARENA_STACK_DEBUG_DATA_SIZE     32
    #define ARENA_STACK_DEBUG_DATA_PATTERN  0x55
    #define ARENA_STACK_DEBUG_STACK_PATTERN (u8*) 0x6666666666666666
    EXTERNAL void scratch_arena_test_invariants(Scratch_Arena* arena)
    {
        if(arena->reserved_from == NULL)
            return;

        TEST(arena->commit_granularity >= 1);
        TEST(arena->reserved_size >= 1);
        TEST(arena->frame_capacity >= 1);

        for(isize k = 0; k < ARENA_STACK_CHANNELS; k++)
        {
            Scratch_Stack* stack = &arena->channels[k];

            u8** frames_end = stack->frames + arena->frame_capacity/ARENA_STACK_CHANNELS;

            u8* used_from = (u8*) frames_end;
            u8* used_to = stack->curr_frame ? *stack->curr_frame : NULL;

            TEST(stack->frames <= stack->curr_frame && stack->curr_frame <= frames_end);
            TEST(used_from <= used_to && used_to <= stack->commit_to && stack->commit_to <= stack->reserved_to);

            for(u8** level = (u8**) stack->frames; level < stack->curr_frame; level++)
                TEST(used_from <= *level && *level <= used_to);
            
            if(ARENA_STACK_DEBUG)
            {
                for(u8** frame_ptr = stack->curr_frame + 1; frame_ptr < frames_end; frame_ptr++)
                    TEST(*frame_ptr == ARENA_STACK_DEBUG_STACK_PATTERN);
            
                isize till_end = stack->commit_to - used_to;
                isize check_size = CLAMP(ARENA_STACK_DEBUG_DATA_SIZE, 0, till_end);
                for(isize i = 0; i < check_size; i++)
                    TEST(used_to[i] == ARENA_STACK_DEBUG_DATA_PATTERN);
            }
        }
    }

    INTERNAL void _scratch_arena_check_invariants(Scratch_Arena* arena)
    {
        if(ARENA_STACK_DEBUG)
            scratch_arena_test_invariants(arena);
    }

    INTERNAL void _scratch_arena_fill_garbage(Scratch_Arena* arena, isize content_size)
    {
        if(ARENA_STACK_DEBUG)
            for(isize k = 0; k < ARENA_STACK_CHANNELS; k++)
            {
                Scratch_Stack* stack = &arena->channels[k];

                //Fill arena
                u8** frames_end = stack->frames + arena->frame_capacity/ARENA_STACK_CHANNELS;
                for(u8** frame_ptr = stack->curr_frame + 1; frame_ptr < frames_end; frame_ptr++)
                    *frame_ptr = ARENA_STACK_DEBUG_STACK_PATTERN;

                //Fill content
                u8* used_to = *stack->curr_frame;
                isize till_end = stack->commit_to - used_to;
                isize check_size = CLAMP(content_size, 0, till_end);
                memset(used_to, ARENA_STACK_DEBUG_DATA_PATTERN, (size_t) check_size);
            }
    }
#endif

#include "string.h"
void main()
{
    SCRATCH_SCOPE(arena1) {
        int* alloc1 = scratch_push(&arena1, 256, int); 

        SCRATCH_SCOPE(arena2) {
            void* alloc2 = scratch_push(&arena2, 256, int); 
        }
    }
}
