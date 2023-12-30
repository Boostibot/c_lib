#pragma once

#include "allocator_debug.h"
static void break_debug_allocator()
{
    //My theory is that when reallocating from high aligned offset to low aligned offset we dont shift over the data
    //I belive this can be easily solved by alloc & dealloc pair instead of the singular realloc we do currenlty.

    Debug_Allocator debug_alloc = {0};
    debug_allocator_init(&debug_alloc, allocator_get_default(), DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK | DEBUG_ALLOCATOR_CAPTURE_CALLSTACK);
    {
        Allocator* allocator = allocator_get_default();
        isize alloc_gran = 41;
        enum {ITERS = 100, TEST_VAL = 0x66};
        u8* allocs[ITERS] = {0};
        isize sizes[ITERS] = {0};
        isize aligns[ITERS] = {0};

        isize i = 6;
        //for(isize i = 0; i < ITERS; i++)
        {
            sizes[i] = alloc_gran*i;
            aligns[i] = (isize) 1 << (i % 13); 
            allocs[i] = (u8*) debug_allocator_allocate(allocator, sizes[i], NULL, 0, aligns[i], SOURCE_INFO());

            memset(allocs[i], TEST_VAL, sizes[i]);
            for(isize k = 0; k < sizes[i]; k++)
                TEST(allocs[i][k] == TEST_VAL);
        }
        
        //for(isize i = 0; i < ITERS; i++)
        {
            u8* alloc = allocs[i];
            isize size = sizes[i];
            isize align = aligns[i];

            allocs[i] = (u8*) debug_allocator_allocate(allocator, size + alloc_gran, alloc, size, align, SOURCE_INFO());
            sizes[i] = size + alloc_gran;
            for(isize k = 0; k < size - alloc_gran; k++)
                TEST(alloc[k] == TEST_VAL);
        }
        //for(isize i = 0; i < ITERS; i++)
        {
            u8* alloc = allocs[i];
            isize size = sizes[i];
            isize align = aligns[i]; 
            for(isize k = 0; k < size - alloc_gran; k++)
                TEST(alloc[k] == TEST_VAL);

            allocator_deallocate(allocator, alloc, size, align, SOURCE_INFO());
        }
    }
    debug_allocator_deinit(&debug_alloc);
}