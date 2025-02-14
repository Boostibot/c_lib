#ifndef MODULE_MAP
#define MODULE_MAP

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

typedef struct Map_Info {
    uint32_t entry_size;
    uint32_t entry_align;
    uint32_t key_offset;
    void* key_equals; //if null then we trust hashes
    void* key_hash; //if null then can only use the hash interface
} Map_Info;

typedef bool     (*Key_Equals_Func)(const void* stored, const void* key);
typedef uint64_t (*Key_Hash_Func)(const void* key);

typedef struct Map_Slot {
    uint64_t hash;
    uint32_t index;
    uint32_t backlink;
} Map_Slot;

typedef struct Map {
    Allocator* alloc;
    uint8_t* entries;
    uint32_t count;
    uint32_t capacity;

    Map_Slot* slots;
    uint32_t slots_removed;
    uint32_t slots_mask; //capacity of the hash - 1
} Map;

typedef struct Map_Found {
    uint32_t index;
    uint32_t slot;
    uint64_t hash; 
} Map_Found;

typedef struct Map_Find_It {
    struct {
        uint32_t slot;
        uint32_t iter;
    } internal;
    Map_Found found;
} Map_Find_It;

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

//regular interface (requires only key and hash is calculated using info)
MAP_INLINE_API void      map_init(Map* map, Map_Info info, Allocator* alloc);
MAP_INLINE_API void      map_deinit(Map* map, Map_Info info);
MAP_INLINE_API void      map_reserve(Map* map, Map_Info info, isize count);
MAP_INLINE_API void      map_rehash(Map* map, Map_Info info, isize count);

MAP_INLINE_API bool      map_find(const Map* map, Map_Info info, const void* key, Map_Found* found);
MAP_INLINE_API Map_Found map_find_index(const Map* map, isize index);
MAP_INLINE_API bool      map_find_iterate(const Map* map, Map_Info info, const void* key, Map_Find_It* it);
MAP_INLINE_API bool      map_remove(Map* map, Map_Info info, Map_Found found);
MAP_INLINE_API Map_Found map_insert(Map* map, Map_Info info, const void* key);
MAP_INLINE_API bool      map_insert_or_find(Map* map, Map_Info info, const void* key, Map_Found* found);

//hash interface (requires key and hash)
MAP_INLINE_API bool      map_hash_find(const Map* map, Map_Info info, const void* key, uint64_t hash, Map_Found* found);
MAP_INLINE_API bool      map_hash_find_iterate(const Map* map, Map_Info info, const void* key, uint64_t hash, Map_Find_It* it);
MAP_INLINE_API Map_Found map_hash_insert(Map* map, Map_Info info, const void* key, uint64_t hash);
MAP_INLINE_API bool      map_hash_insert_or_find(Map* map, Map_Info info, const void* key, uint64_t hash, Map_Found* found);

#define MAP_TEST_INVARIANTS_BASIC   ((uint32_t) 1)
#define MAP_TEST_INVARIANTS_SLOTS   ((uint32_t) 2)
#define MAP_TEST_INVARIANTS_HASHES  ((uint32_t) 4)
#define MAP_TEST_INVARIANTS_FIND    ((uint32_t) 8)
#define MAP_TEST_INVARIANTS_ALL     ((uint32_t) -1)
ATTRIBUTE_INLINE_NEVER EXTERNAL void map_test_invariant(const Map* map, Map_Info info, uint32_t flags);
ATTRIBUTE_INLINE_NEVER EXTERNAL void map_test_hash_invariant(const Map* map, uint32_t flags);
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

#define MAP_EMPTY_ENTRY   (uint32_t) -1
#define MAP_REMOVED_ENTRY (uint32_t) -2

ATTRIBUTE_INLINE_NEVER EXTERNAL void _map_grow_entries(Map* map, isize requested_capacity, uint32_t entry_size, uint32_t entry_align);
ATTRIBUTE_INLINE_NEVER EXTERNAL void _map_rehash(Map* map, isize requested_capacity);
ATTRIBUTE_INLINE_NEVER EXTERNAL void _map_deinit(Map* map, uint32_t entry_size, uint32_t entry_align);
EXTERNAL bool _map_remove_found(Map* map, uint32_t index, uint32_t slot, uint32_t entry_size);

