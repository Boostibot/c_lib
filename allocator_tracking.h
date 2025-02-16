#ifndef MODULE_ALLOCATOR_MALLOC
#define MODULE_ALLOCATOR_MALLOC

// A tracking allocator building block and its use in Tracking_Allocator.
// 
// Works by prepending each allocation with a header that tracks its attributes packet to only 24B.
// (which is a bigger achievement then how it sounds). This also enables us to traverse all active allocations
// and deinit them if need be. Offers basic correctness checking.
// 
// The main purpose of Tracking_Allocator is to be a quick substitute until more complex allocators are built.
// Tracking_Allocator also exposes a malloc like interface for some basic control over allocations

#include <stdlib.h>
#include "allocator.h"

typedef struct Allocation_List_Block {
    struct Allocation_List_Block* next_block; 
    struct Allocation_List_Block* prev_block;

    uint64_t align       : 16;
    uint64_t size        : 47;
    uint64_t is_offset   : 1;

    #ifdef DO_ASSERTS
    char magic[8];
    #endif
} Allocation_List_Block;

typedef struct Allocation_List {
    Allocation_List_Block* last_block;
} Allocation_List;

typedef struct Tracking_Allocator {
    Allocator alloc[1];
    Allocator* parent; //parent allocator. If parent is null uses malloc/free
    Allocation_List list;

    const char* name;
    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    Allocator_Set allocator_backup;
    uint64_t flags;
} Tracking_Allocator;

EXTERNAL void  allocation_list_free_all(Allocation_List* self, Allocator* parent_or_null);
EXTERNAL void* allocation_list_allocate(Allocation_List* self, Allocator* parent_or_null, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error);

EXTERNAL isize allocation_list_get_block_size(Allocation_List* self, void* old_ptr);
EXTERNAL Allocation_List_Block* allocation_list_get_block_header(Allocation_List* self, void* old_ptr);

#define TRACKING_ALLOCATOR_INIT_USE 1
EXTERNAL void tracking_allocator_init(Tracking_Allocator* self, const char* name, uint64_t flags);
EXTERNAL void tracking_allocator_deinit(Tracking_Allocator* self);
 
EXTERNAL void* tracking_allocator_malloc(Tracking_Allocator* self, isize size);
EXTERNAL void* tracking_allocator_realloc(Tracking_Allocator* self, void* old_ptr, isize new_size);
EXTERNAL void tracking_allocator_free(Tracking_Allocator* self, void* old_ptr);

