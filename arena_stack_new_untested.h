#ifndef JOT_ARENA_STACK
#define JOT_ARENA_STACK

// This is a 'safe' implementation of the arena concept. It maintains the stack like order of allocations 
// on its own without the possibility of accidental invalidation of allocations from 'lower' frames. 
// (Read more for proper explanations).
// 
// Arena_Frame is a linear allocator allocating from a growing buffer, which allows for
// extremely quick free-all/reset operation (just move the index back). 
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
// To illustrate the point we will need to start talking about *frames*.
// Level is a positive number starting at 1 that gets incremented every time we acquire Arena_Frame from Arena_Stack
// and decremented whenever we release the acquired Arena_Frame. This corresponds to a depth in a stack.
//
// The diagrams show frame_ptr on the Y axis along with the memory region A, B where the frame_ptr resides in. The X axis
// shows the order of allocations. ### is symbol marking the alive region of an allocation. It is preceeded by a 
// number corresponding to the frame_ptr it was allocated from.
//
// First we illustrate the problem above with two memory regions A and B in diagram form.
// 
// frame_ptr
//   ^
// A |         3### [1]### //here we allocate at frame_ptr one from A
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
// to arena_frame_release of frames 2 and 3 (somehow). In diagram form:
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
// Now of course we are having a frame_ptr 2 and frame_ptr 3 worth of wasted memory inside the frame_ptr 1 allocation. 
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

#ifndef ARENA_STACK_DEBUG
    #ifdef NDEBUG
        #define ARENA_STACK_DEBUG 0
    #else
        #define ARENA_STACK_DEBUG 1
    #endif
#endif

#ifndef ARENA_STACK_CUSTOM
    #define ARENA_STACK_CHANNELS         2
    #define ARENA_STACK_DEF_STACK_SIZE   256
    #define ARENA_STACK_DEF_RESERVE_SIZE (16*GB)
    #define ARENA_STACK_DEF_COMMIT_SIZE  ( 4*MB) 
#endif

typedef struct Arena_Stack_Channel {
    u8** frames;
    u8** curr_frame; 
    u8* commit_to;
    u8* reserved_to;
    u8* reserved_from;
} Arena_Stack_Channel;

typedef struct Arena_Stack {
    Arena_Stack_Channel channels[ARENA_STACK_CHANNELS];
    u32 frame_count;
    u32 frame_capacity;

    u8* reserved_from;
    isize reserved_size;
    isize commit_granularity;

    const char* name;
} Arena_Stack;

//Models a single lifetime of allocations done from an arena. 
// Also can be though of as representing a arena_frame_acquire()/arena_frame_release() pair.
typedef struct Arena_Frame {
    Allocator alloc[1];
    Arena_Stack* stack;
    Arena_Stack_Channel* channel;
    u8** ptr;
    u32 index;
    u32 _;
} Arena_Frame;

EXTERNAL Platform_Error arena_stack_init(Arena_Stack* arena, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize stack_max_depth_or_zero);
EXTERNAL void arena_stack_test_invariants(Arena_Stack* stack);
EXTERNAL void arena_stack_deinit(Arena_Stack* arena);

EXTERNAL Arena_Frame arena_frame_acquire(Arena_Stack* arena);
EXTERNAL void arena_frame_release(Arena_Frame* arena);
EXTERNAL void* arena_frame_push(Arena_Frame* arena, isize size, isize align);
EXTERNAL void* arena_frame_push_nonzero(Arena_Frame* arena, isize size, isize align);
EXTERNAL void* arena_frame_push_nonzero_error(Arena_Frame* frame, isize size, isize align, Allocator_Error* error);

