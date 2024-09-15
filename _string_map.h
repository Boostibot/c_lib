#pragma once

#include "hash.h"
#include "hash_string.h"
#include "array.h"

typedef struct String_Map {
    union {
        Hash hash;
        Allocator* allocator; 
    };
    
    Hash_String* keys;
    u8* values;
    i32 len;
    i32 capacity;

    i32 value_size;
    i32 value_align;

    //Upper estimate for the number of hash collisions in the hash table.
    //A collision is caused by spurious 64 bit hash collision (extremely rare)
    // or by inserting multiple of the same keys into the map (multihash).
    //If no removals were performed is exact count.
    isize max_collision_count;

    //Allocator used to allocate individual keys. 
    //If null keys are taken to be owned by the caller and must remain valid for 
    // the entire lifetime of this structure
    Allocator* key_allocator; 

    //Destructor called when values are removed from the hash. If null does not get called.
    void (*value_destructor)(void* value, void* context);
    void* value_destructor_context;
} String_Map;

typedef struct String_Map_Found {
    i32 hash_index;
    i32 hash_probe;
    i32 index;

    bool inserted;
    bool _[3];

    Hash_String key;
    void* value;
} String_Map_Found;

EXTERNAL void string_map_init_custom(
    String_Map* table, Allocator* alloc, Allocator* strings_alloc_or_null_if_not_owning, isize value_size, isize value_align, 
    void (*value_destructor_or_null)(void* value, void* context), void* value_destructor_context);
EXTERNAL void string_map_init(String_Map* table, Allocator* alloc, isize value_size);
EXTERNAL void string_map_deinit(String_Map* table);
EXTERNAL void string_map_test_invariants(const String_Map* table, bool slow_checks);
EXTERNAL void string_map_reserve(String_Map* table, isize num_entries);
EXTERNAL void string_map_clear(String_Map* table);
EXTERNAL void string_map_remove_found(String_Map* table, String_Map_Found found);
EXTERNAL i32  string_map_remove(String_Map* table, Hash_String key);
EXTERNAL String_Map_Found string_map_find(const String_Map* table, Hash_String key);
EXTERNAL String_Map_Found string_map_find_next(const String_Map* table, String_Map_Found prev_found);
EXTERNAL String_Map_Found string_map_insert(String_Map* table, Hash_String key, const void* value);
EXTERNAL String_Map_Found string_map_find_or_insert(String_Map* table, Hash_String key, const void* value);
EXTERNAL String_Map_Found string_map_assign_or_insert(String_Map* table, Hash_String key, const void* value);
//EXTERNAL Generic_Array string_map_find_all(const String_Map* table, Hash_String key, Allocator* alloc);
//EXTERNAL Generic_Array string_map_find_all_and_sort(const String_Map* table, Hash_String key, Allocator* alloc);

//Macros to allow pointers to temporary
#ifdef __cplusplus
    #define VPTR(Type, val) (&(const Type&) (val))
    #define VVPTR(val)      (&(const decltype(val)&) (val))
#else
    #define VPTR(Type, val) (Type[]){val}
    #define VVPTR(val)      (__typeof__(val)[]){val}
#endif

#ifndef STRING_MAP_DEBUG
    #if defined(DO_ASSERTS_SLOW)
        #define STRING_MAP_DEBUG 2
    #elif !defined(NDEBUG)
        #define STRING_MAP_DEBUG 1
    #else
        #define STRING_MAP_DEBUG 0
    #endif
#endif

EXTERNAL void string_map_test_invariants(const String_Map* table, bool slow_checks)
{
    TEST((table->keys == NULL) == (table->values == NULL));
    TEST(0 <= table->len && table->len <= table->capacity);
    TEST(0 <= table->value_size);
    TEST(0 <= is_power_of_two(table->value_align));
    TEST(0 <= table->max_collision_count);
    TEST(table->hash.len == table->len);

    if(table->keys != NULL)
        TEST(table->allocator != NULL);

    if(slow_checks)
    {
        for(isize i = 0; i < table->len; i++)
        {
            Hash_String key = table->keys[i];
            void* value = table->values + i*table->value_size;

            bool found_this_entry = false;
            for(String_Map_Found found = string_map_find(table, key); found.index != -1; found = string_map_find_next(table, found))
            {
                if(memcmp(found.value, value, table->value_size) == 0)
                {
                    found_this_entry = true;
                    break;
                }
            }

            TEST(found_this_entry, "All keys need to be findable. Not found " HSTRING_FMT, HSTRING_PRINT(key));
            TEST(found_this_entry, "All keys need to be findable. Not found " HSTRING_FMT, HSTRING_PRINT(key));
        }
    }
}

INTERNAL void _string_map_check_invariants(const String_Map* table)
{
    int debug_level = STRING_MAP_DEBUG;
    if(debug_level > 0)
        string_map_test_invariants(table, debug_level == 2);
}