MAP_INLINE_API void _map_check_invariants(const Map* map, Map_Info info)
{
    #if defined(DO_ASSERTS_SLOW)
        map_test_invariant(map, info, MAP_TEST_INVARIANTS_ALL);
    #elif defined(DO_ASSERTS)
        map_test_invariant(map, info, MAP_TEST_INVARIANTS_BASIC);
    #endif
}

MAP_INLINE_API void _map_check_hash_invariants(const Map* map)
{
    #if defined(DO_ASSERTS_SLOW)
        map_test_hash_invariant(map, MAP_TEST_INVARIANTS_ALL);
    #elif defined(DO_ASSERTS)
        map_test_hash_invariant(map, MAP_TEST_INVARIANTS_BASIC);
    #endif
}

MAP_INLINE_API void map_reserve(Map* map, Map_Info info, isize requested_capacity)
{
    if(map->count*3/4 <= requested_capacity + map->slots_removed)
        _map_rehash(map, requested_capacity);
        
    if(requested_capacity > map->capacity)
        _map_grow_entries(map, requested_capacity, info.entry_size, info.entry_align);
}

MAP_INLINE_API Map_Find_It _map_find_it_make(const Map* map, uint64_t hash)
{
    Map_Find_It it = {0};
    Map_Found found = {(uint32_t) -1, (uint32_t) -1, hash};
    it.found = found;
    it.internal.iter = 1;
    it.internal.slot = (uint32_t) hash & map->slots_mask;
    return it;
}

MAP_INLINE_API bool _map_find_next(const Map* map, Map_Info info, const void* key, Map_Find_It* it, bool prefetch)
{
    _map_check_invariants(map, info);
    for(;it->internal.iter <= map->slots_mask; it->internal.iter += 1)
    {
        ASSERT(it->internal.iter <= map->slots_mask + 1);
        
        uint64_t next_slot = (it->internal.slot + it->internal.iter) & map->slots_mask;
        if(prefetch) {
            
        }

        Map_Slot* slot = &map->slots[it->internal.slot];
        if(slot->hash == it->found.hash) {
            uint8_t* entry = map->entries + slot->index*info.entry_size;
            if(info.key_equals == NULL || ((Key_Equals_Func)info.key_equals)(entry + info.key_offset, key))
            {
                it->found.index = slot->index;
                it->found.slot = it->internal.slot;
                return true;
            }
        }
        else if(slot->hash == MAP_EMPTY_ENTRY) 
            break;
        
        it->internal.slot = (it->internal.slot + it->internal.iter) & map->slots_mask;
    }
    it->found.index = -1;
    it->found.slot = -1;
    return false;
}

MAP_INLINE_API bool _map_insert_or_find(Map* map, Map_Info info, const void* key, uint64_t hash, Map_Found* found, bool do_only_insert)
{
    _map_check_invariants(map, info);
    map_reserve(map, info, (isize) map->count + 1);
    uint64_t i = hash & map->slots_mask;
    uint64_t empty_i = (uint64_t) -1;
    for(uint64_t k = 1; ; k++)
    {
        ASSERT(k <= map->slots_mask);
        Map_Slot* slot = &map->slots[i];
        //if we are inserting we dont care about duplicates. 
        // We just insert to the first slot where we can 
        if(do_only_insert) {
            if(slot->index == MAP_REMOVED_ENTRY || slot->index == MAP_EMPTY_ENTRY)
                break;
        }
        //if we are inserting or finding, we need to keep iterating until we find
        // a properly empty slot. Only then we can be sure the slot is not in the map.
        //We also keep track of the previous removed slot, so we can store it there 
        // and thus help to clean up the hash map a bit.
        else {
            if(slot->index == MAP_REMOVED_ENTRY)
                empty_i = i;

            if(slot->index == MAP_EMPTY_ENTRY) {
                if(empty_i != (uint32_t) -1)
                    i = empty_i;
                break; 
            }

            if(slot->hash == hash) {
                uint8_t* entry = map->entries + slot->index*info.entry_size;
                if(info.key_equals == NULL || ((Key_Equals_Func)info.key_equals)(entry + info.key_offset, key))
                {
                    found->hash = hash;
                    found->index = slot->index;
                    found->slot = i;
                    return false;
                }
            }
        }
            
        i = (i + k) & map->slots_mask;
    }

    //update hash part
    isize added_index = map->count++;
    Map_Slot* slot = &map->slots[i];
    slot->hash = hash;
    slot->index = (uint32_t) added_index;
    map->slots_removed -= slot->index == MAP_REMOVED_ENTRY;
    
    //add the back link
    ASSERT(added_index <= map->slots_mask);
    ASSERT(map->slots[added_index].backlink == (uint32_t) -1);
    map->slots[added_index].backlink = i;

    found->hash = hash;
    found->index = added_index;
    found->slot = i;
    
    _map_check_invariants(map, info);
    return true;
}

