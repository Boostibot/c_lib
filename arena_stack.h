#ifndef JOT_ARENA_STACK
#define JOT_ARENA_STACK

// This is a 'safe' implementation of the arena concept. It maintains the stack like order of allocations 
// on its own without the possibility of accidental invalidation of allocations from 'lower' levels. 
// (Read more for proper explanations).
// 
// Arena_Frame is a allocator used to conglomerate individual allocations to a continuous buffer, which allows for
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
// To illustrate the point we will need to start talking about *levels*.
// Level is a positive number starting at 1 that gets incremented every time we acquire Arena_Frame from Arena_Stack
// and decremented whenever we release the acquired Arena_Frame. This corresponds to a depth in a stack.
//
// The diagrams show next_level_ptr on the Y axis along with the memory region A, B where the next_level_ptr resides in. The X axis
// shows the order of allocations. ### is symbol marking the alive region of an allocation. It is preceeded by a 
// number corresponding to the next_level_ptr it was allocated from.
//
// First we illustrate the problem above with two memory regions A and B in diagram form.
// 
// next_level_ptr
//   ^
// A |         3### [1]### //here we allocate at next_level_ptr one from A
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
// Now of course we are having a next_level_ptr 2 and next_level_ptr 3 worth of wasted memory inside the next_level_ptr 1 allocation. 
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
    union {
        u8* data;
        u8** levels;
    };
    u8*  commit_to;
    u8** active_level; 
} Arena_Stack_Channel;

typedef struct Arena_Stack {
    Arena_Stack_Channel channels[ARENA_STACK_CHANNELS];
    u32 max_alive_level;
    u32 channel_level_count;
    u32 total_level_count;
    u32 _;

    isize commit_granularity;
    isize channel_reserved_size;
    isize total_reserved_size;

    //purely informative
    const char* name;
    isize fall_count;
    isize rise_count;
    isize commit_count;
} Arena_Stack;

