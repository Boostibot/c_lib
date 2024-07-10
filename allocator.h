#ifndef JOT_ALLOCATOR
#define JOT_ALLOCATOR

// This module introduces a framework for dealing with memory and allocation used by every other system.
// It makes very little assumptions about the use case making it very portable to other projects.
//
// Since memory allocation is such a prevalent problem we try to maximize profiling and locality. 
// 
// We do this firstly by introducing a concept of Allocator. Allocators are structs that know how to allocate with the 
// advantage over malloc that they can be local and distinct for distinct tasks. This makes them faster and safer then malloc
// because we can locally see when something goes wrong. They can also be composed where allocators get their 
// memory from allocators above them (their 'parents'). This is especially useful for hierarchical resource management. 
// 
// By using a hierarchies we can guarantee that all memory will get freed by simply freeing the highest allocator. 
// This will work even if the lower allocators/systems leak, unlike malloc or other global allocator systems where every 
// level has to be perfect.
// 
// that for example logs all allocations and checks their correctness.
//
// We also keep two global allocator pointers. These are called 'default' and 'scratch' allocators. Each system requiring memory
// should use one of these two allocators for initialization (and then continue using that saved off pointer). 
// By convention scratch allocator is used for "internal" allocation and default allocator is used for communicating with the outside world.
// Given a function that does some useful computation and returns an allocated result it typically uses a fast scratch allocator internally
// and only allocates using the default allocator the returned result.
//
// This convention ensures that all allocation is predictable and fast (scratch allocators are most often stack allocators that 
// are perfectly suited for fast allocation - deallocation pairs). This approach also stacks. In each function we can simply upon entry
// install the scratch allocator as the default allocator so that all internal functions will also communicate to us using the fast scratch 
// allocator.

// @TODO: Get rid of default and scratch allocator since we need explicitness! If a function needs a general allocator for its internal use thats a scratch allocation and
// it should ask for it or create it right there. If a function wants to allocate something for the caller, the caller should really know where to find it (thus they should pass it as an argument).

#include "defines.h"
#include "assert.h"
#include "platform.h"
#include "profile_defs.h"

typedef struct Allocator        Allocator;
typedef struct Allocator_Stats  Allocator_Stats;

typedef void* (*Allocator_Allocate_Func)(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
typedef Allocator_Stats (*Allocator_Get_Stats_Func)(Allocator* self);

typedef struct Allocator {
    Allocator_Allocate_Func allocate;
    Allocator_Get_Stats_Func get_stats;
} Allocator;

typedef struct Allocator_Stats {
    //The allocator used to obtain memory redistributed by this allocator.
    //If is_top_level is set this should probably be NULL
    Allocator* parent;
    //Human readable name of the type 
    const char* type_name;
    //Optional human readable name of this specific allocator
    const char* name;
    //if doesnt use any other allocator to obtain its memory. For example malloc allocator or VM memory allocator have this set.
    bool is_top_level; 
	bool _padding[7];

    //The number of bytes given out to the program by this allocator. (does NOT include book keeping bytes).
    //Might not be totally accurate but is required to be locally stable - if we allocate 100B and then deallocate 100B this should not change.
    //This can be used to accurately track memory leaks. (Note that if this field is simply not set and thus is 0 the above property is satisfied)
    isize bytes_allocated;
    isize max_bytes_allocated;  //maximum bytes_allocated during the entire lifetime of the allocator

    isize max_concurent_allocations;
    isize allocation_count;     //The number of allocation requests (old_ptr == NULL). Does not include reallocs!
    isize deallocation_count;   //The number of deallocation requests (new_size == 0). Does not include reallocs!
    isize reallocation_count;   //The number of reallocation requests (*else*).
} Allocator_Stats;

typedef struct Allocator_Set {
    Allocator* allocator_default;
    Allocator* allocator_scratch;
    Allocator* allocator_static;

    bool set_default;
    bool set_scratch;
    bool set_static;
    
	bool _padding[5];
} Allocator_Set;

#define DEF_ALIGN PLATFORM_MAX_ALIGN
#define SIMD_ALIGN PLATFORM_SIMD_ALIGN

//Attempts to call the realloc funtion of the from_allocator. Can return nullptr indicating failiure
EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
void* allocator_try_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align);