EXTERNAL void* tracking_allocator_func(void* self_void, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_ALLOCATOR_MALLOC)) && !defined(MODULE_HAS_IMPL_ALLOCATOR_MALLOC)
#define MODULE_HAS_IMPL_ALLOCATOR_MALLOC

    //the way this file is written this can simple be changed to malloc just by defining MALLOC_ALLOCATOR_NAKED
    #ifdef PROFILE_START
        #define PROFILE_START(...)
        #define PROFILE_STOP(...)
    #endif

    #ifndef INTERNAL
        #define INTERNAL inline static
    #endif
    
    #define ALLOCATION_LIST_MAGIC "TrackAl"
    INTERNAL void _allocation_list_assert_block_coherency(Allocation_List* self, Allocation_List_Block* block)
    {
        (void) self;
        if(block == NULL)
            return;

        #ifdef DO_ASSERTS
            ASSERT_SLOW(memcmp(block->magic, ALLOCATION_LIST_MAGIC, sizeof ALLOCATION_LIST_MAGIC) == 0);
            ASSERT_SLOW((block->next_block == NULL) == (self->last_block == block));
            if(block->prev_block != NULL)
                ASSERT_SLOW(block->prev_block->next_block == block);
            if(block->next_block != NULL)
                ASSERT_SLOW(block->next_block->prev_block == block);
        #endif
    }
    
    EXTERNAL void allocation_list_free_all(Allocation_List* self, Allocator* parent_or_null)
    {
        _allocation_list_assert_block_coherency(self, self->last_block);
        for(Allocation_List_Block* block = self->last_block; block != NULL; )
        {
            Allocation_List_Block* prev_block = block->prev_block;
            _allocation_list_assert_block_coherency(self, block);
            
            allocation_list_allocate(self, parent_or_null, 0, block + 1, block->size, block->align, NULL);
            block = prev_block;
        }

        memset(self, 0, sizeof *self);
    }

    EXTERNAL void* allocation_list_allocate(Allocation_List* self, Allocator* parent_or_null, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error)
    {
        PROFILE_START();
        isize capped_align = align > DEF_ALIGN ? align : DEF_ALIGN;
        void* out_ptr = NULL;
        if(new_size != 0)
        {
            isize new_allocation_size = new_size + capped_align - DEF_ALIGN + (isize) sizeof(Allocation_List_Block);
            void* new_allocation = NULL;
            if(parent_or_null != NULL)
                new_allocation = allocator_try_reallocate(parent_or_null, new_allocation_size, NULL, 0, DEF_ALIGN, error);
            else
                new_allocation = malloc(new_allocation_size);

            //if error return error
            if(new_allocation == NULL)
                goto error;

            uint8_t* would_have_been_place = (uint8_t*) new_allocation + sizeof(Allocation_List_Block);
            out_ptr = align_forward(would_have_been_place, capped_align);

            Allocation_List_Block* new_block_ptr = (Allocation_List_Block*) out_ptr - 1;
            
            //If is overaligned and the resulting pointer is offset from its 
            // would be have been place save the offset just before it
            // (we can use uint64_t because there will be at least 64 bits of
            // free space since we capped the alignment to 8)
            bool is_offset = out_ptr != would_have_been_place;
            if(is_offset)
            {
                uint64_t* offset = (uint64_t*) (void*) new_block_ptr - 1;
                *offset = (uint64_t) new_allocation - (uint64_t) new_block_ptr;
            }

            new_block_ptr->is_offset = is_offset; 
            #ifdef __GNUC__
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wconversion"
            #endif
            new_block_ptr->align = (uint64_t) align; 
            new_block_ptr->size = (uint64_t) new_size; 
            #ifdef __GNUC__
                #pragma GCC diagnostic pop
            #endif
            
            #ifdef DO_ASSERTS
                memcpy(new_block_ptr->magic, ALLOCATION_LIST_MAGIC, sizeof ALLOCATION_LIST_MAGIC);
            #endif

            //Link itself into the list
            new_block_ptr->next_block = NULL;
            new_block_ptr->prev_block = self->last_block;
                
            if(new_block_ptr->prev_block != NULL)
                new_block_ptr->prev_block->next_block = new_block_ptr;

            self->last_block = new_block_ptr;

            _allocation_list_assert_block_coherency(self, new_block_ptr);
            ASSERT(out_ptr != NULL);
        }

        if(old_ptr != NULL)
        {
            Allocation_List_Block* old_block_ptr = (Allocation_List_Block*) old_ptr - 1;
            _allocation_list_assert_block_coherency(self, old_block_ptr);
            ASSERT((isize) old_block_ptr->size == old_size && (isize) old_block_ptr->align == align);

            //Unlink the block from the list
            if(old_block_ptr->next_block != NULL)
                old_block_ptr->next_block->prev_block = old_block_ptr->prev_block;
                
            if(old_block_ptr->prev_block != NULL)
                old_block_ptr->prev_block->next_block = old_block_ptr->next_block;
            
            if(self->last_block == old_block_ptr)
                self->last_block = old_block_ptr->prev_block;
                
            //Copy data over
            isize smaller_size = new_size < old_size ? new_size : old_size;
            memcpy(out_ptr, old_ptr, (size_t) smaller_size);

            //Calculate the pointer from which the allocation occurred. 
            //This is just the block pointer if it is not offset.
            void* old_allocation = old_block_ptr;
            if(old_block_ptr->is_offset)
            {
                uint64_t* offset = ((uint64_t*) old_allocation) - 1;
                old_allocation = (uint8_t*) old_allocation - *offset;
            }   

            isize old_allocation_size = old_size + capped_align - DEF_ALIGN + (isize) sizeof(Allocation_List_Block);
            if(parent_or_null != NULL)
                allocator_try_reallocate(parent_or_null, 0, old_allocation, old_allocation_size, DEF_ALIGN, error);
            else
                free(old_allocation);
        }
            
        PROFILE_STOP();
        return out_ptr;

        error:
        //@TODO: make better!
        if(parent_or_null == NULL)
            allocator_error(error, ALLOCATOR_ERROR_OUT_OF_MEM, NULL, new_size, old_ptr, old_size, align, "malloc failed");
        return out_ptr;
    }

    EXTERNAL Allocation_List_Block* allocation_list_get_block_header(Allocation_List* self, void* old_ptr)
    {
        Allocation_List_Block* out = (Allocation_List_Block*) old_ptr - 1;
        _allocation_list_assert_block_coherency(self, out);
        return out;
    }

    EXTERNAL isize allocation_list_get_block_size(Allocation_List* self, void* old_ptr)
    {
        Allocation_List_Block* block = allocation_list_get_block_header(self, old_ptr);
        return block->size;
    }
    
    EXTERNAL void tracking_allocator_init(Tracking_Allocator* self, const char* name, uint64_t flags)
    {
        tracking_allocator_deinit(self);
        self->alloc[0] = tracking_allocator_func;
        self->name = name;
        self->flags = flags;
        if(flags & TRACKING_ALLOCATOR_INIT_USE)
            self->allocator_backup = allocator_set_default(&self->alloc[0]);
    }
    
    EXTERNAL void tracking_allocator_deinit(Tracking_Allocator* self)
    {
        allocation_list_free_all(&self->list, self->parent);
        if(self->flags & TRACKING_ALLOCATOR_INIT_USE)
            allocators_set(self->allocator_backup);

        memset(self, 0, sizeof *self);
    }

    EXTERNAL void* tracking_allocator_func(void* self_void, int mode, isize new_size, void* old_ptr, isize old_size, isize align, void* rest)
    {
        Tracking_Allocator* self = (Tracking_Allocator*) (void*) self_void;
        if(mode == ALLOCATOR_MODE_ALLOC) {
            void* out = allocation_list_allocate(&self->list, self->parent, new_size, old_ptr, old_size, align, (Allocator_Error*) rest);

            if(old_size == 0)
                self->allocation_count += 1;
            if(new_size == 0)
                self->deallocation_count += 1;
            if(new_size != 0 && old_size != 0)
                self->reallocation_count += 1;

            self->bytes_allocated += new_size - old_size;
            if(self->max_bytes_allocated < self->bytes_allocated)
                self->max_bytes_allocated = self->bytes_allocated;

            return out;
        }
        if(mode) {
            Allocator_Stats out = {0};
            out.type_name = "Tracking_Allocator";
            out.name = self->name;
            out.parent = NULL;
            out.is_top_level = true;
            out.is_growing = true;
            out.is_capable_of_resize = true;
            out.is_capable_of_free_all = true;
            out.max_bytes_allocated = self->max_bytes_allocated;
            out.bytes_allocated = self->bytes_allocated;
            out.allocation_count = self->allocation_count;
            out.deallocation_count = self->deallocation_count;
            out.reallocation_count = self->reallocation_count;
            *(Allocator_Stats*) rest = out;
        }
        return NULL;
    }

    EXTERNAL void* tracking_allocator_malloc(Tracking_Allocator* self, isize size)
    {
        return allocation_list_allocate(&self->list, self->parent, size, NULL, 0, DEF_ALIGN, NULL);
    }

    EXTERNAL void* tracking_allocator_realloc(Tracking_Allocator* self, void* old_ptr, isize new_size)
    {
        isize old_size = allocation_list_get_block_size(&self->list, old_ptr);
        void* out = allocation_list_allocate(&self->list, self->parent, new_size, old_ptr, old_size, DEF_ALIGN, NULL);
        return out;
    }

    EXTERNAL void tracking_allocator_free(Tracking_Allocator* self, void* old_ptr)
    {
        if(old_ptr != NULL)
        {
            isize old_size = allocation_list_get_block_size(&self->list, old_ptr);
            allocation_list_allocate(&self->list, self->parent, 0, old_ptr, old_size, DEF_ALIGN, NULL);
        }
    }
#endif
