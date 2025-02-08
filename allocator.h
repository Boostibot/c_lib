#ifndef MODULE_ALLOCATOR
#define MODULE_ALLOCATOR

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
// We also keep two global allocator pointers. These are called 'default' and 'static' allocators. Each system requiring memory
// should use one of these two allocators for initialization (and then continue using that saved off pointer). 

#include "defines.h"
#include "assert.h"
#include "platform.h"
#include "profile.h"
#include "log.h"

typedef struct Allocator        Allocator;
typedef struct Allocator_Stats  Allocator_Stats;
typedef struct Allocator_Error  Allocator_Error;

//@TODO: Figure out a way to allow usage of allocator even without allocator.h
// Maybe this? -> request must somehow be able to hold error string buffer. 
// What to do about default alloc? What to do about reqeusts? Should there be requests?
// How to make the allocator function cheap to call?
//@TODO: remove the special handling of scratch allocators. They dont get run as often to make any sort of difference.
//@TODO: rewrite the motivation comment to be less cringe
#define ALLOC_FAILED_REQUEST                    ((void*) 1)
#define ALLOC_REQUEST_PARENT                    (1ull << 32)
#define ALLOC_REQUEST_NAME                      (2ull << 32)
#define ALLOC_REQUEST_TYPE_NAME                 (3ull << 32)
#define ALLOC_REQUEST_ALLOCATION_COUNT          (4ull << 32)
#define ALLOC_REQUEST_DEALLOCATION_COUNT        (5ull << 32)
#define ALLOC_REQUEST_REALLOCATION_COUNT        (6ull << 32)
#define ALLOC_REQUEST_MAX_ALIVE_ALLOCATIONS     (7ull << 32)
#define ALLOC_REQUEST_MAX_BYTES_ALLOCATED       (8ull << 32)
#define ALLOC_REQUEST_CAN_RESIZE                (9ull << 32)
#define ALLOC_REQUEST_CAN_FREE_ALL              (10ull << 32)
#define ALLOC_REQUEST_CAN_GROW                  (11ull << 32)
#define ALLOC_REQUEST_IS_TOP_LEVEL              (12ull << 32)
#define ALLOC_REQUEST_FIXED_MEMORY_SIZE         (13ull << 32)
typedef void* (*Allocator2)(void* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align_and_request, void* request);

typedef void* (*Allocator_Func)(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error_or_null);
typedef Allocator_Stats (*Allocator_Get_Stats)(Allocator* alloc);

typedef struct Allocator {
    Allocator_Func func;
    Allocator_Get_Stats get_stats;
} Allocator;

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
    char message[124];
} Allocator_Error;

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

typedef struct Allocator_Set {
    Allocator* allocator_default;

    bool set_default;
	bool _[7];
} Allocator_Set;

#define DEF_ALIGN PLATFORM_MAX_ALIGN
#define SIMD_ALIGN PLATFORM_SIMD_ALIGN

//Attempts to call the realloc funtion of the alloc. Can return nullptr indicating failiure
EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
void* allocator_try_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);

//Calls the realloc function of alloc. If fails calls the currently installed Allocator_Out_Of_Memory_Func (panics). This should be used most of the time
EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
void* allocator_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align);

//Calls the realloc function of alloc to allocate, if fails panics
EXTERNAL ATTRIBUTE_ALLOCATOR(2, 3) 
void* allocator_allocate(Allocator* alloc, isize new_size, isize align);

//Calls the realloc function of alloc to deallocate
EXTERNAL 
void allocator_deallocate(Allocator* alloc, void* old_ptr,isize old_size, isize align);

//Retrieves stats from the allocator. The stats can be only partially filled.
EXTERNAL Allocator_Stats allocator_get_stats(Allocator* alloc);

//Gets called when function requiring to always succeed fails an allocation - most often from allocator_reallocate
//If ALLOCATOR_CUSTOM_OUT_OF_MEMORY is defines is left unimplemented
EXTERNAL void allocator_panic(Allocator_Error error);

EXTERNAL void allocator_error(Allocator_Error* error_or_null, Allocator_Error_Type error_type, Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align, const char* format, ...);

EXTERNAL Allocator* allocator_get_default(); //returns the default allocator used for returning values from a function
EXTERNAL Allocator* allocator_or_default(Allocator* allocator_or_null); //Returns the passed in allocator_or_null. If allocator_or_null is NULL returns the current set default allocator
EXTERNAL Allocator* allocator_get_malloc(); //returns the global malloc allocator. This is the default allocator.

//All of these return the previously used Allocator_Set. This enables simple set/restore pair. 
EXTERNAL Allocator_Set allocator_set_default(Allocator* new_default);
EXTERNAL Allocator_Set allocator_set(Allocator_Set backup); 

EXTERNAL bool  is_power_of_two(isize num);
EXTERNAL bool  is_power_of_two_or_zero(isize num);
EXTERNAL void* align_forward(void* ptr, isize align_to);
EXTERNAL void* align_backward(void* ptr, isize align_to);
EXTERNAL void* stack_allocate(isize bytes, isize align_to) {(void) align_to; (void) bytes; return NULL;}

#ifdef _MSC_VER
    #define stack_allocate(size, align) \
        ((align) <= 8 ? _alloca((size_t) size) : align_forward(_alloca((size_t) ((size) + (align))), (align)))
#else
    #define stack_allocate(size, align) \
        __builtin_alloca_with_align((size_t) size, (size_t) align)