//Calls the realloc function of from_allocator. If fails calls the currently installed Allocator_Out_Of_Memory_Func (panics). This should be used most of the time
EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
void* allocator_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align);

//Calls the realloc function of from_allocator to allocate, if fails panics
EXTERNAL ATTRIBUTE_ALLOCATOR(2, 3) 
void* allocator_allocate(Allocator* from_allocator, isize new_size, isize align);

//Calls the realloc function of from_allocator to deallocate
EXTERNAL 
void allocator_deallocate(Allocator* from_allocator, void* old_ptr,isize old_size, isize align);

//Retrieves stats from the allocator. The stats can be only partially filled.
EXTERNAL Allocator_Stats allocator_get_stats(Allocator* self);

//Gets called when function requiring to always succeed fails an allocation - most often from allocator_reallocate
//If ALLOCATOR_CUSTOM_OUT_OF_MEMORY is defines is left unimplemented
EXTERNAL void allocator_out_of_memory(Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align);

EXTERNAL Allocator* allocator_get_default(); //returns the default allocator used for returning values from a function
EXTERNAL Allocator* allocator_get_scratch(); //returns the scratch allocator used for temp often stack order allocations inside a function
EXTERNAL Allocator* allocator_get_static(); //returns the static allocator used for allocations with potentially unbound lifetime. This includes things that will never be deallocated.
EXTERNAL Allocator* allocator_or_default(Allocator* allocator_or_null); //Returns the passed in allocator_or_null. If allocator_or_null is NULL returns the current set default allocator

EXTERNAL bool allocator_is_arena(Allocator* allocator);

//@NOTE: static is useful for example for static dynamic lookup tables, caches inside functions, quick hacks that will not be deallocated for whatever reason.

//All of these return the previously used Allocator_Set. This enables simple set/restore pair. 
EXTERNAL Allocator_Set allocator_set_default(Allocator* new_default);
EXTERNAL Allocator_Set allocator_set_scratch(Allocator* new_scratch);
EXTERNAL Allocator_Set allocator_set_static(Allocator* new_scratch);
EXTERNAL Allocator_Set allocator_set_both(Allocator* new_default, Allocator* new_scratch);
EXTERNAL Allocator_Set allocator_set(Allocator_Set backup); 

