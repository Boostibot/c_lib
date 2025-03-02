#ifndef MODULE_MAP
#define MODULE_MAP

//This is a "generic" map/multimap/set implementation (there is an iterface that supports having multiple of the same key in the map).
//It is not really a full blown map implementation, more of a scaffolding to quickly wrap in your own struct and functions and use that way.
// Simply put its very difficult to make a generic interface that is both convenient and efficient for every possible data type. For example
// we want to store dynamic strings but use string slices for lookup. This can be solved in some ways but I believe the current solution to be 
// simpler and more flexible. Another consideration are lifetimes - sometimes having something akin to destructor is convenient and sometimes not
// having the interface like this helps us to easily use both.
// 
//The interface is in the style of qsort or similar - you pass in some information + function pointers and the functions use those to
// perform a certain action. Calling such function pointers however incurs quite a bit of overhead especially for when the function is really
// small (ie. comparing integers for example). I solve this by marking all functions as __forceinline/__attribute__((always_inline)). 
//This makes the compiler essentially treat these functions as macros and paste the internal code to wherever they are called from. At that point,
// however, the compiler sees that we are assigning some specific function to the function pointer argument and later calling it - it figures out
// it can just call the function directly. Thus, the resulting codegen is identical to if we called a specific function directly.
//Of course, the downside is that if we are not careful this code can get pasted to many many places exploding the code size. Because of this
// I recommend wrapping the functions for a specific type and using those wrappers - it is also a lot more convenient anyway.
//
//Another oddity is that the functions take hash instead of calculating it inside. The hash needs to be escaped using map_hash_escape.  
// Since we are wrapping the functions anyway this doesnt really decrease their convenience. 
// However it allows us to be more efficient because we can often calculate the hash once and use it in
// several functions. The remove function is similar - unlike most remove functions it takes an already found index. This makes the common
// pattern of "find, use, remove" one lookup more efficient.

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

typedef struct Map {
    Allocator* alloc;
    uint8_t* entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t gavestones;
    uint32_t rehashes; //purely informational number of rehashes so far. Can be used as a generation counter of sorts
} Map;

typedef struct Map_Info {
    uint32_t entry_size;
    uint32_t entry_align;
    uint32_t key_offset;
    uint32_t hash_offset;
    void* key_equals; //if null then we trust hashes
} Map_Info;

typedef bool (*Key_Equals_Func)(const void* stored, const void* key);

#if defined(_MSC_VER)
    #define ATTRIBUTE_INLINE_ALWAYS __forceinline
    #define ATTRIBUTE_INLINE_NEVER  __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_INLINE_ALWAYS __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER  __attribute__((noinline))
#else
    #define ATTRIBUTE_INLINE_ALWAYS inline 
    #define ATTRIBUTE_INLINE_NEVER                              
#endif

#ifndef EXTERNAL
    #define EXTERNAL
#endif

#ifndef MAP_INLINE_API
    #define MAP_INLINE_API ATTRIBUTE_INLINE_ALWAYS static 
    #define MODULE_IMPL_INLINE_MAP
#endif

MAP_INLINE_API uint64_t map_hash_escape(uint64_t hash) { return hash < 2 ? hash + 2 : hash; }
MAP_INLINE_API bool     map_hash_is_valid(uint64_t hash) { return hash >= 2; }

//regular interface (requires only key and hash is calculated using info)
MAP_INLINE_API void  map_init(Map* map, Map_Info info, Allocator* alloc);
MAP_INLINE_API void  map_deinit(Map* map, Map_Info info);
MAP_INLINE_API void  map_reserve(Map* map, Map_Info info, isize count);
MAP_INLINE_API void  map_rehash(Map* map, Map_Info info, isize count);
MAP_INLINE_API void  map_remove(Map* map, Map_Info info, isize found);
MAP_INLINE_API void  map_clear(Map* map, Map_Info info);
MAP_INLINE_API bool  map_find(const Map* map, Map_Info info, const void* key, uint64_t hash, isize* found);

