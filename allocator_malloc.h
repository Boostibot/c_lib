#ifndef JOT_ALLOCATOR_MALLOC
#define JOT_ALLOCATOR_MALLOC

// A tracking allocator building block and its use in Malloc_Allocator (allocator which simply wraps malloc).
// 
// Works by prepending each allocation with a header that tracks its attributes packet to only 24B.
// (which is a bigger achievement then how it sounds). This also enables us to traverse all active allocations
// and deinit them if need be. Offers basic corectness checking.
// 
// The main purpose of Malloc_Allocator is to be a quick substitute until more complex allocators are built.
// Malloc_Allocator also exposes a malloc like inetrface for some basic control over allocations

#include "allocator.h"

typedef struct Allocation_List_Block Allocation_List_Block;

typedef struct Allocation_List {
    Allocation_List_Block* last_block;
} Allocation_List;

typedef struct Malloc_Allocator {
    Allocator allocator;
    Allocator* parent; //parent allocator. If parent is null uses malloc/free
    Allocation_List list;

    const char* name;
    isize bytes_allocated;
    isize max_bytes_allocated;

    isize allocation_count;
    isize deallocation_count;
    isize reallocation_count;

    Allocator_Set allocator_backup;
} Malloc_Allocator;

#define ALLOCATION_LIST_DEBUG
#define ALLOCATION_LIST_MAGIC           "Allocll"

typedef struct Allocation_List_Block {
    Allocation_List_Block* next_block; 
    Allocation_List_Block* prev_block;

    //uses bits [63       ][62....46][45............0]
    //          [is_offset][align   ][size           ]
    u64 packed_info;

    #ifdef ALLOCATION_LIST_DEBUG
    char magic[8];
    #endif
} Allocation_List_Block;

typedef struct Allocation_List_Info {
    u64 size;
    u64 align;
    bool is_offset;
} Allocation_List_Info;

EXPORT Allocation_List_Info allocation_list_info_unpack(u64 size_and_align);
EXPORT u64 allocation_list_info_pack(Allocation_List_Info info);

EXPORT void  allocation_list_free_all(Allocation_List* self, Allocator* parent_or_null);
EXPORT void* allocation_list_allocate(Allocation_List* self, Allocator* parent_or_null, isize new_size, void* old_ptr, isize old_size, isize align);

EXPORT isize allocation_list_get_block_size(Allocation_List* self, void* old_ptr);
EXPORT Allocation_List_Block* allocation_list_get_block_header(Allocation_List* self, void* old_ptr);

EXPORT void malloc_allocator_init(Malloc_Allocator* self, const char* name);
EXPORT void malloc_allocator_init_use(Malloc_Allocator* self, const char* name, u64 flags);  //convenience function that inits the allocator then imidietely makes it the default and scratch. On deinit restores to previous defaults
EXPORT void malloc_allocator_deinit(Malloc_Allocator* self);
 
EXPORT void* malloc_allocator_malloc(Malloc_Allocator* self, isize size);
EXPORT void* malloc_allocator_realloc(Malloc_Allocator* self, void* old_ptr, isize new_size);
EXPORT void malloc_allocator_free(Malloc_Allocator* self, void* old_ptr);

