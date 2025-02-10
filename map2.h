#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#define ASSERT(x, ...) assert(x)
#define REQUIRE(x, ...) assert(x)

#define ATTRIBUTE_INLINE_NEVER 

typedef int64_t isize;
typedef void* (*Allocator2)(void* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* error);

typedef struct Map_Hash_Entry {
    uint64_t hash;
    uint32_t index;
    uint32_t backlink;
} Map_Hash_Entry;

typedef struct Map_Info {
    int32_t key_size;
    int32_t value_size;
    void* key_equals; //if null then we trust hashes
    void* key_store; //if null then memcpy is used
    void* key_hash; //if null then the non-hash interface is not defined.
    void* key_destructor;
    void* value_destructor;
} Map_Info;

typedef struct Map {
    Allocator2* alloc;
    Allocator2* keys_alloc;
    Allocator2* vals_alloc;

    isize count;
    isize capacity;

    uint32_t removed_count;
    uint32_t entries_mask; //capacity - 1
    Map_Hash_Entry* entries;
    
    uint8_t* values;
    uint8_t* keys;

    Map_Info* info;
} Map;

typedef struct Map_Find_It {
    uint64_t hash; 
    uint32_t slot;
    uint32_t iter;
} Map_Find_It;

typedef struct Map_Found {
    uint32_t index;
    uint32_t slot;
    uint64_t hash;
} Map_Found;

typedef struct Map_Find_Iterator {
    Map_Find_It it;
    Map_Found found;
} Map_Find_Iterator;

#define INLINE_IMPL

INLINE_IMPL void      map_init(Map* map, Allocator2* alloc, Map_Info info);
INLINE_IMPL void      map_deinit(Map* map, Map_Info info);
INLINE_IMPL void      map_reserve(Map* map, isize count, Map_Info info);
INLINE_IMPL void      map_rehash(Map* map, isize count, Map_Info info);

INLINE_IMPL Map_Found map_set(Map* map, const void* key, const void* value, Map_Info info);
INLINE_IMPL void*     map_get(const Map* map, const void* key, Map_Info info);
INLINE_IMPL void*     map_get_or(const Map* map, const void* key, void* if_not_found, Map_Info info);
INLINE_IMPL bool      map_remove(Map* map, const void* key);
INLINE_IMPL isize     map_remove_all(Map* map, const void* key);

INLINE_IMPL Map_Found map_insert(Map* map, const void* key, const void* value, Map_Info info);
INLINE_IMPL bool      map_insert_or_find(Map* map, const void* key, const void* value, Map_Found* found, Map_Info info);
INLINE_IMPL bool      map_insert_or_set(Map* map, const void* key, const void* value, Map_Found* found, Map_Info info);

INLINE_IMPL bool      map_find_iterate(const Map* map, const void* key, Map_Find_Iterator* it, Map_Info info);

INLINE_IMPL bool      map_find(const Map* map, const void* key, Map_Found* found, Map_Info info);
INLINE_IMPL void*     map_get_found(const Map* map, Map_Found found, Map_Info info);
INLINE_IMPL void*     map_get_found_key(const Map* map, Map_Found found, Map_Info info);
INLINE_IMPL void      map_set_found(Map* map, Map_Found found, const void* value, Map_Info info);
INLINE_IMPL bool      map_remove_found(Map* map, Map_Found found, Map_Info info);

#define MAP_REMOVED_ENTRY (uint32_t) -1
#define MAP_EMPTY_ENTRY   (uint32_t) -2

typedef void (*Key_Store_Func)(void* stored, const void* key);
typedef bool (*Key_Equals_Func)(const void* stored, const void* key);
typedef uint64_t (*Key_Hash_Func)(const void* key);

uint8_t* _map_key(const Map* map, isize i, Map_Info info);
uint8_t* _map_val(const Map* map, isize i, Map_Info info);

void _map_key_store(void* store, const void* key, Map_Info info)
{
    if(info.key_store)
        ((Key_Store_Func) info.key_store)(store, key);
    else
        memcpy(store, key, info.key_size);
}