MAP_INLINE_API Map_Found map_find_index(const Map* map, isize index)
{
    _map_check_hash_invariants(map);
    Map_Found out = {(uint32_t) -1, (uint32_t) -1, 0};
    if(index >= map->count)
        return out;

    out.slot = map->slots[index].backlink;
    ASSERT(out.slot <= map->slots_mask);
    ASSERT(map->slots[out.slot].index == index);

    out.hash = map->slots[out.slot].hash;
    out.index = index;
    return out;
}

MAP_INLINE_API bool map_remove_found(Map* map, Map_Info info, Map_Found found)
{
    return _map_remove_found(map, found.index, found.slot, info.entry_size);
}

MAP_INLINE_API void map_init(Map* map, Map_Info info, Allocator* alloc)
{
    map_deinit(map, info);
    map->alloc = alloc;
}

MAP_INLINE_API void map_deinit(Map* map, Map_Info info)
{
    _map_check_invariants(map, info);
    _map_deinit(map, info.entry_size, info.entry_align);
    _map_check_invariants(map, info);
}

MAP_INLINE_API bool map_hash_find(const Map* map, Map_Info info, const void* key, uint64_t hash, Map_Found* found)
{
    Map_Find_It it = _map_find_it_make(map, hash);
    bool out = _map_find_next(map, info, key, &it);
    *found = it.found;
    return out;
}

MAP_INLINE_API bool map_hash_find_iterate(const Map* map, Map_Info info, const void* key, uint64_t hash, Map_Find_It* it)
{
    if(it->internal.iter == 0) 
        *it = _map_find_it_make(map, hash);
    return _map_find_next(map, info, key, it);
}

MAP_INLINE_API Map_Found map_hash_insert(Map* map, Map_Info info, const void* key, uint64_t hash)
{
    Map_Found found = {0};
    _map_insert_or_find(map, info, key, hash, &found, true);
    return found;
}

MAP_INLINE_API bool map_hash_insert_or_find(Map* map, Map_Info info, const void* key, uint64_t hash, Map_Found* found)
{
    return _map_insert_or_find(map, info, key, hash, found, false);
}

MAP_INLINE_API bool map_find(const Map* map, Map_Info info, const void* key, Map_Found* found)
{
    ASSERT(info.key_hash);
    return map_hash_find(map, info, key, ((Key_Hash_Func)info.key_hash)(key), found);
}

MAP_INLINE_API bool map_find_iterate(const Map* map, Map_Info info, const void* key, Map_Find_It* it)
{
    ASSERT(info.key_hash);
    if(it->internal.iter == 0) 
        *it = _map_find_it_make(map, ((Key_Hash_Func)info.key_hash)(key));
    return _map_find_next(map, info, key, it);
}

MAP_INLINE_API Map_Found map_insert(Map* map, Map_Info info, const void* key)
{
    ASSERT(info.key_hash);
    return map_hash_insert(map, info, key, ((Key_Hash_Func)info.key_hash)(key));
}

MAP_INLINE_API bool map_insert_or_find(Map* map, Map_Info info, const void* key, Map_Found* found)
{
    ASSERT(info.key_hash);
    return map_hash_insert_or_find(map, info, key, ((Key_Hash_Func)info.key_hash)(key), found);
}
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_MAP)) && !defined(MODULE_HAS_IMPL_MAP)
#define MODULE_HAS_IMPL_MAP

