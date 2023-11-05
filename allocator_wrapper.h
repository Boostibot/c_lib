#ifndef LIB_ALLOCATOR_WRAPPER
#define LIB_ALLOCATOR_WRAPPER

#include "allocator.h"
#include "profile.h"

// A small stateless allocator that provides compatibility between the C style
// effectively stateless malloc/realloc allocation startegies and our explicit allocator strategy.
//
// The compatibility is both ways and fairly efficient. 
// Meaning that: 
//  1: Blocks allocated with wrapper_allocator_malloc can be deallocated using 
//  (any instance of) the Wrapper_Allocator. 
// 
//  2: Blocks allocated with (any instance of) the Wrapper_Allocator can be deallocated
//  using wrapper_allocator_free.
//
// This type of interface is sometimes needed when comunicating with the outside C world
// as is the case when using stb_image.h for example.

typedef enum Wrapper_Allocator_Use_Allocator
{
    WRAPPER_ALLOCATOR_USE_ALLOCATOR_PARENT,
    WRAPPER_ALLOCATOR_USE_ALLOCATOR_DEFAULT,
    WRAPPER_ALLOCATOR_USE_ALLOCATOR_SCRATCH,
    WRAPPER_ALLOCATOR_USE_ALLOCATOR_STATIC,
} Wrapper_Allocator_Use_Allocator;

typedef struct Wrapper_Allocator
{
    Allocator allocator;
    Allocator* parent;
    Wrapper_Allocator_Use_Allocator use_allocator;
} Wrapper_Allocator;

EXPORT void wrapper_allocator_init(Wrapper_Allocator* allocator, Allocator* parent_allocator, Wrapper_Allocator_Use_Allocator use_allocator);
EXPORT void wrapper_allocator_deinit(Wrapper_Allocator* allocator);
EXPORT void* wrapper_allocator_allocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from);
EXPORT Allocator_Stats wrapper_allocator_get_stats(Allocator* self);

//These functions are the stateless interface. Even though their signatures dont exactly match their C counterparts they can be used in that way with 
// the folowing: align = DEF_ALIGN, called_from = SOURCE_INFO(), using_allocator = wrapper_allocator_get_default()
EXPORT void* wrapper_allocator_malloc(Allocator* using_allocator, isize new_size, isize align, Source_Info called_from);
EXPORT void* wrapper_allocator_realloc(Allocator* using_allocator, void* ptr, isize new_size, isize align, Source_Info called_from);
EXPORT void wrapper_allocator_free(void* ptr, Source_Info called_from);

//Since Wrapper_Allocator is stateless we can just have global allocators that take on the form of one of the default allocators.
EXPORT Allocator* wrapper_allocator_get_default();
EXPORT Allocator* wrapper_allocator_get_scratch();
EXPORT Allocator* wrapper_allocator_get_static();

#endif
#define LIB_ALL_IMPL
#if (defined(LIB_ALL_IMPL) || defined(LIB_ALLOCATOR_WRAPPER_IMPL)) && !defined(LIB_ALLOCATOR_WRAPPER_HAS_IMPL)
#define LIB_ALLOCATOR_WRAPPER_HAS_IMPL

typedef struct Wrapper_Allocator_Block
{
    Allocator* allocated_from;
    u32 size;
    u32 align;
} Wrapper_Allocator_Block; 

typedef enum _Wrapper_Alloc_Arguments
{
    WRAPPER_ALLOC_USE_PROVIDED_ARGUMENTS,
    WRAPPER_ALLOC_USE_FOUND_ARGUMENTS,
} _Wrapper_Alloc_Arguments;

#define WRAPPER_ALLOCAROR_DEF_ALIGN MAX(DEF_ALIGN, sizeof(Wrapper_Allocator_Block))


INTERNAL void* wrapper_allocator_allocate_custom(Allocator* using_allocator, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from, _Wrapper_Alloc_Arguments use_provided_arguments)
{
    PERF_COUNTER_START(c);
    isize actual_new_size = 0;
    isize actual_old_size = 0;
    isize actual_align = align;
    void* actual_old_ptr = NULL;

    if(old_ptr != NULL)
    {
        Wrapper_Allocator_Block* old_block_ptr = (Wrapper_Allocator_Block*) old_ptr - 1;
        ASSERT_SLOW(old_block_ptr->size > 0);
        ASSERT_SLOW(old_block_ptr->allocated_from != NULL);
        ASSERT_SLOW(is_power_of_two(old_block_ptr->align));

        if(use_provided_arguments == WRAPPER_ALLOC_USE_PROVIDED_ARGUMENTS)
        {
            ASSERT((isize) old_block_ptr->size == old_size && "wrapper_allocator_allocate_custom() submitted invalid size!");
            ASSERT((isize) old_block_ptr->align == align && "wrapper_allocator_allocate_custom() submitted invalid align!");
        }
        else
        {
            align = old_block_ptr->align;
            old_size = old_block_ptr->size;
        }

        using_allocator = old_block_ptr->allocated_from;
        actual_align = MAX(align, sizeof(Wrapper_Allocator_Block));
        actual_old_ptr = (u8*) old_ptr - actual_align;

        ASSERT_SLOW(using_allocator->allocate != NULL && using_allocator->get_stats != NULL);
    }
    else
    {
        actual_align = MAX(align, sizeof(Wrapper_Allocator_Block));
    }

    if(new_size != 0)
        actual_new_size = new_size + actual_align;
    
    if(old_size != 0)
        actual_old_size = old_size + actual_align;

    void* out_ptr = NULL;
    void* actual_new_ptr = allocator_try_reallocate(using_allocator, actual_new_size, actual_old_ptr, actual_old_size, actual_align, called_from);

    //void* actual_new_ptr = using_allocator->allocate(using_allocator, actual_new_size, actual_old_ptr, actual_old_size, actual_align, called_from);
    if(actual_new_size != 0 && actual_new_ptr != NULL)
    {
        out_ptr = (u8*) actual_new_ptr + actual_align;
        Wrapper_Allocator_Block* new_block_ptr = (Wrapper_Allocator_Block*) out_ptr - 1;
        new_block_ptr->allocated_from = using_allocator;
        new_block_ptr->size = (u32) new_size;
        new_block_ptr->align = (u32) align;
        ASSERT(align != 0);
        ASSERT(using_allocator != NULL);
    }

    PERF_COUNTER_END(c);
    return out_ptr;
}