MAP_INLINE_API void* map_get_or(const Map* map, Map_Info info, const void* key, uint64_t hash, void* if_not_found);
MAP_INLINE_API void* map_set(Map* map, Map_Info info, const void* value);
MAP_INLINE_API void* map_insert(Map* map, Map_Info info, const void* value);

//Can be used to iterate all entries correspoind to certain key in case of multimap
MAP_INLINE_API bool  map_find_next(const Map* map, Map_Info info, const void* key, uint64_t hash, uint32_t* index, uint32_t* iter); 
MAP_INLINE_API void  map_find_next_make(const Map* map, uint64_t hash, uint32_t* index, uint32_t* iter);

//these functions just do the background work of inserting/inserting or finding without actually storing anything.
// one has to fill in the entry at the returned index/ptr appropriately to keep the map in good state
MAP_INLINE_API isize map_prepare_insert(Map* map, Map_Info info, const void* key, uint64_t hash); 
MAP_INLINE_API bool  map_prepare_insert_or_find(Map* map, Map_Info info, const void* key, uint64_t hash, isize* found);
MAP_INLINE_API bool  map_prepare_insert_or_find_ptr(Map* map, Map_Info info, const void* key, uint64_t hash, void** found);

//iterates all entries of wrapped map
#define MAP_FOR(map, T, entry) \
    for(T* entry = (map).entries; entry < (map).entries + (map).capacity; entry += 1) \
        if(entry->hash >= 2)

#define MAP_TEST_INVARIANTS_BASIC   ((uint32_t) 1)
#define MAP_TEST_INVARIANTS_FIND    ((uint32_t) 2)
#define MAP_TEST_INVARIANTS_ALL     ((uint32_t) -1)
ATTRIBUTE_INLINE_NEVER EXTERNAL void map_test_consistency(const Map* map, Map_Info info, uint32_t flags);
MAP_INLINE_API void map_debug_test_consistency(const Map* map, Map_Info info);
#endif

//Inline implementation
#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_INLINE_MAP)) && !defined(MODULE_HAS_IMPL_INLINE_MAP)
#define MODULE_HAS_IMPL_INLINE_MAP

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif
#ifndef TEST
    #include <stdio.h>
    #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
#endif

#define MAP_EMPTY_ENTRY   0
#define MAP_REMOVED_ENTRY 1

ATTRIBUTE_INLINE_NEVER EXTERNAL void _map_grow_entries(Map* map, isize requested_capacity, uint32_t entry_size, uint32_t entry_align);
ATTRIBUTE_INLINE_NEVER EXTERNAL void _map_rehash(Map* map, isize requested_capacity, uint32_t entry_size, uint32_t entry_align, uint32_t hash_offset);
ATTRIBUTE_INLINE_NEVER EXTERNAL void _map_deinit(Map* map, uint32_t entry_size, uint32_t entry_align);

MAP_INLINE_API void map_debug_test_consistency(const Map* map, Map_Info info)
{
    #ifndef MAP_DEBUG
        #if defined(DO_ASSERTS_SLOW)
            #define MAP_DEBUG 2
        #elif !defined(NDEBUG)
            #define MAP_DEBUG 1
        #else
            #define MAP_DEBUG 0
        #endif
    #endif

    (void) map;
    (void) info;
    #if MAP_DEBUG > 1
        map_test_consistency(map, info, MAP_TEST_INVARIANTS_ALL);
    #elif MAP_DEBUG > 0
        map_test_consistency(map, info, MAP_TEST_INVARIANTS_BASIC);
    #endif
}

MAP_INLINE_API void map_init(Map* map, Map_Info info, Allocator* alloc)
{
    map_deinit(map, info);
    map->alloc = alloc;
}

MAP_INLINE_API void map_deinit(Map* map, Map_Info info)
{
    map_debug_test_consistency(map, info);
    _map_deinit(map, info.entry_size, info.entry_align);
    map_debug_test_consistency(map, info);
}

MAP_INLINE_API void map_rehash(Map* map, Map_Info info, isize requested_capacity)
{
    map_debug_test_consistency(map, info);
    _map_rehash(map, requested_capacity, info.entry_size, info.entry_align, info.hash_offset);
    map_debug_test_consistency(map, info);
}

