#ifndef JOT_ALLOCATOR
#define JOT_ALLOCATOR

#include "defines.h"
#include "assert.h"
#include "arena.h"
#include "profile.h"
#include <string.h>

// This module introduces a framework for dealing with memory and allocation used by every other system.
// It makes very little assumptions about the use case making it very portable to other projects.
//
// Since memory allocation is such a prevelent problem we try to maximize profiling and locality. 
// 
// We do this firstly by introducing a concept of Allocator. Allocators are structs that know how to allocate with the 
// advantage over malloc that they can be local and distinct for distinct tasks. This makes them faster and safer then malloc
// because we can localy see when something goes wrong. They can also be composed where allocators get their 
// memory from allocators above them (their 'parents'). This is especially useful for hierarchical resource management. 
// 
// By using a hierarchies we can guarantee that all memory will get freed by simply freeing the highest allocator. 
// This will work even if the lower allocators/systems leak, unlike malloc or other global alloctator systems where every 
// level has to be perfect.
// 
// that for example logs all allocations and checks their correctness.
//
// We also keep two global allocator pointers. These are called 'default' and 'scratch' allocators. Each system requiring memory
// should use one of these two allocators for initialization (and then continue using that saved off pointer). 
// By convention scratch allocator is used for "internal" allocation and default allocator is used for comunicating with the outside world.
// Given a function that does some useful compuatation and returns an allocated reuslt it typically uses a fast scratch allocator internally
// and only allocates using the default allocator the returned result.
//
// This convention ensures that all allocation is predictable and fast (scratch allocators are most often stack allocators that 
// are perfectly suited for fast allocation - deallocation pairs). This approach also stacks. In each function we can simply upon entry
// instal the scratch allocator as the default allocator so that all internal functions will also comunicate to us using the fast scratch 
// allocator.

typedef struct Allocator_Set {
    Allocator* allocator_default;
    Allocator* allocator_scratch;
    Allocator* allocator_static;
} Allocator_Set;

#define DEF_ALIGN PLATFORM_MAX_ALIGN
#define SIMD_ALIGN PLATFORM_SIMD_ALIGN

//Attempts to call the realloc funtion of the from_allocator. Can return nullptr indicating failiure
EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(4) 
void* allocator_try_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align);

//Calls the realloc function of from_allocator. If fails calls the currently installed Allocator_Out_Of_Memory_Func (panics). This should be used most of the time
EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(4) 
void* allocator_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align);

//Calls the realloc function of from_allocator to allocate, if fails panics
EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(2) 
void* allocator_allocate(Allocator* from_allocator, isize new_size, isize align);

//Calls the realloc function of from_allocator to deallocate
EXPORT 
void allocator_deallocate(Allocator* from_allocator, void* old_ptr,isize old_size, isize align);

//Calls the realloc function of from_allocator, then if reallocating up fills the added memory with zeros
EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(4) 
void* allocator_reallocate_cleared(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align);

//Calls the realloc function of from_allocator to allocate, then fills the memory with zeros if fails panics
EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(2) 
void* allocator_allocate_cleared(Allocator* from_allocator, isize new_size, isize align);

//Retrieves stats from the allocator. The stats can be only partially filled.
EXPORT Allocator_Stats allocator_get_stats(Allocator* self);

//Gets called when function requiring to always succeed fails an allocation - most often from allocator_reallocate
//If ALLOCATOR_CUSTOM_OUT_OF_MEMORY is defines is left unimplemented
EXPORT void allocator_out_of_memory(
    Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align, const char* format_string, ...);

EXPORT Allocator* allocator_get_default(); //returns the default allocator used for returning values from a function
EXPORT Allocator* allocator_get_scratch(); //returns the scracth allocator used for temp often stack order allocations inside a function
EXPORT Allocator* allocator_get_static(); //returns the static allocator used for allocations with potentially unbound lifetime. This includes things that will never be deallocated.
EXPORT Arena_Stack* allocator_get_scratch_arena_stack();
EXPORT Arena scratch_arena_acquire();
EXPORT bool allocator_is_arena(Allocator* allocator);

