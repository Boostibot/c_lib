#ifndef JOT_ALLOCATOR_MALLOC
#define JOT_ALLOCATOR_MALLOC

// A simple implementation of the allocator concept using default C malloc.
// The main purpose of this file is to be a quick substitute until more complex allocators are built.
// 
// Works by prepending each allocation with a header that tracks its attributes. This also enables us
// to traverse all active allocations. Offers basic corectness checking.
// 
// Provides compatibility between the allocator approach and the C style approach (using malloc/realloc/free)
// which is useful when talking to APIs that dont pass sizes/alignments into their allocation callbacks (such as glfw, stb image)

#include "allocator.h"

typedef struct Malloc_Allocator_Block_Header Malloc_Allocator_Block_Header;

typedef struct Malloc_Allocator
{
    Allocator allocator;
    const char* name;

    Allocator* parent; //parent allocator. If parent is null uses malloc/free

    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    Malloc_Allocator_Block_Header* first_block;
    Malloc_Allocator_Block_Header* last_block;

    Allocator_Set allocator_backup;
} Malloc_Allocator;

typedef struct Malloc_Allocator_Block_Header {
    Malloc_Allocator_Block_Header* next_block;
    Malloc_Allocator_Block_Header* prev_block;

    i32 size;
    i32 align;
    i32 heap_block_offset; //offset to the start of the heap allocated block
    i32 magic_number;
} Malloc_Allocator_Block_Header;

#define MALLOC_ALLOCATOR_MAGIC_NUMBER (i32) 0x55555555

EXPORT void malloc_allocator_init(Malloc_Allocator* self);
EXPORT void malloc_allocator_deinit(Malloc_Allocator* self);
//convenience function that inits the allocator then imidietely makes it the default and scratch. On deinit restores to previous defaults
EXPORT void malloc_allocator_init_use(Malloc_Allocator* self, u64 flags); 

EXPORT void* malloc_allocator_allocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from);
EXPORT Allocator_Stats malloc_allocator_get_stats(Allocator* self);

EXPORT isize malloc_allocator_get_block_size(Malloc_Allocator* self, void* old_ptr);
EXPORT Malloc_Allocator_Block_Header* malloc_allocator_get_block_header(Malloc_Allocator* self, void* old_ptr);

