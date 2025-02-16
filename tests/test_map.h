



#include "../assert.h"
#include "../map.h"

#include "../hash_string.h"
#include "../defines.h"
#include "../assert.h"
#include "../random.h"
#include "../allocator_debug.h"
#include "../array.h"
#include "../time.h"
#include "../scratch.h"

//Make specialization of the map and test it.
//This specialization is a little bloated because we want to test everything.
//Usually one would support only get/set style or
//insert/test_string_map_find_iterate multimap style. This does both which
//also makes testing a bit difficult (we need to know which entry was set 
// when there are multiple with the same key etc.)
typedef struct Test_String_Map_Entry {
    uint64_t hash;
    String key;
    String value;
} Test_String_Map_Entry;

typedef union Test_String_Map {
    Map generic;
    struct {
        Allocator* alloc;
        Test_String_Map_Entry* entries;
        uint32_t count;
        uint32_t capacity;
    };
} Test_String_Map;

typedef struct Test_String_Map_Find_Iter {
    Test_String_Map_Entry* entry;
    uint64_t hash;
    uint32_t index;            
    uint32_t iteration;            
} Test_String_Map_Find_Iter;

static Test_String_Map_Entry* test_string_map_insert(Test_String_Map* map, String key, String value);
static Test_String_Map_Entry* test_string_map_set(Test_String_Map* map, String key, String value);   
static Test_String_Map_Entry* test_string_map_get(const Test_String_Map* map, String string); 
static bool test_string_map_remove(Test_String_Map* map, Test_String_Map_Entry* entry);
static void test_string_map_clear(Test_String_Map* map);
static void test_string_map_deinit(Test_String_Map* map);
static void test_string_map_init(Test_String_Map* map, Allocator* alloc);
static bool test_string_map_find_iterate(const Test_String_Map* map, String string, Test_String_Map_Find_Iter* iter);  
static isize test_string_map_remove_all(Test_String_Map* map, String string);
static void test_string_map_test_invariants(const Test_String_Map* map);

#define MY_MAP_INFO SINIT(Map_Info) {           \
        sizeof(Test_String_Map_Entry),          \
        __alignof(Test_String_Map_Entry),       \
        offsetof(Test_String_Map_Entry, key),   \
        offsetof(Test_String_Map_Entry, hash),  \
        (void*) string_is_equal_ptrs            \
    }                                           \

static void _my_entry_deinit(Test_String_Map* map, Test_String_Map_Entry* entry)
{
    string_deallocate(map->alloc, &entry->key);
    string_deallocate(map->alloc, &entry->value);
}

static Test_String_Map_Entry* test_string_map_insert(Test_String_Map* map, String key, String value)
{ 
    Test_String_Map_Entry entry = {0};
    entry.key = string_allocate(map->alloc, key);
    entry.value = string_allocate(map->alloc, value);
    entry.hash = map_hash_escape(hash_string(key));
    return (Test_String_Map_Entry*) map_insert(&map->generic, MY_MAP_INFO, &entry);
}

static Test_String_Map_Entry* test_string_map_set(Test_String_Map* map, String key, String value)       
{ 
    uint64_t hash = map_hash_escape(hash_string(key));
    Test_String_Map_Entry* entry = NULL;
    if(map_prepare_insert_or_find_ptr(&map->generic, MY_MAP_INFO, &key, hash, (void**) &entry))
        string_reallocate(map->alloc, &entry->value, value);
    else {
        entry->hash = hash;
        entry->key = string_allocate(map->alloc, key);
        entry->value = string_allocate(map->alloc, value);
    }

    map_debug_test_invariant(&map->generic, MY_MAP_INFO);
    return entry; 
}

static Test_String_Map_Entry* test_string_map_get(const Test_String_Map* map, String string)           
{
    uint64_t hash = map_hash_escape(hash_string(string));
    return (Test_String_Map_Entry*) map_get_or(&map->generic, MY_MAP_INFO, &string, hash, NULL);
}

static bool test_string_map_remove(Test_String_Map* map, Test_String_Map_Entry* entry)
{
    if(entry == NULL)
        return false;
    _my_entry_deinit(map, entry);
    map_remove(&map->generic, MY_MAP_INFO, entry - map->entries);
    return true;
}

static void test_string_map_clear(Test_String_Map* map)
{
    MAP_FOR(*map, Test_String_Map_Entry, entry) 
        _my_entry_deinit(map, entry);
    map_clear(&map->generic, MY_MAP_INFO);
    map_debug_test_invariant(&map->generic, MY_MAP_INFO);
}