//@NOTE: static is useful for example for static dyanmic lookup tables, caches inside functions, quick hacks that will not be deallocated for whatever reason.

//All of these return the previously used Allocator_Set. This enables simple set/restore pair. 
EXPORT Allocator_Set allocator_set_default(Allocator* new_default);
EXPORT Allocator_Set allocator_set_scratch(Allocator* new_scratch);
EXPORT Allocator_Set allocator_set_static(Allocator* new_scratch);
EXPORT Allocator_Set allocator_set_both(Allocator* new_default, Allocator* new_scratch);
EXPORT Allocator_Set allocator_set(Allocator_Set backup); 

EXPORT bool  is_power_of_two(isize num);
EXPORT bool  is_power_of_two_or_zero(isize num);
EXPORT void* align_forward(void* ptr, isize align_to);
EXPORT void* align_backward(void* ptr, isize align_to);
EXPORT void* stack_allocate(isize bytes, isize align_to) {(void) align_to; (void) bytes; return NULL;}

#define CACHE_LINE  ((int64_t) 64)
#define PAGE_BYTES  ((int64_t) 4096)
#define KIBI_BYTE   ((int64_t) 1 << 10)
#define MEBI_BYTE   ((int64_t) 1 << 20)
#define GIBI_BYTE   ((int64_t) 1 << 30)
#define TEBI_BYTE   ((int64_t) 1 << 40)

