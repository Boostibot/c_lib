#ifndef JOT_MAP
#define JOT_MAP

#include "hash.h"
#include "defines.h"
#include "assert.h"
#include "allocator.h"

#ifndef MAPAPI
    #define MAPAPI static ATTRIBUTE_INLINE_ALWAYS 
    #define JOT_MAP_IMPL
#endif

typedef struct Map {
    union {
        Hash hash;
        Allocator* allocator; 
    };
    
    u8* keys;
    u8* values;

    i32 len;
    i32 capacity;
    
    //Upper estimate for the number of hash collisions in the hash map.
    //A collision is caused by spurious 64 bit hash collision (extremely rare)
    // or by inserting multiple of the same keys into the map (multimap).
    //If no removals were performed is exact count.
    isize max_collision_count;
} Map;

typedef struct Hash_String Hash_String; 
typedef struct Map_Found {
    i32 hash_index;
    i32 hash_probe;
    u64 hash;
    i32 index;

    bool inserted;
    bool _[3];

    //Points to the STORED version of keys/values!
    union {
        void* key;
        u8*  key_u8;
        u16* key_u16;
        u32* key_u32;
        u64* key_u64;
        i8*  key_i8;
        i16* key_i16;
        i32* key_i32;
        i64* key_i64;
        isize* key_isize;
        Hash_String* key_hstring;
    };
    union {
        void* value;
        u8*  value_u8;
        u16* value_u16;
        u32* value_u32;
        u64* value_u64;
        i8*  value_i8;
        i16* value_i16;
        i32* value_i32;
        i64* value_i64;
        isize* value_isize;
        Hash_String* value_hstring;
    };
} Map_Found;

//Note that stored and supplied keys may be different types!
// (ie String_Builder stored_key and String supplied_key or similar).
typedef u64 (*Map_Hash_Func)(const void* stored_key, void* context);
typedef bool (*Map_Key_Eq_Func)(const void* stored_key, const void* supplied_key, void* context);
typedef void (*Map_Store_Func)(const void* stored_key, const void* supplied_key, void* context);
typedef void (*Map_Deinit_Func)(const void* key_or_value, void* context);

typedef struct Map_Interface {
    u64 (*hash)(const void* stored_key, void* context);
    //Checks stored key and supplied key for equality. If is null uses memcmp instead.
    bool (*is_eq_or_null)(const void* stored_key, const void* supplied_key, void* context);

    //Transforms supplied_key into stored_key. If is null uses memcpy instead.
    void (*key_store_or_null)(void* stored_key, const void* supplied_key, void* context);
    void (*value_store_or_null)(void* stored_value, const void* supplied_value, void* context);

    //Gets called on removed keys/values during explicit remove, clear or deinit. If is null does nothing.
    void (*key_deinit_or_null)(void* key, void* context);
    void (*value_deinit_or_null)(void* value, void* context);

    i32 key_size;
    i32 key_align;
    
    //Value size can be zero in which case the hash map effectively becomes a hash set.
    i32 value_size;
    i32 value_align;

    void* context;
} Map_Interface;

MAPAPI void      map_init(Map* map, Allocator* alloc, Map_Interface info);
MAPAPI void      map_deinit(Map* map, Map_Interface info);
MAPAPI void      map_test_invariants(const Map* map, bool slow_checks, Map_Interface info);
MAPAPI void      map_reserve(Map* map, isize num_entries, Map_Interface info);
MAPAPI void      map_clear(Map* map, Map_Interface info);
MAPAPI bool      map_remove_found(Map* map, Map_Found found, Map_Interface info);
MAPAPI i32       map_remove(Map* map, const void* key, u64 hash, Map_Interface info);
MAPAPI Map_Found map_find(const Map* map, const void* key, u64 hash, Map_Interface info);
MAPAPI Map_Found map_find_next(const Map* map, const void* key, Map_Found prev_found, Map_Interface info);
MAPAPI Map_Found map_insert(Map* map, const void* key, u64 hash, const void* value, Map_Interface info);
MAPAPI Map_Found map_find_or_insert(Map* map, const void* key, u64 hash, const void* value, Map_Interface info);
MAPAPI Map_Found map_assign_or_insert(Map* map, const void* key, u64 hash, const void* value, Map_Interface info);