MAP_INLINE_API void map_reserve(Map* map, Map_Info info, isize requested_capacity)
{
    if(map->capacity*3/4 <= requested_capacity + map->gavestones)
        map_rehash(map, info, requested_capacity);
}

//this is a separate fucntion specifically because it doesnt call map_debug_test_consistency so it can be used
// within map_debug_test_consistency
MAP_INLINE_API bool _map_find_next(const Map* map, Map_Info info, const void* key, uint64_t hash, uint32_t* index, uint32_t* iter)
{
    if(map->count > 0)
        for(;;) {
            ASSERT(*iter <= map->capacity);
            uint8_t* entry = map->entries + info.entry_size**index;
            uint64_t entry_hash = 0; memcpy(&entry_hash, entry + info.hash_offset, sizeof entry_hash);
            if(entry_hash == hash) {
                if(info.key_equals == NULL || ((Key_Equals_Func)info.key_equals)(entry + info.key_offset, key))
                    return true;
            }
            else if(entry_hash == MAP_EMPTY_ENTRY) 
                break;
        
            *index = (*index + *iter) & (map->capacity - 1);
            *iter += 1;
        }
    return false;
}

MAP_INLINE_API void map_find_next_make(const Map* map, uint64_t hash, uint32_t* index, uint32_t* iter)
{
    ASSERT(map_hash_is_valid(hash));
    *iter = 0;
    *index = (uint32_t) hash & (map->capacity - 1);
}
MAP_INLINE_API bool map_find_next(const Map* map, Map_Info info, const void* key, uint64_t hash, uint32_t* index, uint32_t* iter)
{
    ASSERT(map_hash_is_valid(hash));
    map_debug_test_consistency(map, info);
    *index = (*index + *iter) & (map->capacity - 1);
    *iter += 1;
    return _map_find_next(map, info, key, hash, index, iter);
}

MAP_INLINE_API bool map_find(const Map* map, Map_Info info, const void* key, uint64_t hash, isize* found)
{
    ASSERT(map_hash_is_valid(hash));
    map_debug_test_consistency(map, info);
    uint32_t iter = 1;
    uint32_t index = (uint32_t) hash & (map->capacity - 1);
    bool out = _map_find_next(map, info, key, hash, &index, &iter);
    *found = index;
    return out;
}

MAP_INLINE_API void* map_get_or(const Map* map, Map_Info info, const void* key, uint64_t hash, void* if_not_found)
{
    ASSERT(map_hash_is_valid(hash));
    map_debug_test_consistency(map, info);
    uint32_t iter = 1;
    uint32_t index = (uint32_t) hash & (map->capacity - 1);
    if(_map_find_next(map, info, key, hash, &index, &iter))
        return map->entries + info.entry_size*index;
    return if_not_found;
}

MAP_INLINE_API bool _map_insert_or_find(Map* map, Map_Info info, const void* key, uint64_t hash, isize* found, bool do_only_insert)
{
    ASSERT(map_hash_is_valid(hash));
    map_debug_test_consistency(map, info);
    map_reserve(map, info, (isize) map->count + 1);
    uint64_t i = hash & (map->capacity - 1);
    uint64_t empty_i = (uint64_t) -1;

    uint64_t entry_hash = 0;
    uint8_t* entry = NULL;
    for(uint64_t k = 1; ; k++)
    {
        //if(k > map->capacity)
            //LOG_HERE();
        ASSERT(k <= map->capacity);
        entry = map->entries + info.entry_size*i;
        memcpy(&entry_hash, entry + info.hash_offset, sizeof entry_hash);

        //if we are inserting we dont care about duplicates. 
        // We just insert to the first slot where we can 
        if(do_only_insert) {
            if(entry_hash < 2)
                break;
        }
        //if we are inserting or finding, we need to keep iterating until we find
        // a properly empty slot. Only then we can be sure the slot is not in the map.
        //We also keep track of the previous removed slot, so we can store it there 
        // and thus help to clean up the hash map a bit.
        else {
            if(entry_hash == hash) {
                if(info.key_equals == NULL || ((Key_Equals_Func)info.key_equals)(entry + info.key_offset, key))
                {
                    *found = i;
                    return true;
                }
            }
            else if(entry_hash == MAP_EMPTY_ENTRY) {
                if(empty_i != (uint64_t) -1)
                    i = empty_i;
                break; 
            }
            else if(entry_hash == MAP_REMOVED_ENTRY)
                empty_i = i;
        }
            
        i = (i + k) & (map->capacity - 1);
    }

    //update hash part
    ASSERT(entry_hash != MAP_REMOVED_ENTRY || map->gavestones > 0);
    map->gavestones -= entry_hash == MAP_REMOVED_ENTRY;
    map->count += 1;
    *found = i;
    return false;
}