#endif

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_ALLOCATOR)) && !defined(MODULE_HAS_IMPL_ALLOCATOR)
#define MODULE_HAS_IMPL_ALLOCATOR

    EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
    void* allocator_try_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        REQUIRE(alloc != NULL);
        ASSERT(new_size >= 0 && old_size >= 0 && is_power_of_two(align));
        void* out = alloc->func(alloc, new_size, old_ptr, old_size, align, error);
        PROFILE_STOP();
        return out;
    }

    EXTERNAL ATTRIBUTE_ALLOCATOR(2, 5) 
    void* allocator_reallocate(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        return allocator_try_reallocate(alloc, new_size, old_ptr, old_size, align, NULL);
    }

    EXTERNAL ATTRIBUTE_ALLOCATOR(2, 3) 
    void* allocator_allocate(Allocator* alloc, isize new_size, isize align)
    {
        return allocator_try_reallocate(alloc, new_size, NULL, 0, align, NULL);
    }

    EXTERNAL void allocator_deallocate(Allocator* alloc, void* old_ptr, isize old_size, isize align)
    {
        PROFILE_START();
        if(old_size > 0)
            alloc->func(alloc, 0, old_ptr, old_size, align, NULL);
        PROFILE_STOP();
    }

    EXTERNAL Allocator_Stats allocator_get_stats(Allocator* alloc)
    {
        Allocator_Stats stats = {0};
        if(alloc && alloc->get_stats)
            stats = alloc->get_stats(alloc);

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
        va_list args;
        va_start(args, format);
        vsprintf(error.message, format, args);
        va_end(args);

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
    
    INTERNAL void* _malloc_allocator_func(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error_or_null)
    {
        void* out = NULL;
        PROFILE_SCOPE(malloc)
        {
            (void) alloc; (void) old_size; (void) align; (void) error_or_null;
            out = platform_heap_reallocate(new_size, old_ptr, align);
            if(out == NULL && new_size != 0)
                allocator_error(error_or_null, ALLOCATOR_ERROR_OUT_OF_MEM, alloc, new_size, old_ptr, old_size, align, "malloc failed!");
        }
        return out;
    }

    INTERNAL Allocator_Stats _malloc_allocator_get_stats(Allocator* alloc)
    {
        (void) alloc;
        Allocator_Stats stats = {0};
        stats.is_top_level = true;
        stats.is_growing = true;
        stats.is_capable_of_resize = true;
        stats.type_name = "malloc";
        return stats;
    }

    Allocator _malloc_alloc = {_malloc_allocator_func, _malloc_allocator_get_stats};
    EXTERNAL Allocator* allocator_get_malloc()
    {
        return &_malloc_alloc;
    }
    
    INTERNAL ATTRIBUTE_THREAD_LOCAL Allocator* g_default_allocator = {&_malloc_alloc};
    EXTERNAL Allocator* allocator_get_default()
    {
        return g_default_allocator;
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
    EXTERNAL Allocator_Set allocator_set(Allocator_Set set_to)
    {
        Allocator_Set prev = {0};
        if(set_to.set_default)
        {
            prev.allocator_default = g_default_allocator;
            prev.set_default = true;
            g_default_allocator = set_to.allocator_default;
        }

        return prev;
    }

    EXTERNAL Allocator_Stats log_allocator_stats(const char* log_name, Log_Type log_type, Allocator* allocator)
    {
        Allocator_Stats stats = {0};
        if(allocator != NULL && allocator->func != NULL)
        {
            stats = allocator_get_stats(allocator);
            if(stats.type_name == NULL)
                stats.type_name = "<no log_type name>";

            if(stats.name == NULL)
                stats.name = "<no name>";

            LOG(log_type, log_name, "type_name:           %s", stats.type_name);
            LOG(log_type, log_name, "name:                %s", stats.name);

            LOG(log_type, log_name, "bytes_allocated:     %s", format_bytes(stats.bytes_allocated).data);
            LOG(log_type, log_name, "max_bytes_allocated: %s", format_bytes(stats.max_bytes_allocated).data);

            LOG(log_type, log_name, "allocation_count:    %lli", stats.allocation_count);
            LOG(log_type, log_name, "deallocation_count:  %lli", stats.deallocation_count);
            LOG(log_type, log_name, "reallocation_count:  %lli", stats.reallocation_count);
        }
        else
            LOG(log_type, log_name, "Allocator NULL or missing get_stats callback.");

        return stats;
    }
    
    EXTERNAL void allocator_panic(Allocator_Error error)
    {
        Allocator_Stats stats = {0};
        if(error.alloc != NULL && error.alloc->func != NULL)
            stats = allocator_get_stats(error.alloc);

        if(stats.type_name == NULL)
            stats.type_name = "<no type name>";

        if(stats.name == NULL)
            stats.name = "<no name>";

        LOG_FATAL("memory", "Allocator %s of type %s reported out of memory! Message: '%s'", stats.type_name, stats.name, error.message);

        LOG_INFO(">memory", "new_size:    %s", format_bytes(error.new_size).data);
        LOG_INFO(">memory", "old_size:    %s", format_bytes(error.old_size).data);
        LOG_INFO(">memory", "old_ptr:     %s", format_ptr(error.old_ptr).data);
        LOG_INFO(">memory", "align:       %lli", (lli) error.align);

        LOG_INFO(">memory", "Allocator_Stats:");
        LOG_INFO(">>memory", "bytes_allocated:     %s", format_bytes(stats.bytes_allocated).data);
        LOG_INFO(">>memory", "max_bytes_allocated: %s", format_bytes(stats.max_bytes_allocated).data);

        LOG_INFO(">>memory", "allocation_count:    %lli", (lli) stats.allocation_count);
        LOG_INFO(">>memory", "deallocation_count:  %lli", (lli) stats.deallocation_count);
        LOG_INFO(">>memory", "reallocation_count:  %lli", (lli) stats.reallocation_count);

        PANIC("Allocation error");
    }
#endif