EXPORT void* malloc_allocator_allocate(Allocator* self, isize new_size, void* old_ptr, isize old_size, isize align);
EXPORT Allocator_Stats malloc_allocator_get_stats(Allocator* self);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ALLOCATOR_MALLOC_IMPL)) && !defined(JOT_ALLOCATOR_MALLOC_HAS_IMPL)
#define JOT_ALLOCATOR_MALLOC_HAS_IMPL

    //the way this file is written this can simple be changed to malloc just by defining MALLOC_ALLOCATOR_NAKED
    #ifdef MALLOC_ALLOCATOR_NAKED
        #include <stdlib.h>
        #define PERF_COUNTER_START(x)
        #define PERF_COUNTER_END(x)
        #define MALLOC_ALLOCATOR_MALLOC(size) malloc(size)
        #define MALLOC_ALLOCATOR_FREE(pointer) free(pointer)
    #else
        #include "platform.h"
        #include "profile.h"
        #define MALLOC_ALLOCATOR_MALLOC(size) platform_heap_reallocate(size, NULL, DEF_ALIGN)
        #define MALLOC_ALLOCATOR_FREE(pointer) platform_heap_reallocate(0, pointer, DEF_ALIGN)
    #endif 

    #define ALLOCATION_LIST_SIZE_BITS       46
    #define ALLOCATION_LIST_ALIGN_BITS      17
    #define ALLOCATION_LIST_IS_OFFSET_BIT   63
    #define ALLOCATION_LIST_SIZE_MASK       (((u64) 1 << ALLOCATION_LIST_SIZE_BITS) - 1)
    #define ALLOCATION_LIST_ALIGN_MASK      (((u64) 1 << ALLOCATION_LIST_ALIGN_BITS) - 1)

    EXPORT Allocation_List_Info allocation_list_info_unpack(u64 size_and_align)
    {
        Allocation_List_Info out = {0};
        out.size = size_and_align & ALLOCATION_LIST_SIZE_MASK; 
        out.align = (size_and_align >> ALLOCATION_LIST_SIZE_BITS) & ALLOCATION_LIST_ALIGN_MASK;
        out.is_offset = (size_and_align >> ALLOCATION_LIST_IS_OFFSET_BIT) > 0;

        return out;
    }
    
    EXPORT u64 allocation_list_info_pack(Allocation_List_Info info)
    {
        u64 max_align = ((u64) 1 << ALLOCATION_LIST_ALIGN_BITS) - 1;
        ASSERT(is_power_of_two(info.align) && info.align <= max_align);

        u64 size_and_align = ((u64) info.is_offset << ALLOCATION_LIST_IS_OFFSET_BIT) 
            |  ((u64) info.align << ALLOCATION_LIST_SIZE_BITS) 
            | ((u64) info.size & ALLOCATION_LIST_SIZE_MASK);

        return size_and_align;
    }

    INTERNAL void _allocation_list_assert_block_coherency(Allocation_List* self, Allocation_List_Block* block)
    {
        (void) self;
        if(block == NULL)
            return;

        #ifdef DO_ASSERTS_SLOW
            #ifdef ALLOCATION_LIST_DEBUG
                ASSERT_SLOW(memcmp(block->magic, ALLOCATION_LIST_MAGIC, sizeof ALLOCATION_LIST_MAGIC) == 0);
            #endif
            ASSERT_SLOW((block->next_block == NULL) == (self->last_block == block));
            if(block->prev_block != NULL)
                ASSERT_SLOW(block->prev_block->next_block == block);
            if(block->next_block != NULL)
                ASSERT_SLOW(block->next_block->prev_block == block);
        #endif
    }

    
    EXPORT void allocation_list_free_all(Allocation_List* self, Allocator* parent_or_null)
    {
        _allocation_list_assert_block_coherency(self, self->last_block);
        for(Allocation_List_Block* block = self->last_block; block != NULL; )
        {
            Allocation_List_Block* prev_block = block->prev_block;
            _allocation_list_assert_block_coherency(self, block);
            
            Allocation_List_Info info = allocation_list_info_unpack(prev_block->packed_info);
            allocation_list_allocate(self, parent_or_null, 0, block + 1, info.size, info.align);
            block = prev_block;
        }

        memset(self, 0, sizeof *self);
    }

    EXPORT void* allocation_list_allocate(Allocation_List* self, Allocator* parent_or_null, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        isize capped_align = MAX(align, DEF_ALIGN);

        void* out_ptr = NULL;
        if(new_size != 0)
        {
            isize new_allocation_size = new_size + capped_align - DEF_ALIGN + (isize) sizeof(Allocation_List_Block);
            void* new_allocation = NULL;
            if(parent_or_null != NULL)
                new_allocation = parent_or_null->allocate(parent_or_null, new_allocation_size, NULL, 0, DEF_ALIGN);
            else
                new_allocation = MALLOC_ALLOCATOR_MALLOC(new_allocation_size);

            //if error return error
            if(new_allocation == NULL)
                return NULL;

            u8* would_have_been_place = (u8*) new_allocation + sizeof(Allocation_List_Block);
            out_ptr = align_forward(would_have_been_place, capped_align);

            Allocation_List_Block* new_block_ptr = (Allocation_List_Block*) out_ptr - 1;
            
            //If is overaligned and the resulting pointer is offset from its 
            // would be have been place save the offset just before it
            // (we can use u64 because there will be at least 64 bits of
            // free space since we capped the alignment to 8)
            bool is_offset = out_ptr != would_have_been_place;
            if(is_offset)
            {
                u64* offset = (u64*) (void*) new_block_ptr - 1;
                *offset = (u64) new_allocation - (u64) new_block_ptr;
            }

            //Fill stats
            Allocation_List_Info info = {0};
            info.size = new_size;
            info.align = align;
            info.is_offset = is_offset;

            new_block_ptr->packed_info = allocation_list_info_pack(info); 
            #ifdef ALLOCATION_LIST_DEBUG
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

            Allocation_List_Info info = allocation_list_info_unpack(old_block_ptr->packed_info);
            ASSERT((isize) info.size == old_size && (isize) info.align == align);

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

            //Calculate the pointer from which the allocation occured. 
            //This is just the block pointer if it is not offset.
            void* old_allocation = old_block_ptr;
            if(info.is_offset)
            {
                u64* offset = ((u64*) old_allocation) - 1;
                old_allocation = (u8*) old_allocation - *offset;
            }   

            isize old_allocation_size = old_size + capped_align - DEF_ALIGN + (isize) sizeof(Allocation_List_Block);
            if(parent_or_null != NULL)
                parent_or_null->allocate(parent_or_null, 0, old_allocation, old_allocation_size, DEF_ALIGN);
            else
                MALLOC_ALLOCATOR_FREE(old_allocation);
        }
            
        return out_ptr;
    }

    EXPORT Allocation_List_Block* allocation_list_get_block_header(Allocation_List* self, void* old_ptr)
    {
        Allocation_List_Block* out = (Allocation_List_Block*) old_ptr - 1;
        _allocation_list_assert_block_coherency(self, out);
        return out;
    }

    EXPORT isize allocation_list_get_block_size(Allocation_List* self, void* old_ptr)
    {
        Allocation_List_Block* block = allocation_list_get_block_header(self, old_ptr);
        Allocation_List_Info info = allocation_list_info_unpack(block->packed_info);
        return info.size;
    }

    EXPORT void malloc_allocator_init(Malloc_Allocator* self, const char* name)
    {
        if(self == NULL)
            return;

        malloc_allocator_deinit(self);
        self->allocator.allocate = malloc_allocator_allocate;
        self->allocator.get_stats = malloc_allocator_get_stats;
        self->name = name;
    }

    EXPORT void malloc_allocator_init_use(Malloc_Allocator* self, const char* name, u64 flags)
    {
        (void) flags;
        malloc_allocator_init(self, name);
        self->allocator_backup = allocator_set_both(&self->allocator, &self->allocator);
    }

    EXPORT void malloc_allocator_deinit(Malloc_Allocator* self)
    {
        allocation_list_free_all(&self->list, self->parent);
        allocator_set(self->allocator_backup);
        memset(self, 0, sizeof *self);
    }

    EXPORT void* malloc_allocator_allocate(Allocator* self_, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        PERF_COUNTER_START();
        Malloc_Allocator* self = (Malloc_Allocator*) (void*) self_;
        void* out = allocation_list_allocate(&self->list, self->parent, new_size, old_ptr, old_size, align);

        if(old_ptr == NULL)
            self->allocation_count += 1;
        else if(new_size == 0)
            self->deallocation_count += 1;
        else
            self->reallocation_count += 1;

        self->bytes_allocated += new_size - old_size;
        if(self->max_bytes_allocated < self->bytes_allocated)
            self->max_bytes_allocated = self->bytes_allocated;
        PERF_COUNTER_END();

        return out;
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

    EXPORT void* malloc_allocator_malloc(Malloc_Allocator* self, isize size)
    {
        return allocation_list_allocate(&self->list, self->parent, size, NULL, 0, DEF_ALIGN);
    }

    EXPORT void* malloc_allocator_realloc(Malloc_Allocator* self, void* old_ptr, isize new_size)
    {
        isize old_size = allocation_list_get_block_size(&self->list, old_ptr);
        void* out = allocation_list_allocate(&self->list, self->parent, new_size, old_ptr, old_size, DEF_ALIGN);
        return out;
    }

    EXPORT void malloc_allocator_free(Malloc_Allocator* self, void* old_ptr)
    {
        if(old_ptr != NULL)
        {
            isize old_size = allocation_list_get_block_size(&self->list, old_ptr);
            allocation_list_allocate(&self->list, self->parent, 0, old_ptr, old_size, DEF_ALIGN);
        }
    }
#endif