bool _map_key_equals(const void* store, const void* key, Map_Info info)
{ 
    return info.key_equals == NULL || ((Key_Equals_Func)info.key_equals)(store, key); 
}

uint64_t _map_hash(const void* key, Map_Info info)
{
    return ((Key_Hash_Func)info.key_hash)(key);
}

Map_Find_It _map_find_it_make(const Map* map, uint64_t hash)
{
    Map_Find_It it = {hash, (uint32_t) hash & map->entries_mask, 1};
    return it;
}

bool _map_find_next(const Map* map, const void* key, Map_Find_It* it, Map_Found* found, Map_Info info)
{
    for(;it->iter <= map->entries_mask; it->iter += 1)
    {
        ASSERT(it->iter <= map->entries_mask + 1);
        Map_Hash_Entry* entry = &map->entries[it->slot];
        
        if(entry->hash == it->hash) {
            void* entry_key = _map_key(map, entry->index, info);
            if(_map_key_equals(entry_key, key, info)) 
            {
                found->index = entry->index;
                found->slot = it->slot;
                found->hash = it->hash;
                return true;
            }
        }
        else if(entry->hash == MAP_EMPTY_ENTRY)
        {
            found->index = -1;
            found->slot = -1;
            found->hash = it->hash;
            return false;
        }
        
        it->slot = (it->slot + it->iter) & map->entries_mask;
    }
}

ATTRIBUTE_INLINE_NEVER 
void _map_reserve_key_values(Map* map, isize requested_capacity, Map_Info info)
{
    if(requested_capacity > map->capacity)
    {
        isize new_capacity = map->capacity*3/2 + 8;
        if(new_capacity < requested_capacity)
            new_capacity = requested_capacity;

        map->keys = (uint8_t*) (*map->alloc)(map->alloc, new_capacity*info.key_size, map->keys, map->capacity*info.key_size, 16, NULL);
        map->values = (uint8_t*) (*map->alloc)(map->alloc, new_capacity*info.value_size, map->values, map->capacity*info.value_size, 16, NULL);
        map->capacity = new_capacity;
    }
}

ATTRIBUTE_INLINE_NEVER 
void map_rehash(Map* map, isize requested_capacity, Map_Info info)
{
    _map_reserve_key_values(map, requested_capacity, info);

    isize new_cap = 16;
    while(new_cap < requested_capacity)
        new_cap *= 2;

    while(new_cap*4/3 < map->count)
        new_cap *= 2;
    
    uint64_t new_mask = (uint64_t) new_cap - 1;
    Map_Hash_Entry* new_entries = (Map_Hash_Entry*) (*map->alloc)(map->alloc, new_cap*sizeof(Map_Hash_Entry), NULL, 0, sizeof(Map_Hash_Entry), NULL);
    for(isize i = 0; i <= map->entries_mask; i++)
    {
        Map_Hash_Entry* entry = &map->entries[i];
        if(entry->index != MAP_EMPTY_ENTRY && entry->index != MAP_REMOVED_ENTRY)
        {
            uint64_t i = entry->hash & new_mask;
            for(uint64_t k = 1; ; k++) {
                if(new_entries[i].index == MAP_REMOVED_ENTRY || new_entries[i].index == MAP_EMPTY_ENTRY)
                    break;
                    
                i = (i + k) & new_mask;
            }           

            new_entries[i].hash = entry->hash;
            new_entries[i].index = entry->index;
            new_entries[entry->index].backlink = i;
        }
    }

    (*map->alloc)(map->alloc, 0, map->entries, (map->entries_mask + 1)*sizeof(Map_Hash_Entry), sizeof(Map_Hash_Entry), NULL);
    map->entries = new_entries;
    map->entries_mask = new_mask;
}


void map_reserve(Map* map, isize requested_capacity, Map_Info info)
{
    if(map->count*3/4 <= requested_capacity + map->removed_count)
        map_rehash(map, requested_capacity, info);
}

