#ifndef JOT_MAP_STRING
#define JOT_MAP_STRING

#include "map.h"
#include "hash_string.h"

typedef struct String_Map String_Map;
typedef void (*String_Map_Store_Func)(void* value_stored, const void* value_supplied, struct String_Map* map);
typedef void (*String_Map_Deinit_Func)(void* value, struct String_Map* map);

typedef struct String_Map {
    union {
        Allocator* allocator; 
        Map map;
        struct {
            Hash hash;
    
            Hash_String* keys;
            u8* values;

            i32 count;
            i32 capacity;
        };
    };

    Allocator* strings_allocator;
    i32 value_size;
    i32 value_align;
    String_Map_Store_Func value_store;
    String_Map_Deinit_Func value_deinit;
    void* user_context;
} String_Map;

EXTERNAL void string_map_init(
    String_Map* map, Allocator* alloc, Allocator* strings_alloc_or_null_if_not_owning, 
    isize value_size, isize value_align, 
    String_Map_Store_Func value_store_or_null,
    String_Map_Deinit_Func value_deinit_or_null,
    void* user_context);
EXTERNAL void string_map_deinit(String_Map* map);
EXTERNAL void string_map_test_invariants(const String_Map* map, bool slow_checks);
EXTERNAL void string_map_reserve(String_Map* map, isize num_entries);
EXTERNAL void string_map_clear(String_Map* map);
EXTERNAL Map_Found string_map_find(const String_Map* map, Hash_String key);
EXTERNAL Map_Found string_map_find_next(const String_Map* map, Hash_String key, Map_Found prev_found);
EXTERNAL Map_Found string_map_insert(String_Map* map, Hash_String key, const void* value);
EXTERNAL Map_Found string_map_find_or_insert(String_Map* map, Hash_String key, const void* value);
EXTERNAL Map_Found string_map_assign_or_insert(String_Map* map, Hash_String key, const void* value);
EXTERNAL bool string_map_remove_found(String_Map* map, Map_Found found);
EXTERNAL i32  string_map_remove(String_Map* map, Hash_String key);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_MAP_STRING_IMPL)) && !defined(JOT_MAP_STRING_HAS_IMPL)
#define JOT_MAP_STRING_HAS_IMPL

MAPAPI u64 _string_map_key_hash(Hash_String* stored_key, String_Map* map)
{
    (void) map;
    return stored_key->hash;
}

MAPAPI bool _string_map_key_eq(Hash_String* stored_key, Hash_String* supplied_key, String_Map* map)
{
    (void) map;
    ASSERT(stored_key->hash == supplied_key->hash);
    return string_is_equal(stored_key->string, supplied_key->string);
}

MAPAPI void _string_map_key_store(Hash_String* stored_key, Hash_String* supplied_key, String_Map* map)
{
    if(map->strings_allocator)
        *stored_key = hash_string_allocate(map->strings_allocator, *supplied_key);
    else
        *stored_key = *supplied_key;
}

MAPAPI void _string_map_key_deinit(Hash_String* key, String_Map* map)
{
    if(map->strings_allocator)
        hash_string_deallocate(map->strings_allocator, key);
}

#define STRING_MAP_INTERFACE(map) BINIT(Map_Interface){     \
        _string_map_key_hash,       \
        _string_map_key_eq,       \
        _string_map_key_store,     \
        (map)->value_store,        \
        _string_map_key_deinit,   \
        (map)->value_deinit,      \
        sizeof(Hash_String),                                \
        16,                                                 \
        (map)->value_size,                                  \
        (map)->value_align,                                 \
        (void*) (map)                                       \
    } \

EXTERNAL void string_map_init(
    String_Map* map, Allocator* alloc, Allocator* strings_alloc_or_null_if_not_owning, 
    isize value_size, isize value_align, 
    String_Map_Store_Func value_store_or_null,
    String_Map_Deinit_Func value_deinit_or_null,
    void* user_context)
{
    map_init(&map->map, alloc, STRING_MAP_INTERFACE(map));
    map->strings_allocator = strings_alloc_or_null_if_not_owning;
    map->value_size = (i32) value_size;
    map->value_align = (i32) value_align;
    map->value_store = value_store_or_null;
    map->value_deinit = value_deinit_or_null;
    map->user_context = user_context;
}

EXTERNAL void string_map_deinit(String_Map* map)
{
    map_deinit(&map->map, STRING_MAP_INTERFACE(map));
    memset(map, 0, sizeof *map);
}

EXTERNAL void string_map_test_invariants(const String_Map* map, bool slow_checks)
{
    map_test_invariants(&map->map, slow_checks, STRING_MAP_INTERFACE(map));
}

EXTERNAL void string_map_reserve(String_Map* map, isize num_entries)
{
    map_reserve(&map->map, num_entries, STRING_MAP_INTERFACE(map));
}

EXTERNAL void string_map_clear(String_Map* map)
{
    map_clear(&map->map, STRING_MAP_INTERFACE(map));
}

EXTERNAL Map_Found string_map_find(const String_Map* map, Hash_String key)
{
    return map_find(&map->map, &key, key.hash, STRING_MAP_INTERFACE(map));
}

EXTERNAL Map_Found string_map_find_next(const String_Map* map, Hash_String key, Map_Found prev_found)
{
    return map_find_next(&map->map, &key, prev_found, STRING_MAP_INTERFACE(map));
}

EXTERNAL Map_Found string_map_insert(String_Map* map, Hash_String key, const void* value)
{
    return map_insert(&map->map, &key, key.hash, value, STRING_MAP_INTERFACE(map));
}

EXTERNAL Map_Found string_map_find_or_insert(String_Map* map, Hash_String key, const void* value)
{
    return map_find_or_insert(&map->map, &key, key.hash, value, STRING_MAP_INTERFACE(map));
}

EXTERNAL Map_Found string_map_assign_or_insert(String_Map* map, Hash_String key, const void* value)
{
    return map_assign_or_insert(&map->map, &key, key.hash, value, STRING_MAP_INTERFACE(map));
}

EXTERNAL bool string_map_remove_found(String_Map* map, Map_Found found)
{
    return map_remove_found(&map->map, found, STRING_MAP_INTERFACE(map));
}

EXTERNAL i32 string_map_remove(String_Map* map, Hash_String key)
{
    return map_remove(&map->map, &key, key.hash, STRING_MAP_INTERFACE(map));
}

#undef STRING_MAP_INTERFACE

#endif