INTERNAL String_Map_Found _string_map_found_from_hash_found(const String_Map* table, Hash_Found found, Hash_String key)
{
    String_Map_Found out = {found.index, found.probes, -1};
    out.inserted = found.inserted;
    out.key = key;
    if(found.index != -1)
    {
        out.index = (i32) found.value;
        out.value = table->values + found.value*table->value_size;
    }
    return out;
}

INTERNAL Hash_Found _hash_found_from_string_map_found(String_Map_Found found)
{
    Hash_Found out = {found.hash_index, found.hash_probe, found.key.hash};
    return out;
}

INTERNAL void _string_map_reserve_values(String_Map* table, isize to_size)
{
    if(to_size > table->capacity)
    {
        isize old_capacity = table->capacity;
        isize new_capacity = MAX(table->capacity*3/2 + 8, to_size);

        table->values = (u8*) allocator_reallocate(table->allocator, new_capacity*table->value_size, table->values, old_capacity*table->value_size, table->value_align);
        table->keys = (Hash_String*) allocator_reallocate(table->allocator, new_capacity*sizeof(Hash_String), table->keys, old_capacity*sizeof(Hash_String), 8);
        table->capacity = (i32) new_capacity;
    }
}

INTERNAL void _string_map_clear_values(String_Map* table)
{
    if(table->key_allocator)
        for(isize i = 0; i < table->len; i++)
            allocator_deallocate(table->key_allocator, (void*) table->keys[i].data, table->keys[i].len + 1, 1);

    if(table->value_destructor)
        for(isize i = 0; i < table->len; i++)
            table->value_destructor(table->values + i*table->value_size, table->value_destructor_context);
}

INTERNAL void _string_map_push_values(String_Map* table, Hash_String key, const void* value)
{
    _string_map_reserve_values(table, table->len + 1);
    
    Hash_String pushed_key = key;
    if(table->key_allocator != NULL)
    {
        char* data = (char*) allocator_allocate(table->key_allocator, key.len + 1, 1);
        memcpy(data, key.data, key.len);
        data[key.len] = '\0';
        pushed_key.data = data;
        pushed_key.len = key.len;
        pushed_key.hash = key.hash;
    }

    memcpy(table->values + table->value_size*table->len, value, table->value_size);
    table->keys[table->len] = pushed_key;
    table->len += 1;
}

EXTERNAL void string_map_deinit(String_Map* table)
{
    _string_map_check_invariants(table);
    _string_map_clear_values(table);
            
    allocator_deallocate(table->allocator, table->values, table->capacity*table->value_size, table->value_align);
    allocator_deallocate(table->allocator, table->keys, table->capacity*sizeof(Hash_String), 8);
    hash_deinit(&table->hash);
    memset(table, 0, sizeof* table);
    _string_map_check_invariants(table);
}

EXTERNAL void string_map_init_custom(String_Map* table, Allocator* alloc, Allocator* strings_alloc_or_null_if_not_owning, isize value_size, isize value_align, void (*value_destructor)(void* value, void* context), void* value_destructor_context)
{
    string_map_deinit(table);

    hash_init(&table->hash, alloc);
    table->key_allocator = strings_alloc_or_null_if_not_owning;
    table->value_size = (i32) value_size;
    table->value_align = (i32) value_align;
    table->value_destructor = value_destructor;
    table->value_destructor_context = value_destructor_context;
    
    _string_map_check_invariants(table);
}

EXTERNAL void string_map_init(String_Map* table, Allocator* alloc, isize value_size)
{
    string_map_init_custom(table, alloc, alloc, value_size, 16, NULL, NULL);
}

EXTERNAL void string_map_reserve(String_Map* table, isize num_entries)
{
    hash_reserve(&table->hash, num_entries);
    _string_map_reserve_values(table, num_entries);
    _string_map_check_invariants(table);
}

EXTERNAL void string_map_clear(String_Map* table)
{
    _string_map_check_invariants(table);
    _string_map_clear_values(table);
    hash_clear(&table->hash);
    table->max_collision_count = 0;
    _string_map_check_invariants(table);
}

EXTERNAL String_Map_Found string_map_find(const String_Map* table, Hash_String key)
{
    Hash_Found found = hash_find(table->hash, key.hash);
    if(found.index != -1)
    {
        Hash_String* found_key = &table->keys[found.value];
        if(table->max_collision_count == 0 || string_is_equal(found_key->string, key.string))
            return _string_map_found_from_hash_found(table, found, key);
    }

    found.index = -1;
    return _string_map_found_from_hash_found(table, found, key);
}