MAP_INLINE_API isize map_prepare_insert(Map* map, Map_Info info, const void* key, uint64_t hash)
{
    isize found = 0;
    _map_insert_or_find(map, info, key, hash, &found, true);
    return found;
}

MAP_INLINE_API bool map_prepare_insert_or_find(Map* map, Map_Info info, const void* key, uint64_t hash, isize* found)
{
    return _map_insert_or_find(map, info, key, hash, found, false);
}


MAP_INLINE_API bool map_prepare_insert_or_find_ptr(Map* map, Map_Info info, const void* key, uint64_t hash, void** found)
{
    isize index = 0;
    bool out = _map_insert_or_find(map, info, key, hash, &index, false);
    *found = map->entries + info.entry_size*index;
    return out;
}

MAP_INLINE_API void* map_insert(Map* map, Map_Info info, const void* value)
{
    isize found = 0;
    uint8_t* entry = (uint8_t*) value;
    uint64_t entry_hash = 0; 
    memcpy(&entry_hash, entry + info.hash_offset, sizeof entry_hash);
    _map_insert_or_find(map, info, entry + info.key_offset, entry_hash, &found, true);
    uint8_t* found_entry = map->entries + info.entry_size*found;
    memcpy(found_entry, entry, info.entry_size);
    return found_entry;
}

MAP_INLINE_API void* map_set(Map* map, Map_Info info, const void* value)
{
    isize found = 0;
    uint8_t* entry = (uint8_t*) value;
    uint64_t entry_hash = 0; 
    memcpy(&entry_hash, entry + info.hash_offset, sizeof entry_hash);
    _map_insert_or_find(map, info, entry + info.key_offset, entry_hash, &found, false);
    uint8_t* found_entry = map->entries + info.entry_size*found;
    memcpy(found_entry, entry, info.entry_size);
    return found_entry;
}

MAP_INLINE_API void map_remove(Map* map, Map_Info info, isize found)
{
    ASSERT(found < map->capacity);
    uint8_t* entry = map->entries + info.entry_size*found;
    uint64_t removed = MAP_REMOVED_ENTRY;
    #if ASSERT_LEVEL > 0
        memset(entry, -1, info.entry_size); //debug
    #endif
    memcpy(entry + info.hash_offset, &removed, sizeof removed);
    map->count -= 1;
    map->gavestones += 1;
}

MAP_INLINE_API void map_clear(Map* map, Map_Info info)
{
    memset(map->entries, 0, map->capacity*info.entry_size);
    map->count = 0;
    map->gavestones = 0;
    map->rehashes += 1; //should it be here?
}

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_MAP)) && !defined(MODULE_HAS_IMPL_MAP)
#define MODULE_HAS_IMPL_MAP

