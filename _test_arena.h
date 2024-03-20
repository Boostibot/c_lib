#pragma once
#include "arena.h"

static char* arena_push_string(Arena* arena, const char* string)
{
    isize len = string ? strlen(string) : 0;   
    char* pat1 = (char*) arena_push(arena, len + 1, 1);
    memcpy(pat1, string, len);
    pat1[len] = 0;

    return pat1;
}

static void test_arena(f64 time)
{
    (void) time;
    Arena_Stack arena_stack = {0};
    arena_init(&arena_stack, 0, 0, "test_arena");
    
    #define PATTERN1 ">HelloWorld(Pattern1)"
    #define PATTERN2 ">GoodbyeWorld(Pattern2)"
    #define PATTERN3 ">****(Pattern3)"

    Arena level1 = arena_acquire(&arena_stack);
    {
        char* pat1 = arena_push_string(&level1, PATTERN1);

        Arena level2 = arena_acquire(&arena_stack);
        {
            char* pat2 = arena_push_string(&level2, PATTERN2);

            Arena level3 = arena_acquire(&arena_stack);
            {
                char* pat3 = arena_push_string(&level3, PATTERN3); (void) pat3;
                pat1 = arena_push_string(&level1, PATTERN1);
            }
            arena_release(&level3);

            TEST(memcmp(pat2, PATTERN2, sizeof PATTERN2 - 1) == 0);
        }

        TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);
        //! No free
    }
    arena_release(&level1);
}