static void test_string_map_deinit(Test_String_Map* map)
{
    MAP_FOR(*map, Test_String_Map_Entry, entry) 
        _my_entry_deinit(map, entry);
    map_deinit(&map->generic, MY_MAP_INFO);
}

static void test_string_map_init(Test_String_Map* map, Allocator* alloc)
{
    test_string_map_deinit(map);
    map_init(&map->generic, MY_MAP_INFO, alloc);
}

static bool test_string_map_find_iterate(const Test_String_Map* map, String string, Test_String_Map_Find_Iter* iter)    
{ 
    if(iter->iteration == 0) {
        Test_String_Map_Find_Iter out = {0};
        out.hash = map_hash_escape(hash_string(string));
        map_find_next_make(&map->generic, out.hash, &out.index, &out.iteration);
        *iter = out;
    }

    bool out = map_find_next(&map->generic, MY_MAP_INFO, &string, iter->hash, &iter->index, &iter->iteration);
    iter->entry = map->entries + iter->index;
    return out;
}

static isize test_string_map_remove_all(Test_String_Map* map, String string)
{
    isize removed = 0;
    for(Test_String_Map_Find_Iter it = {0}; test_string_map_find_iterate(map, string, &it); )
    {
        ASSERT(it.entry != NULL);
        removed += test_string_map_remove(map, it.entry);
    }
    map_debug_test_invariant(&map->generic, MY_MAP_INFO);
    return removed;
}

static void test_string_map_test_invariants(const Test_String_Map* map)
{
    map_test_invariant(&map->generic, MY_MAP_INFO, MAP_TEST_INVARIANTS_ALL);
}

INTERNAL void test_string_map_unit()
{
    Debug_Allocator debug = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK | DEBUG_ALLOC_CAPTURE_CALLSTACK);
    {
        Test_String_Map map = {0};
        test_string_map_init(&map, debug.alloc);
        
        test_string_map_set(&map, STRING("AAA"), STRING("A"));
        test_string_map_set(&map, STRING("BBB"), STRING("B"));
        test_string_map_set(&map, STRING("BBB"), STRING("C"));

        TEST(map.count == 2);
        {
            Test_String_Map_Entry* e1 = test_string_map_get(&map, STRING("AAA"));
            Test_String_Map_Entry* e2 = test_string_map_get(&map, STRING("BBB"));
            Test_String_Map_Entry* e3 = test_string_map_get(&map, STRING("CCC"));
            TEST(e1 && string_is_equal(e1->key, STRING("AAA")) && string_is_equal(e1->value, STRING("A")));
            TEST(e2 && string_is_equal(e2->key, STRING("BBB")) && string_is_equal(e2->value, STRING("C")));
            TEST(e3 == NULL);
        }
        
        test_string_map_remove_all(&map, STRING("BBB"));
        TEST(test_string_map_get(&map, STRING("BBB")) == NULL);

        test_string_map_set(&map, STRING("BBB"), STRING("B"));
        test_string_map_set(&map, STRING("CCC"), STRING("C1"));
        TEST(map.count == 3);
        
        //inserting duplicit keys
        test_string_map_insert(&map, STRING("CCC"), STRING("C2"));
        test_string_map_insert(&map, STRING("CCC"), STRING("C3"));
        test_string_map_insert(&map, STRING("CCC"), STRING("C4"));
        test_string_map_insert(&map, STRING("CCC"), STRING("C5"));
        TEST(map.count == 7);

        //try and force rehash just to test it out
        for(int i = 0; i < 100; i++)
            test_string_map_insert(&map, STRING("REHASH_PLS"), STRING(""));
        TEST(map.count == 107);
        test_string_map_remove_all(&map, STRING("REHASH_PLS"));
        TEST(map.count == 7);

        //try to find all duplicit keys
        uint32_t found = 0;
        for(Test_String_Map_Find_Iter it = {0}; test_string_map_find_iterate(&map, STRING("CCC"), &it); )
        {
            TEST(string_is_equal(it.entry->key, STRING("CCC")));
            if(0) {}
            else if(string_is_equal(it.entry->value, STRING("C1"))) found |= 1u << 0u;
            else if(string_is_equal(it.entry->value, STRING("C2"))) found |= 1u << 1u;
            else if(string_is_equal(it.entry->value, STRING("C3"))) found |= 1u << 2u;
            else if(string_is_equal(it.entry->value, STRING("C4"))) found |= 1u << 3u;
            else if(string_is_equal(it.entry->value, STRING("C5"))) found |= 1u << 4u;
            else TEST(false);
        }
        TEST(found == (1u << 5u) - 1);

        test_string_map_clear(&map);
        TEST(map.count == 0);

        test_string_map_deinit(&map);
    }
    debug_allocator_deinit(&debug);
}