EXTERNAL bool  is_power_of_two(isize num);
EXTERNAL bool  is_power_of_two_or_zero(isize num);
EXTERNAL void* align_forward(void* ptr, isize align_to);
EXTERNAL void* align_backward(void* ptr, isize align_to);
EXTERNAL void* stack_allocate(isize bytes, isize align_to) {(void) align_to; (void) bytes; return NULL;}

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

    typedef struct Global_Allocator_State {
        Allocator* default_allocator;
        Allocator* scratch_allocator;
        Allocator* static_allocator;
    } Global_Allocator_State;

    void* arena_frame_reallocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);

    INTERNAL ATTRIBUTE_THREAD_LOCAL Global_Allocator_State _allocator_state = {0};
    EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
    void* allocator_try_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        PROFILE_START();
        void* out = NULL;
        ASSERT(new_size >= 0 && old_size >= 0 && is_power_of_two(align) && "provided arguments must be valid!");
        
        //If is arena use the arena function directly (inlined)
        if(allocator_is_arena(from_allocator))
            out = arena_frame_reallocate(from_allocator, new_size, old_ptr, old_size, align);
        else 
        {
            //if is dealloc and old_ptr is NULL do nothing. 
            //This is equivalent to free(NULL)
            if(new_size != 0 || old_ptr != NULL)
                out = from_allocator->allocate(from_allocator, new_size, old_ptr, old_size, align);
        }
            
        PROFILE_END();
        return out;
    }

    EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
    void* allocator_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        void* obtained = allocator_try_reallocate(from_allocator, new_size, old_ptr, old_size, align);
        if(obtained == NULL && new_size != 0)
            allocator_out_of_memory(from_allocator, new_size, old_ptr, old_size, align);

        return obtained;
    }

    EXTERNAL ATTRIBUTE_ALLOCATOR(2, 3) 
    void* allocator_allocate(Allocator* from_allocator, isize new_size, isize align)
    {
        return allocator_reallocate(from_allocator, new_size, NULL, 0, align);
    }

    EXTERNAL void allocator_deallocate(Allocator* from_allocator, void* old_ptr,isize old_size, isize align)
    {
        allocator_reallocate(from_allocator, 0, old_ptr, old_size, align);
    }

    EXTERNAL Allocator_Stats allocator_get_stats(Allocator* self)
    {
        Allocator_Stats out = {0};
        if(self && self->get_stats)
            out = self->get_stats(self);

        return out;
    }
    
    EXTERNAL bool allocator_is_arena(Allocator* allocator)
    {
        return allocator != NULL && allocator->allocate == arena_frame_reallocate;
    }
    
    EXTERNAL Allocator* allocator_get_default()
    {
        return _allocator_state.default_allocator;
    }
    EXTERNAL Allocator* allocator_get_scratch()
    {
        return _allocator_state.scratch_allocator;
    }
    EXTERNAL Allocator* allocator_get_static()
    {
        return _allocator_state.static_allocator;
    }
    
    EXTERNAL Allocator* allocator_or_default(Allocator* allocator_or_null)
    {
        return allocator_or_null ? allocator_or_null : allocator_get_default();
    }

    EXTERNAL Allocator_Set allocator_set_default(Allocator* new_default)
    {
        Allocator_Set set_to = {0};
        set_to.allocator_default = new_default;
        set_to.set_default = true;
        return allocator_set(set_to);
    }

    EXTERNAL Allocator_Set allocator_set_scratch(Allocator* new_scratch)
    {
        Allocator_Set set_to = {0};
        set_to.allocator_scratch = new_scratch;
        set_to.set_scratch = true;
        return allocator_set(set_to);
    }

    EXTERNAL Allocator_Set allocator_set_static(Allocator* new_static)
    {
        Allocator_Set set_to = {0};
        set_to.allocator_static = new_static;
        set_to.set_static = true;
        return allocator_set(set_to);
    }

    EXTERNAL Allocator_Set allocator_set_both(Allocator* new_default, Allocator* new_scratch)
    {
        Allocator_Set set_to = {new_default, new_scratch};
        set_to.set_default = true;
        set_to.set_scratch = true;
        return allocator_set(set_to);
    }

    EXTERNAL Allocator_Set allocator_set(Allocator_Set set_to)
    {
        Global_Allocator_State* state = &_allocator_state;

        Allocator_Set prev = {0};
        if(set_to.set_default)
        {
            prev.allocator_default = state->default_allocator;
            prev.set_default = true;
            state->default_allocator = set_to.allocator_default;
        }

        if(set_to.set_scratch)
        {
            prev.allocator_scratch = state->scratch_allocator;
            prev.set_scratch = true;
            state->scratch_allocator = set_to.allocator_scratch;
        }
        
        if(set_to.set_static)
        {
            prev.allocator_static = state->static_allocator;
            prev.set_static = true;
            state->static_allocator = set_to.allocator_static;
        }

        return prev;
    }
    
    EXTERNAL bool is_power_of_two_or_zero(isize num) 
    {
        usize n = (usize) num;
        return ((n & (n-1)) == 0);
    }

    EXTERNAL bool is_power_of_two(isize num) 
    {
        return (num>0 && is_power_of_two_or_zero(num));
    }

    EXTERNAL void* align_forward(void* ptr, isize align_to)
    {
        ASSERT(is_power_of_two(align_to));

        //this is a little cryptic but according to the internet should be the fastest way of doing this
        // my benchmarks support this. 
        //(its about 50% faster than using div_round_up would be - even if we supply log2 alignment and bitshifts)
        isize mask = align_to - 1;
        isize ptr_num = (isize) ptr;
        ptr_num += (-ptr_num) & mask;

        return (void*) ptr_num;
    }

    EXTERNAL void* align_backward(void* ptr, isize align_to)
    {
        ASSERT(is_power_of_two(align_to));

        usize mask = ~((usize) align_to - 1);
        usize ptr_num = (usize) ptr;
        ptr_num = ptr_num & mask;

        return (void*) ptr_num;
    }
    
#endif