#ifndef JOT_HASH_TABLE
#define JOT_HASH_TABLE

#include "hash_index.h"
#include "assert.h"
#include "string.h"
#include "hash.h"

//Very expensive check that iterates all keys and checks if they can be found using 
// the internal hash. DO NOT USE UNLESS DEBUGGING!
#ifdef DO_ASSERTS_SLOW
#define DO_HASH_TABLE_CONSISTENCY_CHECKS
#endif

// Implements a String -> Value_Type hash table using Hash_Index as a base.
// Any other tables can be implemented more or less the same.
#define DEFINE_HASH_TABLE_TYPE(Value_Type, Hash_Table_Type_Name) \
    typedef struct {            \
        Hash_Index index;       \
        String_Builder* keys;   \
        Value_Type* values;     \
        i32 size;               \
        i32 capacity;           \
                                \
        i32 value_type_size; /* size in bytes of the Value_Type */   \
        i32 hash_collisions; /* number of missplaced keys in the index */    \
        u64 seed;            /* seed used to seed all hashes. Can be only changed during rehashing or init @TODO*/   \
    } Hash_Table_Type_Name;     \

DEFINE_HASH_TABLE_TYPE(uint8_t,  u8_Hash_Table);
DEFINE_HASH_TABLE_TYPE(uint16_t, u16_Hash_Table);
DEFINE_HASH_TABLE_TYPE(uint32_t, u32_Hash_Table);
DEFINE_HASH_TABLE_TYPE(uint64_t, u64_Hash_Table);

DEFINE_HASH_TABLE_TYPE(int8_t,   i8_Hash_Table);
DEFINE_HASH_TABLE_TYPE(int16_t,  i16_Hash_Table);
DEFINE_HASH_TABLE_TYPE(int32_t,  i32_Hash_Table);
DEFINE_HASH_TABLE_TYPE(int64_t,  i64_Hash_Table);

DEFINE_HASH_TABLE_TYPE(float,    f32_Hash_Table);
DEFINE_HASH_TABLE_TYPE(double,   f64_Hash_Table);
DEFINE_HASH_TABLE_TYPE(void*,    ptr_Hash_Table);

typedef i64_Hash_Table isize_Hash_Table;
typedef u64_Hash_Table usize_Hash_Table;

typedef struct Hash_Found
{   
    u64 hash;
    isize entry;
    isize finished_at;
    isize hash_index;
} Hash_Found;

//Hashes the string with the given seed
EXPORT u64         hash_string(String string, u64 seed);

//Hashes the builder with the given seed
EXPORT u64         hash_builder(String_Builder builder, u64 seed);

//Initializes the hash_table with allocator, value type and seed. value_type_size can be 0.
//Zero initialization is still possible and results in a valid hash_table.
EXPORT void        hash_table_init(void* table, Allocator* alllocator, isize value_type_size, u64 seed);

//Deinitializes the hash_table freeing all memory. Values have to be deinit manually before calling this function!
EXPORT void        hash_table_deinit(void* table);

//Resets the table without freeing any memory making it appear as if it was just init.
EXPORT void        hash_table_clear(void* table);

//Potentially allocates and rehashes so that up to to_fit_entries can be contained within the 
// table without causing reallocation nor rehash.
EXPORT void        hash_table_reserve(void* table, isize to_fit_entries);

//Inserts new Key-Value pair into the table and return its Hash_Found. 
//Can be used to insert multiple of the same key!
EXPORT Hash_Found  hash_table_insert(void* table, String key, const void* value);

//Removes a Key-Value pair from the table and returns true. If the key was not found returns false and does nothing.
//Saves the removed value into removed_value (only if removed_value is not NULL)
EXPORT bool        hash_table_remove(void* _table, String key, void* removed_value);

//Removes a Key-Value pair from the table and returns a Hash_Found to the the entry swapped into its place. 
//The returned Hash_Found can have entry index equal to -1 in the case when the passed in found is invalid
//or the found is refering to the last entry and thus no swapping is performed.
//Saves the removed value into removed_value (only if removed_value is not NULL).
EXPORT Hash_Found  hash_table_remove_found(void* _table, Hash_Found found, void* removed_value);

//Returns a pointer to the first value associeted with the key. If the key is not found returns NULL
EXPORT void*       hash_table_get(const void* table, String key); 

//Returns a pointer to the first value associeted with the key
EXPORT Hash_Found  hash_table_find(const void* table, String key);

//Returns a pointer to the first value associeted with the key starting from prev_found. 
//Is inteded to be used to iterate all mathcing keys in a multimap.
EXPORT Hash_Found  hash_table_find_next(const void* table, String key, Hash_Found prev_found);