EXTERNAL String_Map_Found string_map_find_next(const String_Map* table, String_Map_Found prev_found)
{
    Hash_Found _prev_found = _hash_found_from_string_map_found(prev_found);
    Hash_Found found = hash_find_next(table->hash, _prev_found);
    if(found.index != -1)
    {
        Hash_String* found_key = &table->keys[found.value];
        if(table->max_collision_count == 0 || string_is_equal(found_key->string, prev_found.key.string))
            return _string_map_found_from_hash_found(table, found, prev_found.key);
    }
    
    found.index = -1;
    return _string_map_found_from_hash_found(table, found, prev_found.key);
}

EXTERNAL String_Map_Found string_map_insert(String_Map* table, Hash_String key, const void* value)
{
    _string_map_check_invariants(table);
    Hash_Found found = hash_find_or_insert(&table->hash, key.hash, table->len);
    if(found.inserted == false)
    {
        table->max_collision_count += 1;
        found = hash_insert(&table->hash, key.hash, table->len);
    }

    _string_map_push_values(table, key, value);
    _string_map_check_invariants(table);
    return _string_map_found_from_hash_found(table, found, key);
}

EXTERNAL String_Map_Found string_map_find_or_insert(String_Map* table, Hash_String key, const void* value)
{
    _string_map_check_invariants(table);
    Hash_Found found = hash_find_or_insert(&table->hash, key.hash, table->len);
    
    bool colided = false;
    while(found.inserted == false)
    {
        Hash_String found_key = table->keys[found.value];
        if(string_is_equal(found_key.string, key.string))
        {
            _string_map_check_invariants(table);
            return _string_map_found_from_hash_found(table, found, key);
        }
        else
        {
            colided = true;
            found = hash_find_or_insert_next(&table->hash, found, table->len);
        }
    }
    
    table->max_collision_count += colided;
    _string_map_push_values(table, key, value);
    _string_map_check_invariants(table);
    return _string_map_found_from_hash_found(table, found, key);
}

EXTERNAL String_Map_Found string_map_assign_or_insert(String_Map* table, Hash_String key, const void* value)
{
    _string_map_check_invariants(table);
    Hash_Found found = hash_find_or_insert(&table->hash, key.hash, table->len);

    bool colided = false;
    while(found.inserted == false)
    {
        Hash_String found_key = table->keys[found.value];
        if(string_is_equal(found_key.string, key.string))
        {
            i32 index = (i32) found.value;
            memcpy(table->values + table->value_size*index, value, table->value_size);
            _string_map_check_invariants(table);
            return _string_map_found_from_hash_found(table, found, key);
        }
        else
        {
            colided = true;
            found = hash_find_or_insert_next(&table->hash, found, table->len); 
        }
    }
    
    table->max_collision_count += colided;
    _string_map_push_values(table, key, value);
    _string_map_check_invariants(table);
    return _string_map_found_from_hash_found(table, found, key);
}

EXTERNAL void string_map_remove_found(String_Map* table, String_Map_Found found)
{
    _string_map_check_invariants(table);
    ASSERT(table->len > 0);
    i32 last_i = table->len - 1;

    //If removing item not on top of the items stack do the classing swap to top and pop.
    //We however have to make sure the hash remains properly updated. 
    if(found.index != last_i)
    {
        Hash_String* last_key = &table->keys[last_i];
        void* last_value = table->values + table->value_size*(last_i);

        Hash_String* rem_key = &table->keys[found.index];
        void* rem_value = table->values + table->value_size*(found.index);

        //Find the hash of the item at the top of the hash and
        //relink it to point to the removed slot instead
        bool relinked = false;
        for(String_Map_Found last_found = string_map_find(table, *last_key); last_found.index != -1; last_found = string_map_find_next(table, last_found))
        {
            if(table->hash.entries[last_found.hash_index].value == last_i)
            {   
                table->hash.entries[last_found.hash_index].value = (u64) found.index;
                relinked = true;
                break;
            }
        }
        ASSERT(relinked);

        //Swap last and removed
        SWAP(last_key, rem_key);
        memswap(last_value, rem_value, table->value_size);
    }
    
    if(table->key_allocator)
        allocator_deallocate(table->key_allocator, (void*) table->keys[last_i].data, table->keys[last_i].len + 1, 1);

    if(table->value_destructor)
        table->value_destructor(table->values + last_i*table->value_size, table->value_destructor_context);

    hash_remove_found(&table->hash, found.hash_index);
    table->len -= 1;
    _string_map_check_invariants(table);
}

EXTERNAL i32 string_map_remove(String_Map* table, Hash_String key)
{
    _string_map_check_invariants(table);
    i32 removed = 0;
    for(String_Map_Found found = string_map_find(table, key); found.index != -1; found = string_map_find_next(table, found))
    {
        string_map_remove_found(table, found);
        removed += 1;
    }
    
    _string_map_check_invariants(table);
    return removed;
}