static void* _map_alloc(Allocator* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
{
    #ifndef USE_MALLOC
        return (*alloc)(alloc, new_size, old_ptr, old_size, align, NULL);
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
void _map_grow_entries(Map* map, isize requested_capacity, uint32_t entry_size, uint32_t entry_align)
{
    TEST(requested_capacity <= UINT32_MAX);

    isize new_capacity = map->capacity*3/2 + 8;
    if(new_capacity < requested_capacity)
        new_capacity = requested_capacity;
    if(new_capacity < 16)
        new_capacity = 16;
    
    map->entries = (uint8_t*) _map_alloc(map->alloc, new_capacity*entry_size, map->entries, map->capacity*entry_size, entry_align);
    map->capacity = new_capacity;
}

ATTRIBUTE_INLINE_NEVER 
void _map_rehash(Map* map, isize requested_capacity)
{
    TEST(requested_capacity <= UINT32_MAX);

    _map_check_hash_invariants(map);
    isize new_cap = 16;
    while(new_cap < requested_capacity)
        new_cap *= 2;

    while(new_cap*4/3 < map->count)
        new_cap *= 2;
    
    // allocate new slots and set all to empty
    uint64_t new_mask = (uint64_t) new_cap - 1;
    Map_Slot* new_slots = (Map_Slot*) _map_alloc(map->alloc, new_cap*sizeof(Map_Slot), NULL, 0, sizeof(Map_Slot));
    memset(new_slots, -1, new_cap*sizeof(Map_Slot)); 

    //copy over slots entries
    for(isize j = 0; j <= map->slots_mask; j++)
    {
        Map_Slot* slot = &map->slots[j];
        if(slot->index != MAP_EMPTY_ENTRY && slot->index != MAP_REMOVED_ENTRY)
        {
            uint64_t i = slot->hash & new_mask;
            for(uint64_t k = 1; ; k++) {
                if(new_slots[i].index == MAP_REMOVED_ENTRY || new_slots[i].index == MAP_EMPTY_ENTRY)
                    break;
                    
                i = (i + k) & new_mask;
            }           

            new_slots[i].hash = slot->hash;
            new_slots[i].index = slot->index;
            new_slots[slot->index].backlink = i;
        }
    }

    _map_alloc(map->alloc, 0, map->slots, (map->slots_mask + 1)*sizeof(Map_Slot), sizeof(Map_Slot));
    map->slots = new_slots;
    map->slots_mask = new_mask;
    _map_check_hash_invariants(map);
}

ATTRIBUTE_INLINE_NEVER 
void _map_deinit(Map* map, uint32_t entry_size, uint32_t entry_align)
{
    if(map->capacity > 0) 
        _map_alloc(map->alloc, 0, map->entries, map->capacity*entry_size, entry_align);
    if(map->slots_mask > 0) 
        _map_alloc(map->alloc, 0, map->slots, (map->slots_mask + 1)*sizeof(Map_Slot), sizeof(Map_Slot));
    memset(map, 0, sizeof* map);
    _map_check_hash_invariants(map);
}

EXTERNAL bool _map_remove_found(Map* map, uint32_t index, uint32_t slot, uint32_t entry_size)
{
    if(index == (uint32_t) -1)
    {
        _map_check_hash_invariants(map);
        isize removed_index = index;
        isize last_index = map->count - 1;
        if(last_index != removed_index)
        {
            isize last_slot_i = map->slots[last_index].backlink;
            Map_Slot* last_slot = &map->slots[last_slot_i];
            
            void* removed_entry = map->entries + removed_index*entry_size; 
            void* last_entry = map->entries + last_index*entry_size;
            memcpy(removed_entry, last_entry, entry_size);
            
            last_slot->index = removed_index;
            map->slots[last_index].backlink = (uint32_t) -1;
        }

        Map_Slot* removed_slot = &map->slots[slot];
        removed_slot->index = MAP_REMOVED_ENTRY;
        removed_slot->hash = (uint32_t) -1;
        map->slots_removed += 1;
        map->count -= 1;
        _map_check_hash_invariants(map);
        return true;
    }
    return false;
}

ATTRIBUTE_INLINE_NEVER
void map_test_hash_invariant(const Map* map, uint32_t flags)
{
    if(map->alloc == NULL) {
        Map null = {0};
        TEST(memcmp(map, &null, sizeof *map) == 0);
    }
    else {
        if(flags & MAP_TEST_INVARIANTS_BASIC) {
            TEST(map->capacity < (uint32_t) -2);
            TEST(map->count <= map->capacity);
            TEST(map->count <= map->slots_mask + 1);
            TEST(map->slots_removed <= map->slots_mask + 1);
            
            TEST((map->capacity == 0) == (map->entries == NULL));
            TEST((map->slots_mask == 0) == (map->slots == NULL));
            TEST((map->capacity == 0) == (map->slots_mask == 0));
        }

        if(flags & MAP_TEST_INVARIANTS_SLOTS) {
            for(uint32_t i = 0; i <= map->slots_mask; i++)
            {
                Map_Slot* slot = &map->slots[i];
                if(slot->index == MAP_EMPTY_ENTRY || slot->index == MAP_REMOVED_ENTRY)
                    TEST(slot->hash == (uint32_t) -1);
                else
                    TEST(slot->index < map->count);
                    
                if(i < map->count)
                {
                    Map_Slot* linked = &map->slots[slot->backlink];
                    TEST(slot->backlink <= map->slots_mask);
                    TEST(linked->index == i);
                }
            }
        }
    }
}

ATTRIBUTE_INLINE_NEVER
void map_test_invariant(const Map* map, Map_Info info, uint32_t flags)
{
    map_test_hash_invariant(map, flags);

    if(flags & MAP_TEST_INVARIANTS_HASHES) {
        TEST(info.key_hash);
        for(uint32_t i = 0; i < map->count; i++)
        {
            Map_Found found = map_find_index(map, i);
            uint8_t* entry = map->entries + i*info.entry_size;
            uint8_t* key = entry + info.key_offset;

            uint64_t computed_hash = ((Key_Hash_Func)info.key_hash)(key);
            TEST(computed_hash == found.hash);
        }
    }

    if(flags & MAP_TEST_INVARIANTS_FIND) {
        for(uint32_t i = 0; i < map->count; i++)
        {
            Map_Found found = map_find_index(map, i);
            Map_Slot* slot = &map->slots[found.slot];
            
            bool found_self = false;

            uint8_t* entry = map->entries + i*info.entry_size;
            uint8_t* key = entry + info.key_offset;
            for(Map_Find_It it = {0}; map_hash_find_iterate(map, info, key, slot->hash, &it); ) {
                TEST(it.found.hash == slot->hash);
                if(it.found.index == i)
                {
                    TEST(it.found.slot == found.slot);
                    found_self = true;
                }
            }

            TEST(found_self);
        }
    }
}

#endif

typedef struct String {
    const char* data;
    isize count;
} String;

typedef struct My_Entry {
    String path;
    int values[16];
} My_Entry;

typedef union My_Map {
    Map generic;
    struct {
        Allocator* alloc;
        My_Entry* entries;
        uint32_t count;
        uint32_t capacity;
        
        Map_Slot* slots;
        uint32_t slots_removed;
        uint32_t slots_mask; //capacity of the hash - 1
    };
} My_Map;

ATTRIBUTE_INLINE_NEVER
uint64_t hash64_fnv(const void* key, int64_t size, uint64_t seed)
{
    const uint8_t* data = (const uint8_t*) key;
    uint64_t hash = seed ^ 0x27D4EB2F165667C5ULL;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ (uint64_t) data[i];

    return hash;
}

bool string_is_equal_ptrs(const String* a, const String* b)
{
    return a->count == b->count && memcmp(a->data, b->data, a->count) == 0;
}

uint64_t string_hash_ptrs(const String* str)
{
    return hash64_fnv(str->data, str->count, 0);
}


#define MY_MAP_INFO (Map_Info){sizeof(My_Entry), __alignof(My_Entry), 0, string_is_equal_ptrs, string_hash_ptrs}

#if 0
bool      my_map_find(const My_Map* map, String string, Map_Found* found)           { return map_find(&map->generic, MY_MAP_INFO, &string, found); }
bool      my_map_find_iterate(const My_Map* map, String string, Map_Find_It* it)    { return map_find_iterate(&map->generic, MY_MAP_INFO, &string, it); }
Map_Found my_map_insert(My_Map* map, String string)                                 { return map_insert(&map->generic, MY_MAP_INFO, &string); }
bool      my_map_insert_or_find(My_Map* map, String string, Map_Found* found)       { return map_insert_or_find(&map->generic, MY_MAP_INFO, &string, found); }
#else
bool      my_map_find(const My_Map* map, String string, Map_Found* found)           { return map_hash_find(&map->generic, MY_MAP_INFO, &string, string_hash_ptrs(&string), found); }
bool      my_map_find_iterate(const My_Map* map, String string, Map_Find_It* it)    { return map_hash_find_iterate(&map->generic, MY_MAP_INFO, &string, string_hash_ptrs(&string), it); }
Map_Found my_map_insert(My_Map* map, String string)                                 { return map_hash_insert(&map->generic, MY_MAP_INFO, &string, string_hash_ptrs(&string)); }
bool      my_map_insert_or_find(My_Map* map, String string, Map_Found* found)       { return map_hash_insert_or_find(&map->generic, MY_MAP_INFO, &string, string_hash_ptrs(&string), found); }
#endif