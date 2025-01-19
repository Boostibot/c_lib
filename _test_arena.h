#pragma once
#include "scratch.h"
#include "random.h"
#include "time.h"

static char* arena_push_string(Scratch* arena, const char* string)
{
    size_t len = string ? strlen(string) : 0;   
    char* pat1 = scratch_push(arena, (isize) len + 1, char);
    memcpy(pat1, string, len);
    pat1[len] = 0;

    return pat1;
}

static void test_arena_unit()
{
    const char PATTERN1[] = ">HelloWorld(Pattern1)";
    const char PATTERN2[] = ">GoodbyeWorld(Pattern2)";
    const char PATTERN3[] = ">****(Pattern3)";
    
    Scratch_Arena arena_stack = {0};
    scratch_arena_init(&arena_stack, "test_arena", 0, 0, 0);

    {
        Scratch level1 = scratch_acquire(&arena_stack);
        arena_push_string(&level1, PATTERN1);
        scratch_release(&level1);
    }

    {
        Scratch level1 = scratch_acquire(&arena_stack);
        {
            char* pat1 = arena_push_string(&level1, PATTERN1);
            TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);

            Scratch level2 = scratch_acquire(&arena_stack);
            {
                char* pat2 = arena_push_string(&level2, PATTERN2);
                TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);

                //not a fall (due to multiplex)
                char* pat1_2 = arena_push_string(&level1, PATTERN1);
                TEST(memcmp(pat1_2, PATTERN1, sizeof PATTERN1 - 1) == 0);
                TEST(SCRATCH_ARENA_CHANNELS != 2 || arena_stack.fall_count == 0);

                Scratch level3 = scratch_acquire(&arena_stack);
                {
                    char* pat3 = arena_push_string(&level3, PATTERN3); (void) pat3;
                    TEST(SCRATCH_ARENA_CHANNELS != 2 || arena_stack.fall_count == 0);
                    
                    //fall!
                    char* pat1_3 = arena_push_string(&level1, PATTERN1);
                    TEST(SCRATCH_ARENA_CHANNELS != 2 || arena_stack.fall_count == 1);
                
                    Scratch level4 = scratch_acquire(&arena_stack);
                    {
                        TEST(SCRATCH_ARENA_CHANNELS != 2 || arena_stack.rise_count == 0);
                        Scratch level5 = scratch_acquire(&arena_stack);
                        {
                            //Rise!
                            arena_push_string(&level5, PATTERN3);
                            TEST(SCRATCH_ARENA_CHANNELS != 2 || arena_stack.rise_count == 1);
                            TEST(memcmp(pat1,   PATTERN1, sizeof PATTERN1 - 1) == 0);
                            TEST(memcmp(pat1_2, PATTERN1, sizeof PATTERN1 - 1) == 0);
                            TEST(memcmp(pat1_3, PATTERN1, sizeof PATTERN1 - 1) == 0);
                        }
                        //missing release!
                    }
                    scratch_release(&level4);

                    char* pat3_2 = arena_push_string(&level3, PATTERN3);
                    TEST(memcmp(pat3, PATTERN3, sizeof PATTERN3 - 1) == 0);
                    TEST(memcmp(pat3_2, PATTERN3, sizeof PATTERN3 - 1) == 0);
                }
                scratch_release(&level3);

                TEST(memcmp(pat2, PATTERN2, sizeof PATTERN2 - 1) == 0);
                TEST(memcmp(pat2, PATTERN2, sizeof PATTERN2 - 1) == 0);
            }

            TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);
            TEST(memcmp(pat1, PATTERN1, sizeof PATTERN1 - 1) == 0);
            //! No free
        }
        scratch_release(&level1);
    }

    //Same thing with the macro
    SCRATCH_SCOPE(level1)
        arena_push_string(&level1, PATTERN1);

    scratch_arena_deinit(&arena_stack);
}

static void test_arena_stress(f64 time)
{
    enum {
		MAX_ITERS = 1000*1000*10,
		MIN_ITERS = 100,

        MAX_SIZE = 1024*256,
        MAX_ALIGN_LOG2 = 10,

        MAX_LEVELS = 256
    };

    enum Action {
        ACQUIRE,
        RELEASE,
        ALLOCATE,
        ACTION_ENUM_COUNT
    };
    
    Discrete_Distribution dist[] = {
        {ACQUIRE, 5},
        {RELEASE, 1},
        {ALLOCATE, 5},
    };
	random_discrete_make(dist, ARRAY_LEN(dist));

    Scratch_Arena arena_stack = {0};
    scratch_arena_init(&arena_stack, "test_arena", 0, 0, MAX_LEVELS);
    
    Scratch frames[MAX_LEVELS] = {0};
    isize levels = 0;
    
	// uint64_t random_seed = 0x6b3979953b41cf7d;
	// *random_state() = random_state_make(random_seed);

    f64 start = clock_s();
	for(isize i = 0; i < MAX_ITERS; i++)
	{
		if(clock_s() - start >= time && i >= MIN_ITERS)
			break;

		isize action = random_discrete(dist, ARRAY_LEN(dist));
        if(levels <= 0)
            action = ACQUIRE;
        else if(levels >= MAX_LEVELS && action == ACQUIRE)
            continue;

        switch(action)
        {
            case ACQUIRE: {
                if(levels < MAX_LEVELS)
                    frames[levels++] = scratch_acquire(&arena_stack);
            } break;
            
            case RELEASE: {
                if(levels > 0)
                {
                    isize level = random_range(0, levels);
                    scratch_release(&frames[level]);
                    levels = level;
                }
            } break;
            
            case ALLOCATE: {
                if(levels > 0)
                {
                    isize level = random_range(0, levels);
                    isize size = random_range(0, MAX_SIZE);
                    isize align = 1LL << random_range(0, MAX_ALIGN_LOG2);

                    //@TODO: verify the contents of the allocation
                    scratch_push_generic(&frames[level], size, align, NULL);
                }
            } break;
        }

        scratch_arena_test_invariants(&arena_stack);
    }

    scratch_arena_deinit(&arena_stack);
}

ATTRIBUTE_INLINE_NEVER 
void test_arena_assembly()
{
    SCRATCH_SCOPE(arena)
        scratch_push_nonzero(&arena, 200, void*);
}

static void test_arena(f64 time)
{
    test_arena_unit();
    test_arena_stress(time);
    test_arena_assembly();
}