static String_Builder random_lorem_ipsum(Allocator* alloc, isize len)
{
    static const char* LOREM_IPSUM_WORDS[] = {
        "Lorem", "ipsum", "dolor", "sit", "amet,", "consectetur", "adipiscing", "elit.", "Etiam", "mattis", "sem", "vitae", 
        "elit", "efficitur", "ultricies.", "Phasellus", "luctus", "blandit", "libero", "eu", "ultricies.", "Phasellus", "a", 
        "tempus", "nisl,", "id", "lobortis", "urna.", "Pellentesque", "rutrum,", "nunc", "id", "accumsan", "convallis,", "metus", 
        "velit", "commodo", "est,", "vel", "condimentum", "turpis", "eros", "eget", "magna.", "Praesent", "aliquam", "aliquam", 
        "dolor,", "in", "cursus", "ipsum", "condimentum", "id.", "Vivamus", "et", "cursus", "ante.", "Donec", "pretium", "metus", 
        "sit", "amet", "pharetra", "porta."
    };

    String_Builder out = builder_make(alloc, 0);
    for(isize i = 0; i < len; i++)
    {
        if(i > 0)
            builder_push(&out, ' ');

        uint64_t rand_word_i = random_u64() % ARRAY_COUNT(LOREM_IPSUM_WORDS); 
        String rand_word = string_of(LOREM_IPSUM_WORDS[rand_word_i]);
        builder_append(&out, rand_word);
    }

    return out;
}

static int _qsort_string_compare(const void* a, const void* b)
{
    return string_compare(*(String*) a, *(String*) b);
}