EXPORT void* malloc_allocator_malloc(Malloc_Allocator* self, isize size);
EXPORT void* malloc_allocator_realloc(Malloc_Allocator* self, void* old_ptr, isize new_size);
EXPORT void  malloc_allocator_free(Malloc_Allocator* self, void* old_ptr);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ALLOCATOR_MALLOC_IMPL)) && !defined(JOT_ALLOCATOR_MALLOC_HAS_IMPL)
#define JOT_ALLOCATOR_MALLOC_HAS_IMPL


    //the way this file is written this can simple be changed to malloc just by defining MALLOC_ALLOCATOR_NAKED
    #ifdef MALLOC_ALLOCATOR_NAKED
        #include <stdlib.h>
        #define PERF_COUNTER_START(x)
        #define PERF_COUNTER_END(x)
        #define MALLOC_ALLOCATOR_MALLOC(size) malloc(size)
        #define MALLOC_ALLOCATOR_FREE(pointer, size) free(pointer)
    #else
        #include "platform.h"
        #include "profile.h"
        #define MALLOC_ALLOCATOR_MALLOC(size) platform_heap_reallocate(size, NULL, 0, DEF_ALIGN)
        #define MALLOC_ALLOCATOR_FREE(pointer, size) platform_heap_reallocate(0, pointer, size, DEF_ALIGN)
    #endif
    
    INTERNAL void* _malloc_allocator_parent_allocate(Malloc_Allocator* self, isize size, Source_Info source)
    {
        if(self->parent != NULL)
            return self->parent->allocate(self->parent, size, NULL, 0, DEF_ALIGN, source);
        else
            return MALLOC_ALLOCATOR_MALLOC(size);
    }
    
    INTERNAL void _malloc_allocator_parent_free(Malloc_Allocator* self, void* pointer, isize size, Source_Info source)
    {
        if(self->parent != NULL)
            self->parent->allocate(self->parent, 0, pointer, size, DEF_ALIGN, source);
        else
            MALLOC_ALLOCATOR_FREE(pointer, size);
    }

    INTERNAL void _malloc_allocator_assert_block_coherency(Malloc_Allocator* self, Malloc_Allocator_Block_Header* block)
    {
        (void) self;
        if(block == NULL)
            return;

        #ifdef DO_ASSERTS_SLOW
            ASSERT_SLOW(block->magic_number == MALLOC_ALLOCATOR_MAGIC_NUMBER);
            ASSERT_SLOW((self->first_block == NULL) == (self->last_block == NULL));
            ASSERT_SLOW((block->next_block == NULL) == (self->last_block == block));
            ASSERT_SLOW((block->prev_block == NULL) == (self->first_block == block));
            if(block->prev_block != NULL)
                ASSERT_SLOW(block->prev_block->next_block == block);
            if(block->next_block != NULL)
                ASSERT_SLOW(block->next_block->prev_block == block);
        #endif
    }

    EXPORT void malloc_allocator_init(Malloc_Allocator* self)
    {
        if(self == NULL)
            return;

        malloc_allocator_deinit(self);
        self->allocator.allocate = malloc_allocator_allocate;
        self->allocator.get_stats = malloc_allocator_get_stats;
    }

    EXPORT void malloc_allocator_init_use(Malloc_Allocator* self, u64 flags)
    {
        (void) flags;
        malloc_allocator_init(self);
        self->allocator_backup = allocator_set_both(&self->allocator, &self->allocator);
    }

    EXPORT void malloc_allocator_deinit(Malloc_Allocator* self)
    {
        _malloc_allocator_assert_block_coherency(self, self->first_block);
        _malloc_allocator_assert_block_coherency(self, self->last_block);
        for(Malloc_Allocator_Block_Header* block = self->first_block; block != NULL; )
        {
            Malloc_Allocator_Block_Header* next_block = block->next_block;
            _malloc_allocator_assert_block_coherency(self, block);
            malloc_allocator_allocate(&self->allocator, 0, block + 1, block->size, block->align, SOURCE_INFO());
            block = next_block;
        }
        
        allocator_set(self->allocator_backup);

        ASSERT_SLOW(self->first_block == NULL);
        ASSERT_SLOW(self->last_block == NULL);
        ASSERT(self->bytes_allocated == 0);
        ASSERT(self->allocation_count == self->deallocation_count);
        Malloc_Allocator null = {0};
        *self = null;
    }

    EXPORT void* malloc_allocator_allocate(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align, Source_Info called_from)
    {
        PERF_COUNTER_START(c);
        (void) called_from;
        Malloc_Allocator* self = (Malloc_Allocator*) (void*) self_;
        void* out_ptr = NULL;

        if(new_size != 0)
        {
            void* actual_new_ptr = _malloc_allocator_parent_allocate(self, new_size + align + sizeof(Malloc_Allocator_Block_Header), called_from);
            //if error return error
            if(actual_new_ptr == NULL)
            {
                PERF_COUNTER_END(c);
                return NULL;
            }

            out_ptr = align_forward((u8*) actual_new_ptr + sizeof(Malloc_Allocator_Block_Header), align);

            Malloc_Allocator_Block_Header* new_block_ptr = (Malloc_Allocator_Block_Header*) out_ptr - 1;
            Malloc_Allocator_Block_Header null = {0};
            *new_block_ptr = null;
            new_block_ptr->align = (i32) align;
            new_block_ptr->size = (i32) new_size;
            new_block_ptr->heap_block_offset = (i32) ((isize) out_ptr - (isize) actual_new_ptr);
            new_block_ptr->magic_number = MALLOC_ALLOCATOR_MAGIC_NUMBER;
            new_block_ptr->next_block = NULL;
            new_block_ptr->prev_block = self->last_block;

            if(new_block_ptr->prev_block != NULL)
            {
                new_block_ptr->prev_block->next_block = new_block_ptr;
            }

            self->last_block = new_block_ptr;
            if(self->first_block == NULL)
                self->first_block = new_block_ptr;

            _malloc_allocator_assert_block_coherency(self, new_block_ptr);
            ASSERT((self->first_block == NULL) == (self->last_block == NULL));
            ASSERT(out_ptr != NULL);
        }

        if(old_ptr != NULL)
        {
            Malloc_Allocator_Block_Header* old_block_ptr = (Malloc_Allocator_Block_Header*) old_ptr - 1;
            ASSERT(old_block_ptr->magic_number == MALLOC_ALLOCATOR_MAGIC_NUMBER);
            ASSERT(old_block_ptr->size == old_size);
            ASSERT(old_block_ptr->align == align);

            _malloc_allocator_assert_block_coherency(self, old_block_ptr);

            if(old_block_ptr->next_block != NULL)
                old_block_ptr->next_block->prev_block = old_block_ptr->prev_block;
                
            if(old_block_ptr->prev_block != NULL)
                old_block_ptr->prev_block->next_block = old_block_ptr->next_block;
            
            if(self->last_block == old_block_ptr)
                self->last_block = old_block_ptr->prev_block;
                
            if(self->first_block == old_block_ptr)
                self->first_block = old_block_ptr->next_block;

            ASSERT((self->first_block == NULL) == (self->last_block == NULL));
            
            isize smaller_size = new_size < old_size ? new_size : old_size;
            memcpy(out_ptr, old_ptr, smaller_size);

            _malloc_allocator_parent_free(self, (u8*) old_ptr - old_block_ptr->heap_block_offset, old_size + align + sizeof(Malloc_Allocator_Block_Header), called_from);
        }
        
        if(old_ptr == NULL)
            self->allocation_count += 1;
        else if(new_size == 0)
            self->deallocation_count += 1;
        else
            self->reallocation_count += 1;

        self->bytes_allocated += new_size - old_size;
        if(self->max_bytes_allocated < self->bytes_allocated)
            self->max_bytes_allocated = self->bytes_allocated;
            
        PERF_COUNTER_END(c);
        return out_ptr;
    }

    EXPORT Allocator_Stats malloc_allocator_get_stats(Allocator* self_)
    {
        Malloc_Allocator* self = (Malloc_Allocator*) (void*) self_;
        Allocator_Stats out = {0};
        out.type_name = "Malloc_Allocator";
        out.name = self->name;
        out.parent = NULL;
        out.is_top_level = true;
        out.max_bytes_allocated = self->max_bytes_allocated;
        out.bytes_allocated = self->bytes_allocated;
        out.allocation_count = self->allocation_count;
        out.deallocation_count = self->deallocation_count;
        out.reallocation_count = self->reallocation_count;

        return out;
    }
    
    EXPORT Malloc_Allocator_Block_Header* malloc_allocator_get_block_header(Malloc_Allocator* self, void* old_ptr)
    {
        (void) self;
        Malloc_Allocator_Block_Header* out = (Malloc_Allocator_Block_Header*) old_ptr - 1;
        ASSERT(out->magic_number == MALLOC_ALLOCATOR_MAGIC_NUMBER);
        return out;
    }

    EXPORT isize malloc_allocator_get_block_size(Malloc_Allocator* self, void* old_ptr)
    {
        return malloc_allocator_get_block_header(self, old_ptr)->size;
    }
    
    EXPORT void* malloc_allocator_malloc(Malloc_Allocator* self, isize size)
    {
        return malloc_allocator_allocate(&self->allocator, size, NULL, 0, DEF_ALIGN, SOURCE_INFO());
    }

    EXPORT void* malloc_allocator_realloc(Malloc_Allocator* self, void* old_ptr, isize new_size)
    {
        isize old_size = malloc_allocator_get_block_size(self, old_ptr);
        void* out = malloc_allocator_allocate(&self->allocator, new_size, old_ptr, old_size, DEF_ALIGN, SOURCE_INFO());
        return out;
    }

    EXPORT void  malloc_allocator_free(Malloc_Allocator* self, void* old_ptr)
    {
        isize old_size = malloc_allocator_get_block_size(self, old_ptr);
        malloc_allocator_allocate(&self->allocator, 0, old_ptr, old_size, DEF_ALIGN, SOURCE_INFO());
    }

#endif