EXPORT void* wrapper_allocator_allocate(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from)
{
    Wrapper_Allocator* self = (Wrapper_Allocator*) (void*) self_;
    Allocator* using_allocator = NULL;
    switch(self->use_allocator)
    {
        case WRAPPER_ALLOCATOR_USE_ALLOCATOR_DEFAULT:
            using_allocator = allocator_get_default();
        break;

        case WRAPPER_ALLOCATOR_USE_ALLOCATOR_SCRATCH:
            using_allocator = allocator_get_scratch();
        break;

        case WRAPPER_ALLOCATOR_USE_ALLOCATOR_STATIC:
            using_allocator = allocator_get_static();
        break;

        default:
        case WRAPPER_ALLOCATOR_USE_ALLOCATOR_PARENT:
            if(self->parent == NULL)
                using_allocator = allocator_get_default();
        break;
    }

    ASSERT(using_allocator != NULL && using_allocator != self_ && "wrapper allocator must not be used as default when use mode is not WRAPPER_ALLOCATOR_USE_ALLOCATOR_PARENT!");
    return wrapper_allocator_allocate_custom(using_allocator, new_size, old_ptr, old_size, align, called_from, WRAPPER_ALLOC_USE_FOUND_ARGUMENTS);
}

EXPORT void* wrapper_allocator_malloc(Allocator* using_allocator, isize new_size, isize align, Source_Info called_from)
{
    void* out_ptr = NULL;
    if(new_size != 0)
        out_ptr = wrapper_allocator_allocate_custom(using_allocator, new_size, NULL, 0, align, called_from, WRAPPER_ALLOC_USE_FOUND_ARGUMENTS);
    return out_ptr;
}

EXPORT void* wrapper_allocator_realloc(Allocator* using_allocator, void* ptr, isize new_size, isize align, Source_Info called_from)
{
    void* out_ptr = NULL;
    if(ptr != NULL || new_size != 0)
        out_ptr = wrapper_allocator_allocate_custom(using_allocator, new_size, ptr, 0, align, called_from, WRAPPER_ALLOC_USE_FOUND_ARGUMENTS);

    return out_ptr;
}

EXPORT void wrapper_allocator_free(void* ptr, Source_Info called_from)
{
    if(ptr != NULL)
        wrapper_allocator_allocate_custom(NULL, 0, ptr, 0, 0, called_from, WRAPPER_ALLOC_USE_FOUND_ARGUMENTS);
}

EXPORT Allocator_Stats wrapper_allocator_get_stats(Allocator* self_)
{
    Wrapper_Allocator* self = (Wrapper_Allocator*) (void*) self_;
    Allocator_Stats out = {0};
    out.parent = self->parent;
    out.type_name = "Wrapper_Allocator";
    return out;
}

EXPORT void wrapper_allocator_init(Wrapper_Allocator* allocator, Allocator* parent_allocator, Wrapper_Allocator_Use_Allocator use_allocator)
{
    wrapper_allocator_deinit(allocator);
    allocator->allocator.allocate = wrapper_allocator_allocate;
    allocator->allocator.get_stats = wrapper_allocator_get_stats;
    allocator->parent = parent_allocator;
    allocator->use_allocator = use_allocator;
}
EXPORT void wrapper_allocator_deinit(Wrapper_Allocator* allocator)
{
    memset(allocator, 0, sizeof *allocator);
}

Wrapper_Allocator global_wrapper_alloc_default = {wrapper_allocator_allocate, wrapper_allocator_get_stats, NULL, WRAPPER_ALLOCATOR_USE_ALLOCATOR_DEFAULT};
Wrapper_Allocator global_wrapper_alloc_scratch = {wrapper_allocator_allocate, wrapper_allocator_get_stats, NULL, WRAPPER_ALLOCATOR_USE_ALLOCATOR_SCRATCH};
Wrapper_Allocator global_wrapper_alloc_static = {wrapper_allocator_allocate, wrapper_allocator_get_stats, NULL, WRAPPER_ALLOCATOR_USE_ALLOCATOR_STATIC};

EXPORT Allocator* wrapper_allocator_get_default()
{
    return &global_wrapper_alloc_default.allocator;
}
EXPORT Allocator* wrapper_allocator_get_scratch()
{
    return &global_wrapper_alloc_scratch.allocator;
}
EXPORT Allocator* wrapper_allocator_get_static()
{
    return &global_wrapper_alloc_static.allocator;
}

#endif