inline static void* _map_alloc(Allocator* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
{
    #ifndef USE_MALLOC
        return (*alloc)(alloc, 0, new_size, old_ptr, old_size, align, NULL);
    #else
        if(new_size != 0) {
            void* out = realloc(old_ptr, new_size);
            TEST(out);
            return out;
        }
        else
            free(old_ptr);
    #endif
}

ATTRIBUTE_INLINE_NEVER 
EXTERNAL void _map_rehash(Map* map, isize requested_capacity, uint32_t entry_size, uint32_t entry_align, uint32_t hash_offset)
{
    TEST(requested_capacity <= UINT32_MAX);
    
    //Unless there are many many gravestones, count them into the lest size.
    //This prevents a porblem where if the map has 11 entries and one removed entry, we will rehash
    // to the same capacity (16). If we then insert an item, remove an item we are back where we were.
    //Essentially for some unlucky sizes (one before the rehash will triger) there would be a rehash on
    // every second operation - this is of course very bad for perf
    isize least_size = map->gavestones > map->count ? map->count : map->gavestones + map->count;
    if(least_size < requested_capacity)
        least_size = requested_capacity;

    isize new_cap = 16;
    while(new_cap*3/4 <= least_size)
        new_cap *= 2;
    
    // allocate new slots and set all to empty
    uint64_t new_mask = (uint64_t) new_cap - 1;
    uint8_t* new_entries = (uint8_t*) _map_alloc(map->alloc, new_cap*entry_size, NULL, 0, entry_align);
    memset(new_entries, 0, new_cap*entry_size); 

    //copy over slots entries
    for(isize j = 0; j < map->capacity; j++)
    {
        uint8_t* entry = map->entries + entry_size*j;
        uint64_t hash = 0; memcpy(&hash, entry + hash_offset, sizeof hash);
        if(hash >= 2)
        {
            uint64_t i = hash & new_mask;
            for(uint64_t k = 1; ; k++) {
                ASSERT(k <= (uint64_t) new_cap);
                uint8_t* new_entry = new_entries + entry_size*i;
                uint64_t new_hash = 0; memcpy(&new_hash, new_entry + hash_offset, sizeof new_hash);
                if(new_hash == MAP_REMOVED_ENTRY || new_hash == MAP_EMPTY_ENTRY) {
                    memcpy(new_entry, entry, entry_size);
                    break;
                }
                    
                i = (i + k) & new_mask;
            }           
        }
    }
    
    _map_alloc(map->alloc, 0, map->entries, map->capacity*entry_size, entry_align);
    map->entries = new_entries;
    map->capacity = (uint32_t) new_cap;
    map->gavestones = 0;
    map->rehashes += 1;
}

ATTRIBUTE_INLINE_NEVER 
EXTERNAL void _map_deinit(Map* map, uint32_t entry_size, uint32_t entry_align)
{
    if(map->capacity > 0) 
        _map_alloc(map->alloc, 0, map->entries, map->capacity*entry_size, entry_align);
    memset(map, 0, sizeof* map);
}

ATTRIBUTE_INLINE_NEVER
EXTERNAL void map_test_consistency(const Map* map, Map_Info info, uint32_t flags)
{
    if(flags & MAP_TEST_INVARIANTS_BASIC) {
        if(map->alloc == NULL) {
            Map null = {0};
            TEST(memcmp(map, &null, sizeof *map) == 0);
        }
        else {
            TEST(map->capacity < (uint32_t) -2);
            TEST(map->count + map->gavestones <= map->capacity*3/4);
            TEST((map->capacity == 0) == (map->entries == NULL));
        }
    }

    if(flags & MAP_TEST_INVARIANTS_FIND) {
        isize found_count = 0;
        for(uint32_t i = 0; i < map->capacity; i++)
        {
            uint8_t* entry = map->entries + info.entry_size*i;
            uint8_t* key = entry + info.key_offset;
            uint64_t hash = 0; memcpy(&hash, entry + info.hash_offset, sizeof hash);

            if(hash >= 2) {
                uint32_t iter = 1;
                uint32_t index = (uint32_t) hash & (map->capacity - 1);
                bool found_self = false;
                while(_map_find_next(map, info, key, hash, &index, &iter)) {
                    if(index == i) {
                        found_self = true;
                        break;
                    }

                    index = (index + iter) & (map->capacity - 1);
                    iter += 1;
                }

                TEST(found_self);
                found_count += 1;
            }
        }

        TEST(map->count == found_count);
        TEST(map->count == found_count);
    }
}
#endif
