#pragma once

#include "../stable.h"
#include "../allocator_debug.h"

static void test_stable()
{
	Debug_Allocator debug_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_USE);
    {
        Stable stable = {0};
        stable_init(&stable, debug_alloc.alloc, sizeof(int32_t));

        int32_t* val = NULL;
        isize i1 = stable_insert(&stable, (void**) &val);

        int32_t* val_get = (int32_t*) stable_at(&stable, i1);
        TEST(val == val_get);
        *val = 32;

        TEST(stable_at_or(&stable, -2, NULL) == NULL);
        TEST(stable_at_or(&stable, -1, NULL) == NULL);
        TEST(stable_at_or(&stable, 0, NULL) != NULL);
        TEST(stable_at_or(&stable, 1, NULL) == NULL);
        TEST(stable_at_or(&stable, 2, NULL) == NULL);
        stable_remove(&stable, 0);

        enum {INSERT_COUNT = 129};
        for(isize i = 0; i < INSERT_COUNT; i++)
        {
            int32_t* at = NULL;
            isize index = stable_insert(&stable, (void**) &at);
            *at = (int32_t) i;
            TEST(index == i);
        }
        
        STABLE_FOR(&stable, it, int32_t, value) 
            TEST(*value == it.index);

        for(isize i = 0; i < INSERT_COUNT; i++)
        {
            int32_t* at = (int32_t*) stable_at(&stable, i);
            TEST(*at == i);
            stable_remove(&stable, i);
        }

        stable_deinit(&stable);
    }

    debug_allocator_deinit(&debug_alloc);
}