//Macros to allow pointers to temporary
#ifdef __cplusplus
    #define VPTR(Type, val) (&(const Type&) (val))
    #define VVPTR(val)      (&(const decltype(val)&) (val))
#else
    #define VPTR(Type, val) (Type[]){val}
    #define VVPTR(val)      (__typeof__(val)[]){val}
#endif

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_MAP_IMPL)) && !defined(JOT_MAP_HAS_IMPL)
#define JOT_MAP_HAS_IMPL

#ifndef MAP_DEBUG
    #if defined(DO_ASSERTS_SLOW)
        #define MAP_DEBUG 2
    #elif !defined(NDEBUG)
        #define MAP_DEBUG 1
    #else
        #define MAP_DEBUG 0
    #endif
#endif

#define MAPAPI_INTERNAL static ATTRIBUTE_INLINE_ALWAYS

#define MAP_KEY(i)      ((u8*) map->keys + (i)*info.key_size)
#define MAP_VALUE(i)    ((u8*) map->values + (i)*info.value_size)

MAPAPI void map_test_invariants(const Map* map, bool slow_checks, Map_Interface info)
{
    TEST((map->keys == NULL) == (map->values == NULL));
    TEST(0 <= map->len && map->len <= map->capacity);

    TEST(0 <= info.value_size);
    TEST(is_power_of_two_or_zero(info.value_align));
    TEST(0 <= info.key_size);
    TEST(is_power_of_two_or_zero(info.key_align));

    TEST(0 <= map->max_collision_count);
    TEST(map->hash.len == map->len);

    if(map->keys != NULL && info.key_size != 0)
        TEST(map->allocator != NULL);

    if(slow_checks)
    {
        isize all_indexes_sum = 0;
        for(isize i = 0; i < map->hash.entries_count; i++)
        {
            Hash_Entry entry = map->hash.entries[i];    
            if(hash_is_entry_used(entry))
            {
                all_indexes_sum += entry.value; 
                void* key = MAP_KEY(entry.value);
                bool found_this_entry = false;
                for(Map_Found found = map_find(map, key, entry.hash, info); found.index != -1; found = map_find_next(map, key, found, info))
                {
                    if(found.index == entry.value)
                    {
                        found_this_entry = true;
                        break;
                    }
                }

                TEST(found_this_entry, "All keys need to be findable");
            }
        }
        
        isize control_sum = map->len * (map->len - 1) / 2;
        TEST(all_indexes_sum == control_sum);
    }
}


MAPAPI_INTERNAL void _map_check_invariants(const Map* map, Map_Interface info)
{
    int debug_level = MAP_DEBUG;
    if(debug_level > 0)
        map_test_invariants(map, debug_level > 1, info);
}

MAPAPI_INTERNAL Map_Found _map_found_from_hash_found(const Map* map, Hash_Found found, Map_Interface info)
{
    Map_Found out = {found.index, found.probes, found.hash, -1};
    out.inserted = found.inserted;
    if(found.index != -1)
    {
        out.index = (i32) found.value;
        out.key = MAP_KEY(found.value);
        out.value = MAP_VALUE(found.value);
    }
    return out;
}

MAPAPI_INTERNAL void _map_reserve_values(Map* map, isize to_size, Map_Interface info)
{
    if(to_size > map->capacity)
    {
        isize old_capacity = map->capacity;
        isize new_capacity = MAX(map->capacity*3/2 + 8, to_size);

        map->values = allocator_reallocate(map->allocator, new_capacity*info.value_size, map->values, old_capacity*info.value_size, info.value_align);
        map->keys = allocator_reallocate(map->allocator, new_capacity*info.key_size, map->keys, old_capacity*info.key_size, info.key_align);
        map->capacity = (i32) new_capacity;
    }
}