//Returns a pointer to the first value associeted with the key. If the key was not found creates it.
//Saves wheter the value was cretaed or found into was_found. If was_found is NULL does not save anything.
EXPORT void*       hash_table_get_or_make(void* table, String key, bool* was_found); 

//Returns a Hash_Found to the first value associeted with the key. If the key was not found creates it.
//Saves wheter the value was cretaed or found into was_found. If was_found is NULL does not save anything.
EXPORT Hash_Found  hash_table_find_or_make(void* _table, String key, bool* was_found);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_TABLE_IMPL)) && !defined(JOT_HASH_TABLE_HAS_IMPL)
#define JOT_HASH_TABLE_HAS_IMPL

EXPORT u64 hash_string(String string, u64 seed)
{
    return hash64_murmur(string.data, string.size, seed);
}

EXPORT u64 hash_builder(String_Builder builder, u64 seed)
{
    return hash64_murmur(builder.data, builder.size, seed);
}

INTERNAL bool _hash_table_is_invarinat(const void* _table)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    #ifdef DO_HASH_TABLE_CONSISTENCY_CHECKS
        //all entries should be findable 
        for(isize i = 0; i < table->size; i++)
        {
            String key = table->keys[i].string;
            Hash_Found found = hash_table_find(table, key);
            CHECK_BOUNDS(found.entry, table->size);
            CHECK_BOUNDS(found.hash_index, table->index.entries_count);
            //ASSERT(found.hash_index == found.finished_at);
            ASSERT(found.hash == hash_string(key, table->seed));

            Hash_Index_Entry entry = table->index.entries[found.hash_index];
            ASSERT(found.hash == entry.hash);
            ASSERT((isize) entry.value == i && "The hash index must point back to the original entry");
        }
    #endif

    bool size_match = table->size == table->index.size && table->size >= 0;
    bool capacities_match = table->size <= table->capacity;
    bool key_data_inv = (table->keys != NULL) == (table->capacity != 0);
    bool value_data_inv = (table->values != NULL) == (table->capacity != 0);
    bool result = size_match && capacities_match && key_data_inv && value_data_inv;
    ASSERT(result);
    return result;
}

INTERNAL void* _hash_table_get_value(const void* _table, isize index)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    void* out = table->values + index*table->value_type_size;
    return out;
}
EXPORT void hash_table_init(void* _table, Allocator* alllocator, isize value_type_size, u64 seed)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    memset(table, 0, sizeof table);
    hash_index_init(&table->index, alllocator);
    table->seed = seed;
    table->value_type_size = (i32) value_type_size;
    ASSERT_SLOW(_hash_table_is_invarinat(table));
}

EXPORT void hash_table_deinit(void* _table)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    
    for(isize i = 0; i < table->size; i++)
        array_deinit(&table->keys[i]);

    if(table->capacity > 0)
    {
        ASSERT(table->index.allocator != NULL);
        allocator_deallocate(table->index.allocator, table->keys, table->capacity * sizeof(String_Builder), DEF_ALIGN);
        allocator_deallocate(table->index.allocator, table->values, table->capacity * table->value_type_size, DEF_ALIGN);
    }
    
    hash_index_deinit(&table->index);
    memset(table, 0, sizeof table);
}

EXPORT void hash_table_clear(void* _table)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    hash_index_clear(&table->index);
    table->size = 0;
}

EXPORT Hash_Found hash_table_find(const void* _table, String key)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    Hash_Found found = {0};
    found.hash = hash_string(key, table->seed);
    found.hash_index = hash_index_find_first(table->index, found.hash, &found.finished_at);
    found.entry = -1;

    while(found.hash_index != -1)
    {
        isize entry = table->index.entries[found.hash_index].value;
        String found_key = table->keys[entry].string;
        if(string_is_equal(found_key, key))
        {
            found.entry = entry;
            break;
        }
        else
        {
            found.hash_index = hash_index_find_next(table->index, found.hash, found.hash_index, &found.finished_at);
        }
    }

    return found;
}

EXPORT Hash_Found hash_table_find_next(const void* _table, String key, Hash_Found prev_found)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    
    Hash_Found found = prev_found;
    while(found.hash_index != -1)
    {
        found.hash_index = hash_index_find_next(table->index, found.hash, found.hash_index, &found.finished_at);
        if(found.hash_index == -1)
            break;

        isize entry = table->index.entries[found.hash_index].value;
        String found_key = table->keys[entry].string;
        if(string_is_equal(found_key, key))
        {
            found.entry = entry;
            break;
        }
    }
    
    return found;
}