INTERNAL void test_string_map_stress(f64 max_seconds)
{
    Debug_Allocator debug = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK);
    Debug_Allocator truth_alloc = debug_allocator_make(allocator_get_default(), DEBUG_ALLOC_LEAK_CHECK);
	{
		typedef enum {
			REINIT,
			CLEAR,
			INSERT,
			INSERT_DUPLICIT,
			SET,
			SET_DUPLICIT,
			REMOVE,
			REMOVE_ALL_WITH_KEY,
			REMOVE_ALL_WITH_BAD_KEY,
		} Action;

		Discrete_Distribution dist[] = {
			{REINIT,			1},
			{CLEAR,				1},
			{INSERT,	        1000},
			{INSERT_DUPLICIT,	1000},
			{SET,	            2500},
			{SET_DUPLICIT,	    2500},
			{REMOVE,			50},
			{REMOVE_ALL_WITH_KEY,   50},
			{REMOVE_ALL_WITH_BAD_KEY,   10},
		};
        random_discrete_make(dist, ARRAY_COUNT(dist));

		enum {
			MIN_ITERS = 50, //for debugging
			NON_EXISTANT_KEYS_CHECKS = 2,
		};
		
		typedef Array(String) String_Array_;

		//We store everything twice to allow us to test copy operation by coping the current state1 into state2 
		// (or vice versa) and continuing working with the copied data (by swapping the structs)
		String_Array_ truth_val_array = {debug.alloc};
		String_Array_ truth_key_array = {debug.alloc};

        Test_String_Map map = {0};
        test_string_map_init(&map, debug.alloc);
		//uint64_t random_seed = random_clock_seed();
		// uint64_t random_seed = 0;
		// *random_state() = random_state_make(random_seed);

		isize max_size = 0;
		isize max_capacity = 0;
		double start = clock_sec();
		for(isize z = 0; ; z++)
		{
			Action action = (Action) random_discrete(dist, ARRAY_COUNT(dist));
			if(clock_sec() - start >= max_seconds && z >= MIN_ITERS)
            {
                test_string_map_init(&map, debug.alloc);
                for(isize i = 0; i < truth_key_array.count; i++) {
                    string_deallocate(truth_alloc.alloc, &truth_key_array.data[i]);
                    string_deallocate(truth_alloc.alloc, &truth_val_array.data[i]);
                }
                array_clear(&truth_key_array);
                array_clear(&truth_val_array);
                break;
            }

            SCRATCH_SCOPE(arena_outer)
            {
            String_Builder lorem_key = random_lorem_ipsum(arena_outer.alloc, random_range(0, 10));
            String_Builder lorem_val = random_lorem_ipsum(arena_outer.alloc, random_range(0, 10));
			switch(action)
			{
				case REINIT: {
                    test_string_map_init(&map, debug.alloc);
					for(isize i = 0; i < truth_key_array.count; i++) {
						string_deallocate(truth_alloc.alloc, &truth_key_array.data[i]);
						string_deallocate(truth_alloc.alloc, &truth_val_array.data[i]);
					}
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);
				} break;
					
				case CLEAR: {
					test_string_map_clear(&map);
					for(isize i = 0; i < truth_key_array.count; i++) {
						string_deallocate(truth_alloc.alloc, &truth_key_array.data[i]);
						string_deallocate(truth_alloc.alloc, &truth_val_array.data[i]);
					}
					array_clear(&truth_key_array);
					array_clear(&truth_val_array);
				} break;

				case INSERT: 
                case INSERT_DUPLICIT: {
					String key = lorem_key.string;
					String val = lorem_val.string;

					if(action == INSERT_DUPLICIT && truth_key_array.count > 0) {
                        key = truth_key_array.data[random_range(0, truth_key_array.count)];
                        val = truth_val_array.data[random_range(0, truth_key_array.count)];
                    }

					array_push(&truth_key_array, string_allocate(truth_alloc.alloc, key));
					array_push(&truth_val_array, string_allocate(truth_alloc.alloc, val));

                    test_string_map_insert(&map, key, val);
					TEST(test_string_map_get(&map, key) != NULL);
				} break;
					
                
                case REMOVE: {
					if(truth_key_array.count > 0) {
                        String removed_key = truth_key_array.data[random_range(0, truth_key_array.count)];
                        removed_key = string_allocate(arena_outer.alloc, removed_key);

                        //we need to make sure we are removing the same entry
                        // test_string_map_remove is removing so we first try to find the one
                        // which test_string_map_remove will remove, remove it and also remove it
                        // from truth
                        Test_String_Map_Entry* entry = test_string_map_get(&map, removed_key);
                        TEST(entry != NULL && string_is_equal(entry->key, removed_key));
                        
                        bool key_val_found = false;
                        for(isize j = 0; j < truth_key_array.count; j++) {
                            if(string_is_equal(truth_key_array.data[j], entry->key) && string_is_equal(truth_val_array.data[j], entry->value)) {
                                SWAP(&truth_key_array.data[j], array_last(truth_key_array));
                                SWAP(&truth_val_array.data[j], array_last(truth_val_array));
                                String key = array_pop(&truth_key_array);
                                String val = array_pop(&truth_val_array);

                                string_deallocate(truth_alloc.alloc, &key);
                                string_deallocate(truth_alloc.alloc, &val);
                                key_val_found = true;
                                break;
                            }
                        }   
                        TEST(test_string_map_remove(&map, test_string_map_get(&map, removed_key)));
                        TEST(key_val_found);
                    }
				} break;

                case SET: 
                case SET_DUPLICIT: {
					String key = lorem_key.string;
					String val = lorem_val.string;

					if(action == SET_DUPLICIT && truth_key_array.count > 0) {
                        key = truth_key_array.data[random_range(0, truth_key_array.count)];
                        val = truth_val_array.data[random_range(0, truth_key_array.count)];

                        key = string_allocate(arena_outer.alloc, key);
                        val = string_allocate(arena_outer.alloc, val);
                    }

                    //same as with remove
                    Test_String_Map_Entry* entry = test_string_map_get(&map, key);

                    bool key_found = false;
                    for(isize j = 0; j < truth_key_array.count; j++) {
                        if(entry && string_is_equal(truth_key_array.data[j], key) && string_is_equal(truth_val_array.data[j], entry->value)) {
                            string_deallocate(truth_alloc.alloc, &truth_val_array.data[j]);
                            truth_val_array.data[j] = string_allocate(truth_alloc.alloc, val);
                            key_found = true;
                            break;
                        }
                    }

                    TEST((entry != NULL) == key_found);
                    if(key_found == false) {
                        array_push(&truth_key_array, string_allocate(truth_alloc.alloc, key));
                        array_push(&truth_val_array, string_allocate(truth_alloc.alloc, val));
                    }
                    
                    test_string_map_set(&map, key, val);
					TEST(test_string_map_get(&map, key) != NULL);
				} break;

				case REMOVE_ALL_WITH_KEY: 
				case REMOVE_ALL_WITH_BAD_KEY: {
                    String removed_key = lorem_key.string;
					if(action == REMOVE_ALL_WITH_KEY && truth_key_array.count > 0)
					{
                        removed_key = truth_key_array.data[random_range(0, truth_key_array.count)];
                        removed_key = string_allocate(arena_outer.alloc, removed_key);
					}

                    isize removed_hash_count = test_string_map_remove_all(&map, removed_key);
					TEST(test_string_map_get(&map, removed_key) == NULL);

                    isize removed_truth_count = 0;
                    for(isize j = 0; j < truth_key_array.count; j++)
                        if(string_is_equal(truth_key_array.data[j], removed_key))
                        {
                            SWAP(&truth_key_array.data[j], array_last(truth_key_array));
                            SWAP(&truth_val_array.data[j], array_last(truth_val_array));
                            String key = array_pop(&truth_key_array);
                            String val = array_pop(&truth_val_array);

                            string_deallocate(truth_alloc.alloc, &key);
                            string_deallocate(truth_alloc.alloc, &val);
                            j -= 1;
                            removed_truth_count += 1;
                        }

                    TEST(removed_truth_count == removed_hash_count);
                    TEST(removed_truth_count == removed_hash_count);
				} break;
			}

			if(max_size < map.count)
				max_size = map.count;
			if(max_capacity < map.capacity)
				max_capacity = map.capacity;
				
			test_string_map_test_invariants(&map);
			ASSERT(truth_key_array.count == truth_val_array.count);
			TEST(truth_key_array.count == map.count);
				
			//Find every single key. 
			for(isize k = 0; k < truth_key_array.count; k++)
			{
				String key = truth_key_array.data[k];
				SCRATCH_SCOPE(arena)
				{
					String_Array_ truth_found = {arena.alloc};
					String_Array_ hash_found = {arena.alloc};
						
					for(isize j = 0; j < truth_key_array.count; j++)
						if(string_is_equal(truth_key_array.data[j], key))
							array_push(&truth_found, truth_val_array.data[j]);

                    for(Test_String_Map_Find_Iter it = {0}; test_string_map_find_iterate(&map, key, &it); )
                        array_push(&hash_found, it.entry->value);

					TEST(hash_found.count == truth_found.count);
					if(hash_found.count > 1)
					{
						qsort(hash_found.data, hash_found.count, sizeof *hash_found.data, _qsort_string_compare);
						qsort(truth_found.data, truth_found.count, sizeof *truth_found.data, _qsort_string_compare);
					}

					for(isize l = 0; l < hash_found.count; l++)
                    {
                        String truth = truth_found.data[l];
                        String hash = hash_found.data[l];
                        if(string_is_equal(truth, hash) == false)
                        {
					        for(isize j = 0; j < hash_found.count; j++)
                                LOG_ERROR(">DEBUG", "truth: %.*s", STRING_PRINT(truth_found.data[j]));

					        for(isize j = 0; j < hash_found.count; j++)
                                LOG_ERROR(">DEBUG", "hash:  %.*s", STRING_PRINT(hash_found.data[j]));
                        }

					    TEST(string_is_equal(truth, hash));
					    TEST(string_is_equal(truth, hash));
                    }
				}
			}

			//Test integrity of some non existant keys
			for(isize k = 0; k < NON_EXISTANT_KEYS_CHECKS; k++)
			{
                String_Builder key = random_lorem_ipsum(truth_alloc.alloc, random_range(0, 20));
            
                //Only if the genrated key is unique 
                //(again extrenely statistically unlikely that it will fail truth_key_array.count/10^19 chance)
                bool key_found = false;
                for(isize j = 0; j < truth_key_array.count; j++)
                    if(string_is_equal(truth_key_array.data[j], key.string))
                    {
                        key_found = true;
                        break;
                    }

				TEST(key_found || test_string_map_get(&map, key.string) == NULL);
                builder_deinit(&key);
			}
            }
		}

		array_deinit(&truth_val_array);
		array_deinit(&truth_key_array);
	}

	debug_allocator_deinit(&truth_alloc);
	debug_allocator_deinit(&debug);
}

INTERNAL void test_map(f64 max_seconds)
{
	test_string_map_unit();
	test_string_map_stress(max_seconds);
}