MAPAPI_INTERNAL void _map_push_values(Map* map, const void* key, const void* value, Map_Interface info)
{
    _map_reserve_values(map, map->len + 1, info);
    if(info.key_store_or_null)
        info.key_store_or_null(MAP_KEY(map->len), key, info.context);
    else
        memmove(MAP_KEY(map->len), key, info.key_size);
        
    if(info.value_store_or_null)
        info.value_store_or_null(MAP_VALUE(map->len), value, info.context);
    else
        memmove(MAP_VALUE(map->len), value, info.value_size);
    map->len += 1;
}

MAPAPI_INTERNAL bool _map_key_eq(const void* stored, const void* supplied, Map_Interface info)
{
    if(info.is_eq_or_null)
        return info.is_eq_or_null(stored, supplied, info.context);
    else
        return memcmp(stored, supplied, info.key_size) == 0;
}

MAPAPI_INTERNAL void _map_clear_key_values(Map* map, Map_Interface info)
{
    if(info.key_deinit_or_null)
        for(isize i = 0; i < map->len; i++)
            info.key_deinit_or_null(MAP_KEY(i), info.context);
        
    if(info.value_deinit_or_null)
        for(isize i = 0; i < map->len; i++)
            info.value_deinit_or_null(MAP_VALUE(i), info.context);
}

MAPAPI void map_deinit(Map* map, Map_Interface info)
{
    _map_check_invariants(map, info);
    _map_clear_key_values(map, info);
    allocator_deallocate(map->allocator, map->values, map->capacity*info.value_size, info.value_align);
    allocator_deallocate(map->allocator, map->keys,   map->capacity*info.key_size,   info.key_align);
    hash_deinit(&map->hash);
    memset(map, 0, sizeof* map);
    _map_check_invariants(map, info);
}

MAPAPI void map_init(Map* map, Allocator* alloc, Map_Interface info)
{
    map_deinit(map, info);
    hash_init(&map->hash, alloc);
    _map_check_invariants(map, info);
}

MAPAPI void map_reserve(Map* map, isize num_entries, Map_Interface info)
{
    hash_reserve(&map->hash, num_entries);
    _map_reserve_values(map, num_entries, info);
}

MAPAPI void map_clear(Map* map, Map_Interface info)
{
    _map_check_invariants(map, info);
    _map_clear_key_values(map, info);
    map->max_collision_count = 0;
    map->len = 0;
    hash_clear(&map->hash);
    _map_check_invariants(map, info);
}

MAPAPI Map_Found map_find(const Map* map, const void* key, u64 hash, Map_Interface info)
{
    Hash_Found found = hash_find(map->hash, hash);
    for(; found.index != -1; found = hash_find_next(map->hash, found))
    {
        void* found_key = MAP_KEY(found.value);
        if(_map_key_eq(found_key, key, info))
            break;
    }

    return _map_found_from_hash_found(map, found, info);
}

MAPAPI Map_Found map_find_next(const Map* map, const void* key, Map_Found prev_found, Map_Interface info)
{
    Hash_Found found = {prev_found.hash_index, prev_found.hash_probe, prev_found.hash};
    while(true)
    {
        found = hash_find_next(map->hash, found);
        if(found.index == -1)
            break;

        void* found_key = MAP_KEY(found.value);
        if(_map_key_eq(found_key, key, info))
            break;
    }

    return _map_found_from_hash_found(map, found, info);
}

MAPAPI Map_Found map_insert(Map* map, const void* key, u64 hash, const void* value, Map_Interface info)
{
    _map_check_invariants(map, info);
    Hash_Found found = hash_find_or_insert(&map->hash, hash, map->len);
    if(found.inserted == false)
    {
        map->max_collision_count += 1;
        found = hash_insert_next(&map->hash, found, map->len);
    }

    _map_push_values(map, key, value, info);
    _map_check_invariants(map, info);
    return _map_found_from_hash_found(map, found, info);
}