//attempts to find key in map and return its index. If fails inserts the key,value and returns its index+1 negated. 
// (Thus if returned index i >= 0 then the value was found and if i < 0 then the value was inserted).
//If do_only_insert is set then doesnt attempt to find and only inserts even if duplicate keys would be in the map.
bool _map_insert_or_find(Map* map, const void* key, const void* value, Map_Found* found, uint64_t hash, bool do_only_insert, Map_Info info)
{
    map_reserve(map, map->count + 1, info);
    uint64_t i = hash & map->entries_mask;
    uint64_t empty_i = (uint64_t) -1;
    for(uint64_t k = 1; ; k++)
    {
        ASSERT(k <= map->entries_mask);
        Map_Hash_Entry* entry = &map->entries[i];
        //if we are inserting we dont care about duplicates. 
        // We just insert to the first slot where we can 
        if(do_only_insert) {
            if(entry->index == MAP_REMOVED_ENTRY || entry->index == MAP_EMPTY_ENTRY)
                break;
        }
        //if we are inserting or finding, we need to keep iterating until we find
        // a properly empty entry. Only then we can be sure the entry is not in the map.
        //We also keep track of the previous removed slot, so we can store it there 
        // and thus help to clean up the hash map a bit.
        else {
            if(entry->index == MAP_REMOVED_ENTRY)
                empty_i = i;

            if(entry->index == MAP_EMPTY_ENTRY) {
                if(empty_i != (uint32_t) -1)
                    i = empty_i;
                break; 
            }

            if(entry->hash == hash) {
                void* entry_key = _map_key(map, entry->index, info);
                if(_map_key_equals(entry_key, key, info))
                {
                    found->hash = hash;
                    found->index = entry->index;
                    found->slot = i;
                    return true;
                }
            }
        }
            
        i = (i + k) & map->entries_mask;
    }

    //store key value
    isize added_index = map->count++;
    void* added_key = _map_key(map, added_index, info);
    void* added_val = _map_val(map, added_index, info);
    
    _map_key_store(added_key, key, info);
    memcpy(added_val, value, info.value_size);
    
    //update hash part
    Map_Hash_Entry* entry = &map->entries[i];
    entry->hash = hash;
    entry->index = (uint32_t) added_index;
    map->removed_count -= entry->index == MAP_REMOVED_ENTRY;
    
    //add the back link
    ASSERT(added_index <= map->entries_mask);
    ASSERT(map->entries[added_index].backlink == (uint32_t) -1);
    map->entries[added_index].backlink = i;

    found->hash = hash;
    found->index = added_index;
    found->slot = i;
    return false;
}

bool map_remove_found(Map* map, Map_Found found, Map_Info info)
{
    if(found.index == (uint32_t) -1)
    {
        isize removed_index = found.index;
        isize last_index = map->count - 1;
        if(last_index != removed_index)
        {
            isize last_entry_i = map->entries[last_index].backlink;
            Map_Hash_Entry* last_entry = &map->entries[last_entry_i];
            
            void* removed_key = _map_key(map, removed_index, info);
            void* removed_val = _map_val(map, removed_index, info);

            void* last_key = _map_key(map, last_index, info);
            void* last_val = _map_val(map, last_index, info);

            typedef void (*Destructor)(void* item);
            if(info.key_destructor)   ((Destructor) info.key_destructor)(removed_key);
            if(info.value_destructor) ((Destructor) info.value_destructor)(removed_val);

            memcpy(removed_key, last_key, info.key_size);
            memcpy(removed_val, last_val, info.value_size);
            
            last_entry->index = removed_index;
            map->entries[last_index].backlink = (uint32_t) -1;
        }

        Map_Hash_Entry* removed_entry = &map->entries[found.slot];
        removed_entry->index = MAP_REMOVED_ENTRY;
        map->removed_count += 1;
        map->count -= 1;
        return true;
    }
    return false;
}

void map_init(Map* map, Allocator2* alloc, Map_Info info)
{
    map_deinit(map, info);
    map->alloc = alloc;
}