EXPORT void hash_table_reserve(void* _table, isize to_fit_entries)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    
    if(hash_index_needs_rehash(table->index, to_fit_entries))
        table->hash_collisions = (i32) hash_index_rehash(&table->index, to_fit_entries);

    if(to_fit_entries >= table->capacity)
    {
        i32 old_cap = table->capacity;
        i32 new_cap = old_cap * 2;
        if(new_cap < 8)
            new_cap = 8;

        isize key_size = sizeof(String_Builder);
        isize val_size = table->value_type_size;

        table->keys = (String_Builder*) allocator_reallocate(table->index.allocator, new_cap * key_size, table->keys, old_cap * key_size, DEF_ALIGN);
        table->values = (u8*) allocator_reallocate(table->index.allocator, new_cap * val_size, table->values, old_cap * val_size, DEF_ALIGN);

        table->capacity = new_cap;
    }
    ASSERT_SLOW(_hash_table_is_invarinat(table));
}

EXPORT Hash_Found hash_table_insert(void* _table, String key, const void* value)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    hash_table_reserve(table, table->size + 1);

    table->keys[table->size] = builder_from_string(key, table->index.allocator);
    memmove(_hash_table_get_value(table, table->size), value, table->value_type_size);

    Hash_Found found = {0};
    found.hash = hash_string(key, table->seed);
    found.hash_index = hash_index_insert(&table->index, found.hash, table->size);
    found.finished_at = found.hash_index;
    
    table->size += 1;
    
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    return found;
}

EXPORT void* hash_table_get_or_make(void* table, String key, bool* was_found)
{
    Hash_Found found = hash_table_find_or_make(table, key, was_found);
    void* val = _hash_table_get_value(table, found.entry);
    return val;
}
EXPORT Hash_Found hash_table_find_or_make(void* _table, String key, bool* was_found)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    hash_table_reserve(table, table->size + 1);
    ASSERT_SLOW(_hash_table_is_invarinat(table));

    //find the key
    Hash_Found found = hash_table_find(table, key);
    
    //report if it was found or not
    if(was_found != NULL)
        *was_found = found.entry != -1;

    //if it was not found add a new zeroed entries and add them
    // to index
    if(found.entry == -1)
    {
        //place it where it ended
        found.entry = table->size;
        found.hash_index = found.finished_at;
        
        array_init(&table->keys[table->size], table->index.allocator);
        builder_append(&table->keys[table->size], key);
        memset(_hash_table_get_value(table, table->size), 0, table->value_type_size);

        table->index.entries[found.hash_index].hash = found.hash;
        table->index.entries[found.hash_index].value = found.entry;
        table->index.size += 1;
        table->size += 1;
    }
    
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    return found;
}

EXPORT bool hash_table_remove(void* _table, String key, void* removed_value)
{
    Hash_Found found = hash_table_find(_table, key);
    if(found.entry < 0)
        return false;

    hash_table_remove_found(_table, found, removed_value);
    return true;
}
    
EXPORT Hash_Found hash_table_remove_found(void* _table, Hash_Found found, void* removed_value)
{
    Hash_Found found_last = {0};
    found_last.entry = -1;
    if(found.entry < 0)
        return found_last;
        
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    ASSERT(table->size >= 1 && "Cannot remove from empty!");
    ASSERT_SLOW(_hash_table_is_invarinat(table));
    
    //Save removed value if desired
    if(removed_value != NULL)
        memmove(removed_value, _hash_table_get_value(table, found.entry), table->value_type_size);

    //Deinit the key
    array_deinit(table->keys + found.entry);

    //if we are not removing the last entry
    //removes it by swapping it with the last entry.
    isize last = table->size - 1;
    if(found.entry != last)
    {
        String last_key = table->keys[last].string;
        found_last = hash_table_find(table, last_key);
        ASSERT(found_last.entry != -1);

        table->keys[found.entry] = table->keys[last];
        memmove(_hash_table_get_value(table, found.entry), _hash_table_get_value(table, last), table->value_type_size);

        table->index.entries[found_last.hash_index].value = found.entry;
        found_last.entry = found.entry;
    }
    
    hash_index_remove(&table->index, found.hash_index);
    table->size --;
    ASSERT_SLOW(_hash_table_is_invarinat(table));

    return found_last;
}

EXPORT void* hash_table_get(const void* _table, String key)
{
    u8_Hash_Table* table = (u8_Hash_Table*) _table;
    Hash_Found found = hash_table_find(table, key);
    void* out = NULL;
    if(found.entry != -1)
        out = table->values + found.entry*table->value_type_size;

    return out;
}

#endif