EXTERNAL void* arena_frame_allocator_func(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
EXTERNAL Allocator_Stats arena_frame_allocator_get_stats(Allocator* self);

EXTERNAL Arena_Stack* scratch_arena_stack();
EXTERNAL Arena_Frame scratch_arena_frame_acquire();

#define ARENA_FRAME_PUSH(arena_ptr, count, Type) ((Type*) arena_frame_push((arena_ptr), (count) * sizeof(Type), __alignof(Type)))

#define SCRATCH_ARENA(frame_name) \
    for(int PP_UNIQ(_defer_) = 0; PP_UNIQ(_defer_) == 0; PP_UNIQ(_defer_) = 1) \
        for(Arena_Frame frame_name = scratch_arena_frame_acquire(); PP_UNIQ(_defer_) == 0; arena_frame_release(&frame_name), PP_UNIQ(_defer_) = 1) \

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ARENA_STACK_IMPL)) && !defined(JOT_ARENA_STACK_HAS_IMPL)
#define JOT_ARENA_STACK_HAS_IMPL

    INTERNAL void _arena_stack_check_invariants(Arena_Stack* arena);
    INTERNAL void _arena_stack_fill_garbage(Arena_Stack* arena, isize content_size);

    EXTERNAL void arena_stack_deinit(Arena_Stack* stack)
    {
        _arena_stack_check_invariants(stack);
        if(stack->reserved_from)
            platform_virtual_reallocate(NULL, stack->reserved_from, stack->reserved_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        memset(stack, 0, sizeof *stack);
    }

    EXTERNAL Platform_Error arena_stack_init(Arena_Stack* stack, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize frame_count_or_zero)
    {
        arena_stack_deinit(stack);
        isize alloc_granularity = platform_allocation_granularity();
    
        //validate and normalize arguments
        REQUIRE(reserve_size_or_zero >= 0);
        REQUIRE(commit_granularity_or_zero >= 0);
        REQUIRE(frame_count_or_zero >= 0);
        REQUIRE(alloc_granularity >= 1);
    
        isize commit_granularity = commit_granularity_or_zero > 0 ? commit_granularity_or_zero : ARENA_STACK_DEF_COMMIT_SIZE;
        isize reserve_size = reserve_size_or_zero > 0 ? reserve_size_or_zero : ARENA_STACK_DEF_RESERVE_SIZE;
        isize frame_capacity = frame_count_or_zero > 0 ? frame_count_or_zero : ARENA_STACK_DEF_STACK_SIZE;

        commit_granularity = DIV_CEIL(commit_granularity, alloc_granularity)*alloc_granularity;
        reserve_size = DIV_CEIL(reserve_size, alloc_granularity)*alloc_granularity;
        frame_capacity = MIN(frame_capacity, reserve_size/isizeof(u8*));

        //reserve
        u8* reserved_from = 0;
        Platform_Error error = platform_virtual_reallocate((void**) &reserved_from, NULL, reserve_size, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        
        //prepare channels and commit levels to each
        Arena_Stack_Channel channels[ARENA_STACK_CHANNELS] = {0};
        isize frames_commit_size = DIV_CEIL(frame_capacity, commit_granularity)*commit_granularity;
        if(error == 0)
        {
            isize reserved_per_channel = reserve_size/ARENA_STACK_CHANNELS;
            for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
            {
                u8* base = reserved_from + reserved_per_channel*i;
                error = platform_virtual_reallocate(NULL, base, frames_commit_size, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
                if(error != 0)
                    break;

                channels[i].reserved_from = base;
                channels[i].reserved_to = base + reserved_per_channel;
                channels[i].commit_to = base + frames_commit_size;
                channels[i].frames = (u8**) (void*) base;
                channels[i].curr_frame = channels[i].frames;
                *channels[i].curr_frame = base + frame_capacity*sizeof(u8*);
            }
        }            
    
        //write into stack or error reciver
        if(error) 
        {
            if(reserved_from)
                platform_virtual_reallocate(NULL, reserved_from, reserve_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        }
        else
        {
            memcpy(stack->channels, channels, sizeof channels);
            stack->frame_capacity = (u32) frame_capacity;
            stack->frame_count = 1;
            stack->reserved_size = reserve_size;
            stack->commit_granularity = commit_granularity;
            stack->name = name;
            _arena_stack_fill_garbage(stack, frames_commit_size);
        }

        _arena_stack_check_invariants(stack);
        return error;
    }

    EXTERNAL ATTRIBUTE_INLINE_NEVER void* _arena_frame_handle_unusual_push(Arena_Stack* stack, Arena_Stack_Channel* channel, u8** frame_ptr, isize size, isize align, Allocator_Error* error)
    {
        _arena_stack_check_invariants(stack);
        
        u8* out = (u8*) align_forward(*frame_ptr, align);
        isize commit = DIV_CEIL(size, stack->commit_granularity)*stack->commit_granularity;
        ASSERT(out + size > channel->commit_to);
        ASSERT((isize) channel->commit_to % platform_allocation_granularity() == 0);

        if(channel->commit_to + commit > channel->reserved_to)
        {
            isize reserved_size = channel->reserved_to - channel->reserved_from;
            isize used_size = channel->commit_to - channel->reserved_from;
            allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, size, NULL, 0, align, 
                "More memory is needed then reserved! Reserved: %.2lf MB, commit: %.2lf MB", 
                (double) reserved_size/MB, (double) used_size/MB);
            return NULL;
        }
        
        Platform_Error platform_error = platform_virtual_reallocate(
            NULL, channel->commit_to, commit, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
        if(platform_error)
        {
            char buffer[4096];
            platform_translate_error(platform_error, buffer, sizeof buffer);
            allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, size, NULL, 0, align, 
                "Virtual memory commit failed! Error: %s", buffer);
            return NULL;
        }

        channel->commit_to += commit;
        _arena_stack_fill_garbage(stack, commit);

        *frame_ptr = out + size;
        for(u8** curr = channel->curr_frame; curr > frame_ptr; curr++)
            *curr = out + size;

        _arena_stack_check_invariants(stack);
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push_nonzero_error(Arena_Frame* frame, isize size, isize align, Allocator_Error* error)
    {
        REQUIRE(frame->stack && frame->channel && 1 <= frame->index && frame->index < frame->stack->frame_count, 
            "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");

        Arena_Stack_Channel* channel = frame->channel;
        _arena_stack_check_invariants(frame->stack);

        u8* out = (u8*) align_forward(*frame->ptr, align);
        u8* after = out + size;
        if(after > channel->commit_to)
            return _arena_frame_handle_unusual_push(frame->stack, frame->channel, frame->ptr, size, align, error);
            
        //this generates awfull mess...
        for(u8** curr = channel->curr_frame; curr > frame->ptr; curr++)
            *curr = after;

        *frame->ptr = after;
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push_nonzero(Arena_Frame* frame, isize size, isize align)
    {
        return arena_frame_push_nonzero_error(frame, size, align, NULL);
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push(Arena_Frame* frame, isize size, isize align)
    {
        void* ptr = arena_frame_push_nonzero_error(frame, size, align, NULL);
        memset(ptr, 0, (size_t) size);
        return ptr;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Frame arena_frame_acquire(Arena_Stack* stack)
    {
        ASSERT_BOUNDS(0 < stack->frame_count && stack->frame_count < stack->frame_capacity, "Too many arena frames!");
        _arena_stack_check_invariants(stack);

        u32 frame_i   = stack->frame_count / ARENA_STACK_CHANNELS;
        u32 channel_i = stack->frame_count % ARENA_STACK_CHANNELS;
        Arena_Stack_Channel* channel = &stack->channels[channel_i];
        u8** frame_ptr = channel->frames + frame_i;
    
        Arena_Frame frame = {0};
        frame.alloc[0].func = arena_frame_allocator_func;
        frame.alloc[0].get_stats = arena_frame_allocator_get_stats;
        frame.ptr = frame_ptr;
        frame.channel = channel;
        frame.index = stack->frame_count;
        frame.stack = stack;

        *frame_ptr = *channel->curr_frame;
        channel->curr_frame += 1;
        stack->frame_count += 1;

        _arena_stack_check_invariants(stack);
        return frame;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void arena_frame_release(Arena_Frame* frame)
    {
        REQUIRE(frame->stack && 1 <= frame->index && frame->index < frame->stack->frame_count, 
            "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");

        Arena_Stack* stack = frame->stack;
        Arena_Stack_Channel* channel = frame->channel;
        REQUIRE(channel->curr_frame >= frame->ptr);
        _arena_stack_check_invariants(stack);
    
        u8* old_used_to = *channel->curr_frame;
        channel->curr_frame = frame->ptr - 1; 
        stack->frame_count = frame->index;

        _arena_stack_fill_garbage(stack, old_used_to - *channel->curr_frame);
        _arena_stack_check_invariants(stack);

        if(ARENA_STACK_DEBUG)
            frame->stack = 0;
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
    
        Arena_Stack* stack = frame->stack;
        Arena_Stack_Channel* channel = frame->channel;
        u8* start = *(frame->ptr - 1);
        stats.type_name = "Arena_Frame";
        stats.name = stack->name;
        stats.is_top_level = true;
        stats.is_capable_of_free_all = true;
        stats.fixed_memory_pool_size = channel->reserved_to - start;
        stats.bytes_allocated = *frame->ptr - start;
        stats.max_bytes_allocated = *frame->ptr - start;
        return stats;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Stack* scratch_arena_stack()
    {
        static ATTRIBUTE_THREAD_LOCAL Arena_Stack _scratch_stack = {0};
        return &_scratch_stack;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Frame scratch_arena_frame_acquire()
    {
        REQUIRE(scratch_arena_stack()->reserved_size > 0, "Must be already init!");
        return arena_frame_acquire(scratch_arena_stack());
    }

    #define ARENA_STACK_DEBUG_DATA_SIZE     32
    #define ARENA_STACK_DEBUG_DATA_PATTERN  0x55
    #define ARENA_STACK_DEBUG_STACK_PATTERN (u8*) 0x6666666666666666
    EXTERNAL void arena_stack_test_invariants(Arena_Stack* stack)
    {
        TEST(stack->commit_granularity >= 0);
        TEST(stack->reserved_size >= 0);
        TEST(stack->frame_capacity >= 0);

        for(isize k = 0; k < ARENA_STACK_CHANNELS; k++)
        {
            Arena_Stack_Channel* channel = &stack->channels[k];

            u8** frames_end = channel->frames + stack->frame_capacity;
            TEST(channel->frames <= channel->curr_frame && channel->curr_frame <= frames_end);

            u8* used_from = (u8*) frames_end;
            u8* used_to = *channel->curr_frame;
            TEST(used_from <= used_to && used_to <= channel->commit_to && channel->commit_to <= channel->reserved_to);
            for(u8** frame = (u8**) channel->frames; frame < channel->curr_frame; frame++)
                TEST(used_from <= *frame && *frame <= used_to);
            
            if(ARENA_STACK_DEBUG)
            {
                for(u8** frame_ptr = channel->curr_frame + 1; frame_ptr < frames_end; frame_ptr++)
                    TEST(*frame_ptr == ARENA_STACK_DEBUG_STACK_PATTERN);
            
                isize till_end = channel->commit_to - used_to;
                isize check_size = CLAMP(ARENA_STACK_DEBUG_DATA_SIZE, 0, till_end);
                for(isize i = 0; i < check_size; i++)
                    TEST(used_to[i] == ARENA_STACK_DEBUG_DATA_PATTERN);
            }
        }
    }

    INTERNAL void _arena_stack_check_invariants(Arena_Stack* stack)
    {
        if(ARENA_STACK_DEBUG)
            arena_stack_test_invariants(stack);
    }

    INTERNAL void _arena_stack_fill_garbage(Arena_Stack* stack, isize content_size)
    {
        if(ARENA_STACK_DEBUG)
            for(isize k = 0; k < ARENA_STACK_CHANNELS; k++)
            {
                Arena_Stack_Channel* channel = &stack->channels[k];

                //Fill frame ptrs
                u8** frames_end = channel->frames + stack->frame_capacity;
                for(u8** frame_ptr = channel->curr_frame + 1; frame_ptr < frames_end; frame_ptr++)
                    *frame_ptr = ARENA_STACK_DEBUG_STACK_PATTERN;

                //Fill content
                u8* used_to = *channel->curr_frame;
                isize till_end = channel->commit_to - used_to;
                isize check_size = CLAMP(content_size, 0, till_end);
                memset(used_to, ARENA_STACK_DEBUG_DATA_PATTERN, (size_t) check_size);
            }
    }
#endif