//Models a single lifetime of allocations done from an arena. 
// Also can be though of as representing a ScratchBegin()/ScratchEnd() pair.
typedef struct Arena_Frame {
    Allocator alloc[1];
    Arena_Stack* stack;
    Arena_Stack_Channel* channel;
    u8** next_level_ptr;
    u32 level;
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
        for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
            if(stack->channels[i].data)
                platform_virtual_reallocate(NULL, stack->channels[i].data, stack->channel_reserved_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        memset(stack, 0, sizeof *stack);
    }

    EXTERNAL Platform_Error arena_stack_init(Arena_Stack* stack, const char* name, isize reserve_size_or_zero, isize commit_granularity_or_zero, isize level_count_or_zero)
    {
        arena_stack_deinit(stack);
        isize alloc_granularity = platform_allocation_granularity();
    
        ASSERT_PARAMS(reserve_size_or_zero >= 0);
        ASSERT_PARAMS(commit_granularity_or_zero >= 0);
        ASSERT_PARAMS(alloc_granularity >= 0);
    
        isize commit_granularity = commit_granularity_or_zero > 0 ? commit_granularity_or_zero : ARENA_STACK_DEF_COMMIT_SIZE;
        commit_granularity = DIV_CEIL(commit_granularity, alloc_granularity)*alloc_granularity;

        isize reserve_size = reserve_size_or_zero > 0 ? reserve_size_or_zero : ARENA_STACK_DEF_RESERVE_SIZE;
        isize channel_reserve_size = DIV_CEIL(reserve_size, alloc_granularity*ARENA_STACK_CHANNELS)*alloc_granularity;
        reserve_size = channel_reserve_size*ARENA_STACK_CHANNELS;

        isize level_count = level_count_or_zero > 0 ? level_count_or_zero : ARENA_STACK_DEF_STACK_SIZE;
        isize channel_level_count = MIN(level_count/ARENA_STACK_CHANNELS, reserve_size/isizeof(u8*));
        level_count = channel_level_count*ARENA_STACK_CHANNELS;

        isize levels_commit_size = DIV_CEIL(level_count, commit_granularity)*commit_granularity;

        u8* datas[ARENA_STACK_CHANNELS] = {NULL};
        Platform_Error error = 0;
        for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
        {
            error = platform_virtual_reallocate((void**) &datas[i], NULL, reserve_size, PLATFORM_VIRTUAL_ALLOC_RESERVE, PLATFORM_MEMORY_PROT_NO_ACCESS);
            if(error != 0)
                break;

            error = platform_virtual_reallocate(NULL, datas[i], levels_commit_size, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
            if(error != 0)
                break;
        }
    
        if(error == 0)
        {
            for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
            {
                Arena_Stack_Channel* channel = &stack->channels[i];
                channel->data = datas[i];
                channel->commit_to = channel->data + levels_commit_size;
                channel->active_level = channel->levels;
                *channel->active_level = (u8*) (channel->levels + level_count);
            }
        
            stack->commit_granularity = commit_granularity;
            stack->channel_level_count = (u32) channel_level_count;
            stack->total_level_count = (u32) level_count;
            stack->channel_reserved_size = channel_reserve_size;
            stack->total_reserved_size = reserve_size;
            stack->name = name;
            stack->max_alive_level = 0;
        
            _arena_stack_fill_garbage(stack, levels_commit_size);
        }
        else
        {  
            for(isize i = 0; i < ARENA_STACK_CHANNELS; i++)
                if(datas[i])
                    platform_virtual_reallocate(NULL, datas[i], reserve_size, PLATFORM_VIRTUAL_ALLOC_RELEASE, PLATFORM_MEMORY_PROT_NO_ACCESS);
        }
    
        _arena_stack_check_invariants(stack);
        return error;
    }

    EXTERNAL ATTRIBUTE_INLINE_NEVER void* _arena_frame_handle_unusual_push(Arena_Stack* stack, Arena_Stack_Channel* channel, u8** max_alive_level, isize size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        _arena_stack_check_invariants(stack);
        
        u8* used_to = *channel->active_level;
    
        //fall
        if(max_alive_level < channel->active_level)
        {
            *max_alive_level = used_to;
            stack->fall_count += 1;
        }

        //rise
        if(max_alive_level > channel->active_level)
        {
            for(u8** next_level_ptr = channel->active_level + 1; next_level_ptr <= max_alive_level; next_level_ptr++)
                *next_level_ptr = used_to;
            stack->rise_count += 1;
        }

        u8* out = (u8*) align_forward(*max_alive_level, align);
        u8* new_used_to = out + size;
        isize commit = 0;
        if((u8*) new_used_to > channel->commit_to)
        {
            commit = DIV_CEIL(size, stack->commit_granularity)*stack->commit_granularity;
            ASSERT((size_t) channel->commit_to % platform_allocation_granularity() == 0);

            if(commit > stack->channel_reserved_size)
            {
                out = NULL;
                allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, size, NULL, 0, align, 
                    "More memory is needed then reserved! Reserved: %.2lf MB, commit: %.2lf MB", 
                    (double) (stack->channel_reserved_size)/MB, (double) (channel->commit_to - channel->data)/MB);
                goto end;
            }
            
            Platform_Error platform_error = platform_virtual_reallocate(NULL, channel->commit_to, commit, PLATFORM_VIRTUAL_ALLOC_COMMIT, PLATFORM_MEMORY_PROT_READ_WRITE);
            if(platform_error)
            {
                out = NULL;
                char buffer[4096];
                platform_translate_error(platform_error, buffer, sizeof buffer);
                allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, size, NULL, 0, align, 
                    "Virtual memory commit failed! Error: %s", buffer);
                goto end;
            }

            stack->commit_count += 1;
            channel->commit_to += commit;
        }

        channel->active_level = max_alive_level;
        *max_alive_level = new_used_to;

        _arena_stack_fill_garbage(stack, commit);
        _arena_stack_check_invariants(stack);

        end:
        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void* arena_frame_push_nonzero_error(Arena_Frame* frame, isize size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        ASSERT_PARAMS(frame->stack && frame->channel && 0 <= frame->level && frame->level <= frame->stack->max_alive_level, 
            "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");
        Arena_Stack_Channel* channel = frame->channel;
        _arena_stack_check_invariants(frame->stack);

        u8* out = (u8*) align_forward(*frame->next_level_ptr, align);
        if(channel->active_level != frame->next_level_ptr || out + size > channel->commit_to)
            out = (u8*) _arena_frame_handle_unusual_push(frame->stack, frame->channel, frame->next_level_ptr, size, align, error);
        else
        {
            *frame->next_level_ptr = out + size;
            _arena_stack_check_invariants(frame->stack);
        }

        PROFILE_STOP();
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
        PROFILE_START();
        ASSERT_PARAMS(stack->max_alive_level < stack->total_level_count, "Too many arena levels!");
        _arena_stack_check_invariants(stack);

        u32 level_i   = stack->max_alive_level / ARENA_STACK_CHANNELS;
        u32 channel_i = stack->max_alive_level % ARENA_STACK_CHANNELS;
        Arena_Stack_Channel* channel = &stack->channels[channel_i];
    
        //Here we could do a full for loop setting all levels affected by the 'rise' 
        // similar to the one in "_arena_frame_handle_unusual_push"
        // However, the situation that requires the full for loop is very unlikely. 
        // Thus we only do one next_level_ptr (which is the usual case) and let the rest be 
        // handled in "_arena_frame_handle_unusual_push".
        *(channel->active_level + 1) = *channel->active_level;
        channel->active_level += 1;

        Arena_Frame out = {0};
        out.next_level_ptr = channel->levels + level_i + 1;
        out.level = stack->max_alive_level;
        out.stack = stack;
        out.channel = channel;

        //Assign the arena frame. Fill the allocator part. 
        // For simple cases (acquire frame, push, release frame) this gets optimized away.
        out.alloc[0].func = arena_frame_allocator_func;
        out.alloc[0].get_stats = arena_frame_allocator_get_stats;

        stack->max_alive_level += 1;

        _arena_stack_check_invariants(stack);
        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS void arena_frame_release(Arena_Frame* frame)
    {
        PROFILE_START();
        ASSERT_PARAMS(frame->stack && 0 <= frame->level && frame->level <= frame->stack->max_alive_level, 
            "Using an invalid frame! Its not initialized or it was used after it or a parent frame was released!");

        Arena_Stack* stack = frame->stack;
        Arena_Stack_Channel* channel = frame->channel;
        _arena_stack_check_invariants(stack);
    
        u8* old_used_to = *channel->active_level;
        channel->active_level = MIN(channel->active_level, frame->next_level_ptr - 1); 
        stack->max_alive_level = frame->level;

        _arena_stack_fill_garbage(stack, old_used_to - *channel->active_level);
        _arena_stack_check_invariants(stack);
    
        //Set the frame to zero so that if someone tries to allocate from this frame
        // it will trigger an assert
        if(ARENA_STACK_DEBUG)
            memset(frame, 0, sizeof* frame);

        PROFILE_STOP();
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
        u8* start = *(frame->next_level_ptr - 1);
        stats.type_name = "Arena_Frame";
        stats.name = stack->name;
        stats.is_top_level = true;
        stats.is_capable_of_free_all = true;
        stats.fixed_memory_pool_size = stack->channel_reserved_size - (start - channel->data);
        stats.bytes_allocated = *frame->next_level_ptr - start;
        stats.max_bytes_allocated = *frame->next_level_ptr - start;
        return stats;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Stack* scratch_arena_stack()
    {
        static ATTRIBUTE_THREAD_LOCAL Arena_Stack _scratch_stack = {0};
        return &_scratch_stack;
    }

    EXTERNAL ATTRIBUTE_INLINE_ALWAYS Arena_Frame scratch_arena_frame_acquire()
    {
        ASSERT_PARAMS(scratch_arena_stack()->total_reserved_size > 0, "Must be already init!");
        return arena_frame_acquire(scratch_arena_stack());
    }

    #define ARENA_STACK_DEBUG_DATA_SIZE     32
    #define ARENA_STACK_DEBUG_DATA_PATTERN  0x55
    #define ARENA_STACK_DEBUG_STACK_PATTERN (u8*) 0x6666666666666666
    EXTERNAL void arena_stack_test_invariants(Arena_Stack* stack)
    {
        TEST(stack->commit_granularity >= 0);
        TEST(stack->channel_reserved_size >= 0);
        TEST(stack->channel_level_count >= 0);

        TEST(stack->total_reserved_size == stack->channel_reserved_size*ARENA_STACK_CHANNELS);
        TEST(stack->total_level_count == stack->channel_level_count*ARENA_STACK_CHANNELS);

        for(isize k = 0; k < ARENA_STACK_CHANNELS; k++)
        {
            Arena_Stack_Channel* channel = &stack->channels[k];

            u8** levels_end = channel->levels + stack->channel_level_count;

            u8* used_from = (u8*) levels_end;
            u8* used_to = channel->active_level ? *channel->active_level : NULL;
            u8* reserved_to = channel->data + stack->channel_reserved_size;

            TEST(channel->levels <= channel->active_level && channel->active_level <= levels_end);
            TEST(used_from <= used_to && used_to <= channel->commit_to && channel->commit_to <= reserved_to);

            for(u8** level = (u8**) channel->levels; level < channel->active_level; level++)
                TEST(used_from <= *level && *level <= used_to);
            
            if(ARENA_STACK_DEBUG)
            {
                for(u8** next_level_ptr = channel->active_level + 1; next_level_ptr < levels_end; next_level_ptr++)
                    TEST(*next_level_ptr == ARENA_STACK_DEBUG_STACK_PATTERN);
            
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

                //Fill stack
                u8** levels_end = channel->levels + stack->channel_level_count;
                for(u8** next_level_ptr = channel->active_level + 1; next_level_ptr < levels_end; next_level_ptr++)
                    *next_level_ptr = ARENA_STACK_DEBUG_STACK_PATTERN;

                //Fill content
                u8* used_to = *channel->active_level;
                isize till_end = channel->commit_to - used_to;
                isize check_size = CLAMP(content_size, 0, till_end);
                memset(used_to, ARENA_STACK_DEBUG_DATA_PATTERN, (size_t) check_size);
            }
    }
#endif