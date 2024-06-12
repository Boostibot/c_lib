#pragma once
#include "arena.h"

static char* arena_push_string(Arena_Frame* arena, const char* string)
{
    size_t len = string ? strlen(string) : 0;   
    char* pat1 = (char*) arena_frame_push(arena, (isize) len + 1, 1);
    memcpy(pat1, string, len);
    pat1[len] = 0;

    return pat1;
}

static void test_arena(f64 time)
{
    (void) time;
    Arena_Stack arena_stack = {0};
    arena_stack_init(&arena_stack, 0, 0, 0, NULL, "test_arena");
    
    #define PATTERN1 ">HelloWorld(Pattern1)"
    #define PATTERN2 ">GoodbyeWorld(Pattern2)"
    #define PATTERN3 ">****(Pattern3)"

    Arena_Frame level1 = arena_frame_acquire(&arena_stack);
    {
        char* pat1 = arena_push_string(&level1, PATTERN1);

        Arena_Frame level2 = arena_frame_acquire(&arena_stack);
        {
            char* pat2 = arena_push_string(&level2, PATTERN2);

            Arena_Frame level3 = arena_frame_acquire(&arena_stack);
            {
                char* pat3 = arena_push_string(&level3, PATTERN3); (void) pat3;
                pat1 = arena_push_string(&level1, PATTERN1);
            }
            arena_frame_release(&level3);

            TEST(memcmp(pat2, PATTERN2, sizeof PATTERN2 - 1) == 0);
            TEST(memcmp(pat2, PATTERN2, sizeof PATTERN2 - 1) == 0);
        }

        TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);
        TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);
        //! No free
    }
    arena_frame_release(&level1);
}