void map_deinit(Map* map, Map_Info info)
{
    if(map->capacity > 0) {
        (*map->alloc)(map->alloc, 0, map->entries, (map->entries_mask + 1)*sizeof(Map_Hash_Entry), sizeof(Map_Hash_Entry), NULL);
        (*map->alloc)(map->alloc, 0, map->keys, map->capacity*info.key_size, 16, NULL);
    }
    if(map->entries_mask > 0) 
        (*map->alloc)(map->alloc, 0, map->values, map->capacity*info.value_size, 16, NULL);
    memset(map, 0, sizeof* map);
}




INLINE_IMPL bool map_find_iterate(const Map* map, const void* key, Map_Find_Iterator* iterator, Map_Info info)
{
    if(iterator->it.iter == 0)
    {
        uint64_t hash = _map_hash(key, info);
        iterator->it = _map_find_it_make(map, hash);
    }
    return _map_find_next(map, key, &iterator->it, &iterator->found, info);
}

INLINE_IMPL bool map_find(const Map* map, const void* key, Map_Found* found, Map_Info info)
{
    uint64_t hash = _map_hash(key, info);
    Map_Find_It it = _map_find_it_make(map, hash);
    return _map_find_next(map, key, &it, found, info);
}


INLINE_IMPL isize map_remove_all(Map* map, const void* key, Map_Info info)
{
    for(Map_Find_Iterator it = {0}; map_find_iterate(map, key, &it, info); )
        map_remove_found(map, it.found, info);
}

INLINE_IMPL bool map_remove(Map* map, const void* key, Map_Info info)
{
    Map_Found found = {0};
    if(map_find(map, key, &found, info) == false)
        return false;

    map_remove_found(map, found, info);
    return true;
}

INLINE_IMPL Map_Found map_insert(Map* map, const void* key, const void* value, Map_Info info)
{
    uint64_t hash = _map_hash(key, info);
    Map_Found out = {0};
    _map_insert_or_find(map, key, value, &out, hash, true, info);
    return out;
}

INLINE_IMPL bool map_insert_or_find(Map* map, const void* key, const void* value, Map_Found* found, Map_Info info)
{
    uint64_t hash = _map_hash(key, info);
    Map_Found out = {0};
    return _map_insert_or_find(map, key, value, &out, hash, false, info);
}

INLINE_IMPL bool map_insert_or_set(Map* map, const void* key, const void* value, Map_Found* found, Map_Info info)
{
    uint64_t hash = _map_hash(key, info);
    Map_Found out = {0};
    if(_map_insert_or_find(map, key, value, &out, hash, false, info) == false)
    {
        void* val = _map_val(map, out.index, info);
        memcpy(val, value, info.value_size);
        return false;
    }
    return true;
}

INLINE_IMPL void* map_get_found(const Map* map, Map_Found found, Map_Info info)
{
    REQUIRE(found.index < map->count);
    return _map_val(map, found.index, info);
}
INLINE_IMPL void* map_get_found_key(const Map* map, Map_Found found, Map_Info info)
{
    REQUIRE(found.index < map->count);
    return _map_key(map, found.index, info);
}
INLINE_IMPL void map_set_found(Map* map, Map_Found found, const void* value, Map_Info info)
{
    REQUIRE(found.index < map->count);
    void* val = _map_val(map, found.index, info);
    memcpy(val, value, info.value_size);
}

INLINE_IMPL Map_Found map_set(Map* map, const void* key, const void* value, Map_Info info)
{
    Map_Found found = {0};
    map_insert_or_set(map, key, value, &found, info);
    return found;
}

INLINE_IMPL void* map_get_or(const Map* map, const void* key, void* if_not_found, Map_Info info)
{
    Map_Found found = {0};
    if(map_find(map, key, &found, info))
        _map_val(map, found.index, info);

    return if_not_found;
}
INLINE_IMPL void* map_get(const Map* map, const void* key, Map_Info info)
{
    return map_get_or(map, key, NULL, info);
}