//move to platform!
#ifdef _MSC_VER
    #define stack_allocate(size, align) \
        ((align) <= 8) ? _alloca((size_t) size) : align_forward(_alloca((size_t) ((size) + (align)), align)
#else
    #define stack_allocate(size, align) \
        __builtin_alloca_with_align((size_t) size, (size_t) align)
#endif


#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ALLOCATOR_IMPL)) && !defined(JOT_ALLOCATOR_HAS_IMPL)
#define JOT_ALLOCATOR_HAS_IMPL

    INTERNAL ATTRIBUTE_THREAD_LOCAL Allocator* _default_allocator = NULL;
    INTERNAL ATTRIBUTE_THREAD_LOCAL Allocator* _scratch_allocator = NULL;
    INTERNAL ATTRIBUTE_THREAD_LOCAL Allocator* _static_allocator = NULL;
    INTERNAL ATTRIBUTE_THREAD_LOCAL Arena_Stack _scratch_arena_stack = {0};

    EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(4) void* allocator_try_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        PERF_COUNTER_START(c);
        void* out = NULL;
        ASSERT(new_size >= 0 && old_size >= 0 && is_power_of_two(align) && "provided arguments must be valid!");
        
        //If is arena use the arena function directly (inlined)
        if(allocator_is_arena(from_allocator))
        {
            if(new_size > old_size)
            {
                Arena* arena = (Arena*) (void*) from_allocator;
                out = arena_push_nonzero_inline(arena, new_size, align);
                memcpy(out, old_ptr, old_size);
            }
        }
        else 
        {
            //if is dealloc and old_ptr is NULL do nothing. 
            //This is equivalent to free(NULL)
            if(new_size != 0 || old_ptr != NULL)
                out = from_allocator->allocate(from_allocator, new_size, old_ptr, old_size, align);
        }
            
        PERF_COUNTER_END(c);
        return out;
    }

    EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(4) void* allocator_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        void* obtained = allocator_try_reallocate(from_allocator, new_size, old_ptr, old_size, align);
        if(obtained == NULL && new_size != 0)
            allocator_out_of_memory(from_allocator, new_size, old_ptr, old_size, align, "");

        return obtained;
    }

    EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(2) void* allocator_allocate(Allocator* from_allocator, isize new_size, isize align)
    {
        return allocator_reallocate(from_allocator, new_size, NULL, 0, align);
    }

    EXPORT void allocator_deallocate(Allocator* from_allocator, void* old_ptr,isize old_size, isize align)
    {
        allocator_reallocate(from_allocator, 0, old_ptr, old_size, align);
    }
    
    EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(4) void* allocator_reallocate_cleared(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        void* ptr = allocator_reallocate(from_allocator, new_size, old_ptr, old_size, align);
        if(new_size > old_size)
            memset((u8*) ptr + old_size, 0, (size_t) (new_size - old_size));
        return ptr;
    }

    EXPORT ATTRIBUTE_RETURN_RESTRICT ATTRIBUTE_RETURN_ALIGNED_ARG(2) void* allocator_allocate_cleared(Allocator* from_allocator, isize new_size, isize align)
    {
        void* ptr = allocator_allocate(from_allocator, new_size, align);
        memset(ptr, 0, (size_t) new_size);
        return ptr;
    }

    EXPORT Allocator_Stats allocator_get_stats(Allocator* self)
    {
        return self->get_stats(self);
    }

    
    EXPORT bool allocator_is_arena(Allocator* allocator)
    {
        return allocator != NULL && allocator->allocate == arena_reallocate;
    }

    EXPORT Arena scratch_arena_acquire()
    {
        return arena_acquire(&_scratch_arena_stack);
    }

    EXPORT Allocator* allocator_get_default()
    {
        return _default_allocator;
    }
    EXPORT Allocator* allocator_get_scratch()
    {
        return _scratch_allocator;
    }
    EXPORT Allocator* allocator_get_static()
    {
        return _static_allocator;
    }
    EXPORT Arena_Stack* allocator_get_scratch_arena_stack()
    {
        return &_scratch_arena_stack;
    }

    EXPORT Allocator_Set allocator_set_default(Allocator* new_default)
    {
        return allocator_set_both(new_default, NULL);
    }
    EXPORT Allocator_Set allocator_set_scratch(Allocator* new_scratch)
    {
        return allocator_set_both(NULL, new_scratch);
    }
    EXPORT Allocator_Set allocator_set_static(Allocator* new_static)
    {
        Allocator_Set set_to = {0};
        set_to.allocator_static = new_static;
        return allocator_set(set_to);
    }

    EXPORT Allocator_Set allocator_set_both(Allocator* new_default, Allocator* new_scratch)
    {
        Allocator_Set set_to = {new_default, new_scratch};
        return allocator_set(set_to);
    }

    EXPORT Allocator_Set allocator_set(Allocator_Set set_to)
    {
        Allocator_Set prev = {0};
        if(set_to.allocator_default != NULL)
        {
            prev.allocator_default = _default_allocator;
            _default_allocator = set_to.allocator_default;
        }

        if(set_to.allocator_scratch != NULL)
        {
            prev.allocator_scratch = _scratch_allocator;
            _scratch_allocator = set_to.allocator_scratch;
        }
        
        if(set_to.allocator_static != NULL)
        {
            prev.allocator_static = _static_allocator;
            _static_allocator = set_to.allocator_static;
        }

        return prev;
    }
    
    EXPORT bool is_power_of_two_or_zero(isize num) 
    {
        usize n = (usize) num;
        return ((n & (n-1)) == 0);
    }

    EXPORT bool is_power_of_two(isize num) 
    {
        return (num>0 && is_power_of_two_or_zero(num));
    }

    EXPORT void* align_forward(void* ptr, isize align_to)
    {
        ASSERT(is_power_of_two(align_to));

        //this is a little criptic but according to the iternet should be the fastest way of doing this
        // my benchmarks support this. 
        //(its about 50% faster than using div_round_up would be - even if we supply log2 alignment and bitshifts)
        isize mask = align_to - 1;
        isize ptr_num = (isize) ptr;
        ptr_num += (-ptr_num) & mask;

        return (void*) ptr_num;
    }

    EXPORT void* align_backward(void* ptr, isize align_to)
    {
        ASSERT(is_power_of_two(align_to));

        usize mask = ~((usize) align_to - 1);
        usize ptr_num = (usize) ptr;
        ptr_num = ptr_num & mask;

        return (void*) ptr_num;
    }
    
#endif