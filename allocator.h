#ifndef MODULE_ALLOCATOR
#define MODULE_ALLOCATOR

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef MODULE_ALL_COUPLED
    #include "defines.h"
    #include "assert.h"
    #include "profile.h"
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

typedef enum Allocator_Mode {
    ALLOCATOR_MODE_ALLOC = 0,
    ALLOCATOR_MODE_GET_STATS = 0,
} Allocator_Mode;

typedef enum Allocator_Error_Type {
    ALLOCATOR_ERROR_NONE = 0,
    ALLOCATOR_ERROR_OUT_OF_MEM = 1,
    ALLOCATOR_ERROR_INVALID_PARAMS = 2,
    ALLOCATOR_ERROR_UNSUPPORTED = 3,
} Allocator_Error_Type;

typedef struct Allocator_Error {
    Allocator* alloc;
    isize new_size; 
    void* old_ptr; 
    isize old_size; 
    isize align;

    Allocator_Error_Type error;
    char*   error_string;
    int64_t error_string_count;
    int64_t error_string_capacity;
} Allocator_Error;

typedef struct Allocator_Stats {
    Allocator* parent;      //The allocator used to obtain memory redistributed by this allocator.
    const char* type_name;  //human readable name of the type 
    const char* name;       //human readable name of this specific allocator
    bool is_top_level;      //if doesnt use any other allocator to obtain its memory (for example because uses VirtualAlloc)
    bool is_growing;        
    bool is_capable_of_resize;
    bool is_capable_of_free_all;

    bool _[4];
    isize fixed_memory_pool_size;

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

#define DEF_ALIGN  8
#define SIMD_ALIGN 32

#ifndef EXTERNAL
    #define EXTERNAL
#endif

#if defined(_MSC_VER)
    #define ATTRIBUTE_THREAD_LOCAL       __declspec(thread)
    #define ATTRIBUTE_RESTRICT_RETURN    __declspec(restrict)
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_THREAD_LOCAL       __thread
    #define ATTRIBUTE_RESTRICT_RETURN    __attribute__((malloc))
#else
    #define ATTRIBUTE_THREAD_LOCAL       _Thread_local
    #define ATTRIBUTE_RESTRICT_RETURN
#endif

ATTRIBUTE_RESTRICT_RETURN EXTERNAL void* allocator_try_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);
ATTRIBUTE_RESTRICT_RETURN EXTERNAL void* allocator_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align);
ATTRIBUTE_RESTRICT_RETURN EXTERNAL void* allocator_allocate(Allocator* alloc, isize new_size, isize align);
EXTERNAL void                            allocator_deallocate(Allocator* alloc, void* old_ptr,isize old_size, isize align);
EXTERNAL Allocator_Stats                 allocator_get_stats(Allocator* alloc);

#define REALLOCATE(alloc, new_count, old_ptr, old_count, T) (T*) allocator_reallocate((alloc), (new_count)*sizeof(T), (old_ptr), (old_count)*sizeof(T), __alignof(T))
#define ALLOCATE(alloc, new_count, T)                       (T*) allocator_allocate((alloc), (new_count)*sizeof(T), __alignof(T))
#define DEALLOCATE(alloc, old_ptr, old_count, T)                 allocator_deallocate((alloc), (old_ptr), (old_count)*sizeof(T), __alignof(T))

EXTERNAL void allocator_panic(Allocator_Error error);
EXTERNAL void allocator_error(Allocator_Error* error_or_null, Allocator_Error_Type error_type, Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align, const char* format, ...);

//All of these return the previously used Allocator_Set. This enables simple set/restore pair. 
typedef struct Allocator_Set {
    Allocator* allocator_default;
    Allocator* allocator_static;
} Allocator_Set;

EXTERNAL void* malloc_allocate(isize new_size, void* old_ptr, isize old_size, isize align);

EXTERNAL Allocator* allocator_get_malloc(); //returns the global malloc allocator. This allocator is initially set as default allocator.
EXTERNAL Allocator* allocator_get_static(); //returns the static allocator used for making allocations that last for the entire duration of this threads lifetime.
EXTERNAL Allocator* allocator_get_default(); //returns the default allocator. Modules should use this when no allocator was provided to them.

EXTERNAL Allocator_Set allocator_set_default(Allocator* new_default);
EXTERNAL Allocator_Set allocator_set_static(Allocator* new_default);
EXTERNAL Allocator_Set allocator_set(Allocator_Set backup); 

