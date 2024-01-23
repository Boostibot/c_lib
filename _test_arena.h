#pragma once
#include "arena.h"

static char* arena_push_string(Arena* arena, i32 level, const char* string)
{
    isize len = string ? strlen(string) : 0;   
    char* pat1 = (char*) arena_push(arena, level, len + 1, 1);
    memcpy(pat1, string, len);
    pat1[len] = 0;

    return pat1;
}

static void test_arena(f64 time)
{
    (void) time;
    Arena arena = {0};

    arena_init(&arena, 0, 0);
    
    #define PATTERN1 ">HelloWorld(Pattern1)"
    #define PATTERN2 ">GoodbyeWorld(Pattern2)"
    #define PATTERN3 ">****(Pattern3)"

    i32 level1 = arena_get_level(&arena);
    {
        char* pat1 = arena_push_string(&arena, level1, PATTERN1);

        i32 level2 = arena_get_level(&arena);
        {
            char* pat2 = arena_push_string(&arena, level2, PATTERN2);

            i32 level3 = level2 + 10;
            {
                char* pat3 = arena_push_string(&arena, level3, PATTERN3); (void) pat3;
                pat1 = arena_push_string(&arena, level1, PATTERN1);
            }
            arena_pop(&arena, level3);

            TEST(memcmp(pat2, PATTERN2, sizeof PATTERN2 - 1) == 0);
        }

        TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);
        //! No free
    }
    arena_pop(&arena, level1);
}