MAPAPI Map_Found map_find_or_insert(Map* map, const void* key, u64 hash, const void* value, Map_Interface info)
{
    _map_check_invariants(map, info);
    Hash_Found found = hash_find_or_insert(&map->hash, hash, map->len);
    
    bool colided = false;
    while(found.inserted == false)
    {
        void* found_key = MAP_KEY(found.value);
        if(_map_key_eq(found_key, key, info))
            return _map_found_from_hash_found(map, found, info);
        else
        {
            colided = true;
            found = hash_find_or_insert_next(&map->hash, found, map->len);
        }
    }
    
    map->max_collision_count += colided;
    _map_push_values(map, key, value, info);
    _map_check_invariants(map, info);
    return _map_found_from_hash_found(map, found, info);
}

MAPAPI Map_Found map_assign_or_insert(Map* map, const void* key, u64 hash, const void* value, Map_Interface info)
{
    _map_check_invariants(map, info);
    Hash_Found found = hash_find_or_insert(&map->hash, hash, map->len);

    bool colided = false;
    while(found.inserted == false)
    {
        void* found_key = MAP_KEY(found.value);
        if(_map_key_eq(found_key, key, info))
        {
            if(info.value_store_or_null)
            {
                if(info.value_deinit_or_null)
                    info.value_deinit_or_null(MAP_VALUE(found.value), info.context);
                info.value_store_or_null(MAP_VALUE(found.value), value, info.context);
            }
            else
                memcpy(MAP_VALUE(found.value), value, info.value_size);
            return _map_found_from_hash_found(map, found, info);
        }
        else
        {
            colided = true;
            found = hash_find_or_insert_next(&map->hash, found, map->len); 
        }
    }
    
    map->max_collision_count += colided;
    _map_push_values(map, key, value, info);
    _map_check_invariants(map, info);
    return _map_found_from_hash_found(map, found, info);
}

MAPAPI bool map_remove_found(Map* map, Map_Found found, Map_Interface info)
{
    if(found.hash_index >= 0)
    {
        ASSERT(map->len > 0);
        _map_check_invariants(map, info);
        
        void* rem_key = MAP_KEY(found.index);
        void* rem_value = MAP_VALUE(found.index);
        if(info.key_deinit_or_null)
            info.key_deinit_or_null(rem_key, info.context);
        
        if(info.value_deinit_or_null)
            info.value_deinit_or_null(rem_value, info.context);

        //If removing item not on top of the items stack do the classing swap to top and pop.
        //We however have to make sure the hash remains properly updated. 
        i32 last_i = map->len - 1;
        if(found.index != last_i)
        {
            //Find the hash of the item at the top of the hash and
            //relink it to point to the removed slot instead.
            //We dont have to check any keys while doing this, instead can use the index it points to
            // for identification.
            u64 last_hash = info.hash(MAP_KEY(last_i), info.context);

            bool relinked = false;
            for(Hash_Found last_found = hash_find(map->hash, last_hash); last_found.index != -1; last_found = hash_find_next(map->hash, last_found))
            {
                if((i32) last_found.value == last_i)
                {   
                    last_found.entry->value = (u64) found.index;
                    relinked = true;
                    break;
                }
            }
            ASSERT(relinked);

            memcpy(rem_key, MAP_KEY(last_i), info.key_size);
            memcpy(rem_value, MAP_VALUE(last_i), info.value_size);
        }
        hash_remove_found(&map->hash, found.hash_index);

        map->len -= 1;
        _map_check_invariants(map, info);
    }

    return found.hash_index >= 0;
}

MAPAPI i32 map_remove(Map* map, const void* key, u64 hash, Map_Interface info)
{
    _map_check_invariants(map, info);
    i32 removed = 0;
    for(Map_Found found = map_find(map, key, hash, info); found.index != -1; found = map_find_next(map, key, found, info))
    {
        map_remove_found(map, found, info);
        removed += 1;

        if(map->max_collision_count == 0)
            break;
    }
    
    _map_check_invariants(map, info);
    return removed;
}

#undef MAP_KEY
#undef MAP_VALUE

#endif