EXTERNAL bool  is_power_of_two(isize num);
EXTERNAL bool  is_power_of_two_or_zero(isize num);
EXTERNAL bool  is_aligned(void* ptr, isize align);
EXTERNAL void* align_forward(void* ptr, isize align_to);
EXTERNAL void* align_backward(void* ptr, isize align_to);

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_ALLOCATOR)) && !defined(MODULE_HAS_IMPL_ALLOCATOR)
#define MODULE_HAS_IMPL_ALLOCATOR

    #ifndef PROFILE_START
        #define PROFILE_START(...)
        #define PROFILE_STOP(...)
    #endif

    #ifndef REQUIRE
        #include <assert.h>
        #define REQUIRE(x, ...) assert(x)
    #endif
    
    #ifndef ASSERT
        #include <assert.h>
        #define ASSERT(x, ...) assert(x)
    #endif
    #include <stdarg.h>
    #include <stdio.h>

    EXTERNAL ATTRIBUTE_RESTRICT_RETURN 
    void* allocator_try_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        REQUIRE(alloc != NULL && new_size >= 0 && old_size >= 0 && is_power_of_two(align));
        void* out = (*alloc)(alloc, ALLOCATOR_MODE_ALLOC, new_size, old_ptr, old_size, align, error);
        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_RESTRICT_RETURN 
    void* allocator_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        return allocator_try_reallocate(alloc, new_size, old_ptr, old_size, align, NULL);
    }

    EXTERNAL ATTRIBUTE_RESTRICT_RETURN 
    void* allocator_allocate(Allocator* alloc, isize new_size, isize align)
    {
        return allocator_try_reallocate(alloc, new_size, NULL, 0, align, NULL);
    }

    EXTERNAL void allocator_deallocate(Allocator* alloc, void* old_ptr, isize old_size, isize align)
    {
        PROFILE_START();
        if(old_size > 0)
            (*alloc)(alloc, ALLOCATOR_MODE_ALLOC, 0, old_ptr, old_size, align, NULL);
        PROFILE_STOP();
    }

    EXTERNAL Allocator_Stats allocator_get_stats(Allocator* alloc)
    {
        Allocator_Stats stats = {0};
        if(alloc)
            (*alloc)(alloc, ALLOCATOR_MODE_GET_STATS, 0, 0, 0, 0, &stats);
        return stats;
    }

    EXTERNAL void allocator_error(Allocator_Error* error_or_null, Allocator_Error_Type error_type, Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align, const char* format, ...)
    {
        Allocator_Error error = {0};
        error.alloc = allocator;
        error.new_size = new_size;
        error.old_ptr = old_ptr;
        error.old_size = old_size;
        error.align = align;
        error.error = error_type;

        if(error.error_string) {
            va_list args;
            va_start(args, format);
            error.error_string_count = vsnprintf(error.error_string, error.error_string_capacity, format, args);
            va_end(args);
        }

        if(error_or_null == NULL)
            allocator_panic(error);
        else
            *error_or_null = error;
    }
    
    EXTERNAL bool is_power_of_two_or_zero(isize num) 
    {
        uint64_t n = (uint64_t) num;
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

        uintptr_t mask = ~((uintptr_t) align_to - 1);
        uintptr_t ptr_num = (uintptr_t) ptr;
        ptr_num = ptr_num & mask;

        return (void*) ptr_num;
    }
    
    EXTERNAL bool is_aligned(void* ptr, isize align)
    {
        return ptr == align_backward(ptr, align);
    }
    
    EXTERNAL void* malloc_allocate(isize new_size, void* old_ptr, isize old_size, isize align)
    {
        REQUIRE(new_size >= 0 && old_size >= 0 && align >= 0);

        void* new_ptr = NULL;
        PROFILE_START();
        {
            #if defined(_WIN32) || defined(_WIN64)
                __declspec(dllimport) void* __cdecl _aligned_realloc(void*  _Block, size_t _Size, size_t _Alignment);
                __declspec(dllimport) void __cdecl _aligned_free(void* _Block);
                if(new_size == 0)
                    _aligned_free(old_ptr);
                else 
                    new_ptr = _aligned_realloc(old_ptr, (size_t) new_size, (size_t) align);
            #else
                void *aligned_alloc(size_t alignment, size_t size );
                if(new_size == 0)
                    free(old_ptr);
                else if(align <= 16) 
                    new_ptr = realloc(old_ptr, (size_t) new_size, (size_t) align);
                else {
                    isize min_size = new_size < old_size ? new_size : old_size;
                    new_ptr = aligned_alloc((size_t) align, (size_t) new_size);
                    if(new_ptr != 0)
                    {
                        mempcy(new_ptr, old_ptr, min_size);
                        free(old_ptr);
                    }
                }
            #endif
        }
        PROFILE_STOP();
        return new_ptr;
    }

    static void* _malloc_allocator_func(void* alloc, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest)
    {
        if(mode == ALLOCATOR_MODE_ALLOC) {
            (void) alloc;
            void* new_ptr = malloc_allocate(new_size, old_ptr, old_size, align);
            if(new_ptr == NULL && new_size != 0)
                allocator_error((Allocator_Error*) rest, ALLOCATOR_ERROR_OUT_OF_MEM, (Allocator*) alloc, new_size, old_ptr, old_size, align, "malloc failed!");
            return new_ptr;
        } 
        if(mode == ALLOCATOR_MODE_GET_STATS) {
            Allocator_Stats stats = {0};
            stats.is_top_level = true;
            stats.is_growing = true;
            stats.is_capable_of_resize = true;
            stats.type_name = "malloc";
            *(Allocator_Stats*) rest = stats;
        }
        return NULL;
    }

    Allocator _malloc_alloc = _malloc_allocator_func;
    ATTRIBUTE_THREAD_LOCAL Allocator_Set g_allocators = {&_malloc_alloc, &_malloc_alloc};

    EXTERNAL Allocator* allocator_get_default() { return g_allocators.allocator_default; }
    EXTERNAL Allocator* allocator_get_static()  { return g_allocators.allocator_static; }
    EXTERNAL Allocator* allocator_get_malloc()  { return &_malloc_alloc; }

    EXTERNAL Allocator_Set allocator_set_default(Allocator* new_default)
    {
        Allocator_Set prev = g_allocators;
        g_allocators.allocator_default = new_default;
        return prev;
    }
    EXTERNAL Allocator_Set allocator_set_static(Allocator* new_static)
    {
        Allocator_Set prev = g_allocators;
        g_allocators.allocator_static = new_static;
        return prev;
    }
    EXTERNAL Allocator_Set allocator_set(Allocator_Set set_to)
    {
        Allocator_Set prev = {0};
        g_allocators = set_to;
        return prev;
    }
#endif