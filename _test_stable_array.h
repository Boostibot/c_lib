#pragma once

#include "stable_array.h"
#include "allocator_debug.h"

static void test_stable_array()
{
    Debug_Allocator resources_alloc = {0};
    debug_allocator_init(&resources_alloc, allocator_get_default(), DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK | DEBUG_ALLOCATOR_CAPTURE_CALLSTACK);
    {
        Stable_Array stable = {0};
        stable_array_init(&stable, allocator_get_default(), sizeof(i32));

        i32* val = NULL;
        isize i1 = stable_array_insert(&stable, (void**) &val);

        i32* val_get = stable_array_at(&stable, i1);
        TEST(val == val_get);
        *val = 32;

        TEST(stable_array_at_if_alive(&stable, -2) == NULL);
        TEST(stable_array_at_if_alive(&stable, -1) == NULL);
        TEST(stable_array_at_if_alive(&stable, 0) != NULL);
        TEST(stable_array_at_if_alive(&stable, 1) == NULL);
        TEST(stable_array_at_if_alive(&stable, 2) == NULL);
        TEST(stable_array_remove(&stable, 0));

        enum {INSERT_COUNT = 129};
        for(isize i = 0; i < INSERT_COUNT; i++)
        {
            i32* at = NULL;
            isize index = stable_array_insert(&stable, (void**) &at);
            *at = (i32) i;
            TEST(index == i);
        }

        STABLE_ARRAY_FOR_EACH_BEGIN(stable, i32*, ptr, isize, index)
            i32* at = stable_array_at(&stable, index);
            TEST(*at == index);
        STABLE_ARRAY_FOR_EACH_END
        
        STABLE_ARRAY_FOR_EACH_BEGIN2(stable, i32*, ptr, isize, index)
            i32* at = stable_array_at(&stable, index);
            TEST(*at == index);
        STABLE_ARRAY_FOR_EACH_END2

        for(isize i = 0; i < INSERT_COUNT; i++)
        {
            i32* at = stable_array_at(&stable, i);
            TEST(*at == i);
            TEST(stable_array_remove(&stable, i));
        }

        stable_array_deinit(&stable);
    }

    debug_allocator_deinit(&resources_alloc);
}