#ifndef JOT_ALLOCATOR_FAILING
#define JOT_ALLOCATOR_FAILING

//An implementation of the allocator concept that panics on every allocation/deallocation. 
//This is useful for asserting that a piece of code doesnt allocate.

#include "allocator.h"

typedef void* (*Failing_Allocator_Panic)(Allocator* allocator, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from, void* context);

typedef struct Failing_Allocator
{
    Allocator allocator;
    Failing_Allocator_Panic panic_func;
    void* panic_context;
} Failing_Allocator;

EXPORT void failling_allocator_init(Failing_Allocator* self, Failing_Allocator_Panic panic_func_or_null, void* panic_context);
EXPORT void failling_allocator_deinit(Failing_Allocator* self);

EXPORT void* failling_allocator_allocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from);
EXPORT Allocator_Stats failling_allocator_get_stats(Allocator* self);
EXPORT Allocator* allocator_get_failing();

#endif

#define JOT_ALL_IMPL

#if (defined(JOT_ALL_IMPL) || defined(JOT_ALLOCATOR_FAILING_IMPL)) && !defined(JOT_ALLOCATOR_FAILING_HAS_IMPL)
#define JOT_ALLOCATOR_FAILING_HAS_IMPL

EXPORT void failling_allocator_init(Failing_Allocator* self, Failing_Allocator_Panic panic_func_or_null, void* panic_context)
{
    self->allocator.allocate = failling_allocator_allocate;
    self->allocator.get_stats = failling_allocator_get_stats;
    self->panic_context = panic_context;
    self->panic_func = panic_func_or_null;
}

EXPORT void failling_allocator_deinit(Failing_Allocator* self)
{
    (void) self;
}

EXPORT void* failling_allocator_allocate(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from)
{
    Failing_Allocator* self = (Failing_Allocator*) (void*) self_; 
    if(self->panic_func)
        return self->panic_func(self_, new_size, old_ptr, old_size, align, called_from, self->panic_context);

    return NULL;
}

EXPORT Allocator_Stats failling_allocator_get_stats(Allocator* self)
{
    (void) self;
    Allocator_Stats stats = {0};
    stats.type_name = "Failing_Allocator";
    stats.is_top_level = true;
    return stats;
}

EXPORT Allocator* allocator_get_failing()
{
    static Failing_Allocator alloc = {failling_allocator_allocate, failling_allocator_get_stats};
    return &alloc.allocator;
}

#endif
