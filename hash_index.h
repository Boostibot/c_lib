

#ifndef JOT_HASH_INDEX
#define JOT_HASH_INDEX

#define JOT_ALL_IMPL

#include "allocator.h"

// A simple and flexible linear-probing-hash style hash index.
// 
// We use the term hash index as opposed to hash table because this structure
// doesnt provide the usual hash table ineterface, it marely stores indeces or pointers
// to the key-value data elsewhere.
//
// =================== REASONING ====================
// 
// This approach has certain benefits most importantly enabling to simply
// write SQL style tables where every single row value can have 
// its own accelarating Hash_Index (thats where the name comes from). 
// Consider the following table:
//
// OWNER     AGE   NAME         ANIMAL   BIG_CHUNK_OF_DATA
// "Alice"   7     "Timotheo"   cat      /* blob */
// "Bob"     3     "Neo"        dog      /* blob */
// ....
// 
// We wish to query rows in O(1) time by owner, name and age. 
// How do we achieve this with traditional key value hash table? 
// If the key is OWNER then the owner field will be missing from the row 
// (which is bad when we query by NAME). If the key is NAME the name 
// field will be missing. The only solution is to have 3 hash tables:
// 
//   owner_table: OWNER -> index
//   name_table:  NAME -> index
//   age_table:   AGE -> index
//
// and one array:
// 
//   rows_array: Array<(OWNER, AGE, NAME, ANIMAL, BIG_CHUNK_OF_DATA)>
// 
// Now the problem is we are effectively duplicating most of the data in the table!
// This is clearly pointless and impractical especially when its fields need to be updated! 
// 
// Our approach solves this problem by only considering the hashes of the fields 
// and not the fields themselves. So we have:
// 
//   owner_index: hash -> index
//   name_index:  hash -> index
//   age_index:   hash -> index
//   rows_array:  Array<(OWNER, AGE, NAME, ANIMAL, BIG_CHUNK_OF_DATA)>
// 
// Now whenever we lookup row by owner we first hash the owner field
// (by whtever but always the same hash algorhitm) then use the owner_index 
// index to find the row. 
// 
// Only caveat being a hash colision might occur so we need to remmeber to always check if the 
// row we were looking for is the one we actually got! Because of this each table will most often implement its
// own functions (find_by_owner(...), find_by_name(...), find_by_age(...)...).
// 
// See hash_table.h for a simple String -> Any hash table implementation
// 
// =================== IMPLEMENTATION ====================
// 
// We have a simple dynamic array of tuples containing the hashed key and value.
// Hashed key is 64 bit number (can be 32 bit for better perf). Value is usually 
// an index but often we want to store pointers. Thus we again use a 64 bit field.
// 
// Again if we wanted the best performance (as opposed to usability in C) we would
// do 32 bit hash and 32 bit value which is an index. This would gratley improve the 
// cache utilization and thus the lookup speed on larger indeces.
// 
// We do linear probing to find our entries. We use two special values of hashes
// namely 0 (EMPTY) and 1 (GRAVESTONE) to mark empty spots. We rehash really soon
// (on 50% utilization) to prevent pitfalls of linear probing. 
// See implementation for details.

#define HASH_INDEX_EMPTY        ((uint64_t) 0x2 << 62)
#define HASH_INDEX_GRAVESTONE   ((uint64_t) 0x1 << 62)
#define HASH_INDEX_VALUE_MAX    (~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) //can be used as invalid value

typedef struct Hash_Index_Entry {
    uint64_t hash;
    uint64_t value;
} Hash_Index_Entry;

typedef struct Hash_Index {
    Allocator* allocator;                
    Hash_Index_Entry* entries;                          
    int32_t size;                        
    int32_t entries_count; 
    int32_t entries_removed;

    int8_t load_factor;
    int8_t load_factor_removed;
    uint16_t hash_collisions;   //purely informative. 

    //Is very possible that hash_collisions overflows for big Hash_Index and 
    //in that case a function for calculating hash collisions should be called.
} Hash_Index;

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator);
EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent, isize load_factor_removed_percent);
EXPORT void  hash_index_deinit(Hash_Index* table);
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table);
EXPORT void  hash_index_clear(Hash_Index* to_table);
EXPORT isize hash_index_find(Hash_Index table, uint64_t hash);
EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found);
EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted);
EXPORT void  hash_index_rehash(Hash_Index* table, isize to_size); //rehashes 
EXPORT void  hash_index_reserve(Hash_Index* table, isize to_size); //reserves space such that inserting up to to_size elements will not trigger rehash
EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value);

EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found);
EXPORT bool  hash_index_is_invariant(Hash_Index table);
EXPORT bool  hash_index_is_entry_used(Hash_Index_Entry entry);

EXPORT uint64_t hash_index_escape_value(uint64_t val);
EXPORT uint64_t hash_index_escape_ptr(const void* val);
EXPORT void*    hash_index_restore_ptr(uint64_t val);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_INDEX_IMPL)) && !defined(JOT_HASH_INDEX_HAS_IMPL)
#define JOT_HASH_INDEX_HAS_IMPL

    INTERNAL isize _hash_index_find_from(Hash_Index table, uint64_t hash, uint64_t start_from)
    {
        if(table.entries_count <= 0)
            return -1;

        ASSERT(table.size + table.entries_removed < table.entries_count && "must not be completely full!");
        uint64_t mod = (uint64_t) table.entries_count - 1;
        uint64_t counter = 0;
        uint64_t i = start_from & mod;

        for(;;)
        {
            //if((table.entries[i].value & HASH_INDEX_EMPTY) != 0 || counter >= table.entries_count)
            if((table.entries[i].value & HASH_INDEX_EMPTY) != 0)
                return -1;

            if((table.entries[i].value & HASH_INDEX_GRAVESTONE) == 0 && table.entries[i].hash == hash)
                return i;
                
            ASSERT(counter < (uint64_t) table.entries_count && "must not be completely full!");
            counter += 1; 
            i = (i + counter) & mod;
        }
    }

    INTERNAL isize _hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value, bool stop_if_found) 
    {
        ASSERT(table->size + table->entries_removed < table->entries_count && "there must be space for insertion");
        ASSERT(table->entries_count > 0);

        uint64_t mod = (uint64_t) table->entries_count - 1;
        uint64_t counter = 0;
        uint64_t i = hash & mod;
        uint64_t insert_index = (uint64_t) -1;
        for(;;)
        {
            //@NOTE: While stop_if_found we need to traverse even gravestone entries to find the 
            // true entry if there is one. If not found we would then place the entry to the next slot.
            // That would however mean we would never replace any gravestones while using stop_if_found.
            // We keep insert_index that gets set to the last gravestone. This is most of the fine also the 
            // first and thus optimal gravestone. No matter the case we replace some gravestone which keeps
            // us from rehashing too much.
            if(stop_if_found)
            {
                if(table->entries[i].hash == hash)
                    return i;
                
                if((table->entries[i].value & HASH_INDEX_GRAVESTONE) != 0)
                    insert_index = i;

                if((table->entries[i].value & HASH_INDEX_EMPTY) != 0)
                    break;
            }
            else
            {
                if((table->entries[i].value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) != 0)
                    break;
            }

            ASSERT(counter < (uint64_t) table->entries_count && "must not be completely full!");
            counter += 1; 
            i = (i + counter) & mod;
        }
        
        if(insert_index == -1)
            insert_index = i;

        //If writing over a gravestone reduce the removed counter
        if(table->entries[insert_index].hash & HASH_INDEX_GRAVESTONE)
        {
            ASSERT(table->entries_removed > 0);
            table->entries_removed -= 1;
        }

        //Clear the empty and gravestone bits so that it does not interfere with bookkeeping
        table->entries[insert_index].value = value & ~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE);
        table->entries[insert_index].hash = hash;
        table->size += 1;

        //Saturating add of new_counter
        uint64_t new_counter = MIN(table->hash_collisions + counter, UINT16_MAX);
        table->hash_collisions = (uint16_t) new_counter;
        ASSERT(hash_index_is_invariant(*table));

        return insert_index;
    }
    
    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        //can also be memset to byte pattern that has HASH_INDEX_EMPTY.
        for(int32_t i = 0; i < to_table->entries_count; i++)
        {
            to_table->entries[i].hash = 0;
            to_table->entries[i].value = HASH_INDEX_EMPTY;
        }

        to_table->hash_collisions = 0;
        to_table->entries_removed = 0;
        to_table->size = 0;
    }
    
    EXPORT bool _hash_index_needs_rehash(isize current_size, isize to_size, isize load_factor)
    {
        return to_size * 100 >= current_size * load_factor;
    }

    INTERNAL void _hash_index_rehash(Hash_Index* to_table, Hash_Index from_table, isize to_size)
    {
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));

        if(_hash_index_needs_rehash(to_table->entries_count, to_size, to_table->load_factor))
        {   
            if(to_table->allocator == NULL)
                hash_index_init(to_table, NULL);

            int32_t rehash_to = 16;
            while(_hash_index_needs_rehash(rehash_to, to_size, to_table->load_factor))
                rehash_to *= 2;
                
            //Cannot shrink beyond needed size
            while(_hash_index_needs_rehash(rehash_to, from_table.size, to_table->load_factor))
                rehash_to *= 2;

            int32_t elem_size = sizeof(Hash_Index_Entry);
            to_table->entries = (Hash_Index_Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, DEF_ALIGN, SOURCE_INFO());
            to_table->entries_count = rehash_to;
        }
        
        hash_index_clear(to_table);
        
        for(isize i = 0; i < from_table.entries_count; i++)
        {
            Hash_Index_Entry curr = from_table.entries[i];
            if((curr.value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0)
                _hash_index_find_or_insert(to_table, curr.hash, curr.value, false);
        }

        ASSERT(hash_index_is_invariant(*to_table));
    }
    
    EXPORT bool hash_index_is_invariant(Hash_Index table)
    {
        bool ptr_size_inv = (table.entries == NULL) == (table.entries_count == 0);
        bool allocator_inv = true;
        if(table.entries != NULL)
            allocator_inv = table.allocator != NULL;

        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.entries_removed >= 0; 
        //bool not_full_inv = true;
        bool not_full_inv = (table.size + table.entries_removed < table.entries_count) || table.entries_count == 0;
        bool cap_inv = is_power_of_two_or_zero(table.entries_count);
        bool find_inv = true;

        #ifndef NDEBUG
        #define HASH_INDEX_SLOW_DEBUG 1
        #else
        #define HASH_INDEX_SLOW_DEBUG 0
        #endif // !NDEBUG

        if(HASH_INDEX_SLOW_DEBUG)
        {
            int32_t used_count = 0;
            for(int32_t i = 0; i < table.entries_count; i++)
            {
                Hash_Index_Entry* entry = &table.entries[i];
                if(hash_index_is_entry_used(*entry))
                {
                    find_inv = find_inv && hash_index_find(table, entry->hash) != -1;
                    ASSERT(find_inv);
                    used_count += 1;
                }
            }

            sizes_inv = sizes_inv && used_count == table.size;
            ASSERT(sizes_inv);
        }

        bool final_inv = ptr_size_inv && allocator_inv && cap_inv && sizes_inv && not_full_inv && find_inv;
        ASSERT(final_inv);
        return final_inv;
    }
    
    EXPORT isize hash_index_find(Hash_Index table, uint64_t hash)
    {
        return _hash_index_find_from(table, hash, hash);
    }
    
    EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found)
    {
        CHECK_BOUNDS(prev_found, table.entries_count);
        return _hash_index_find_from(table, hash, prev_found + 1);
    }

    EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent, isize load_factor_removed_percent)
    {
        hash_index_deinit(table);
        table->allocator = allocator;
        if(table->allocator == NULL)
            table->allocator = allocator_get_default();
        
        table->load_factor = (int8_t) load_factor_percent;
        table->load_factor_removed = (int8_t) load_factor_removed_percent;
        if(table->load_factor <= 0 || table->load_factor >= 100)
            table->load_factor = 75;
        if(table->load_factor_removed <= 0 || table->load_factor_removed >= 100)
            table->load_factor_removed = table->load_factor;
    }

    EXPORT void hash_index_init(Hash_Index* table, Allocator* allocator)
    {
        hash_index_init_load_factor(table, allocator, -1, -1);
    }   
    
    EXPORT void hash_index_deinit(Hash_Index* table)
    {
        ASSERT(hash_index_is_invariant(*table));
        allocator_deallocate(table->allocator, table->entries, table->entries_count * sizeof *table->entries, DEF_ALIGN, SOURCE_INFO());
        
        Hash_Index null = {0};
        *table = null;
    }

    EXPORT void hash_index_copy(Hash_Index* to_table, Hash_Index from_table)
    {
        _hash_index_rehash(to_table, from_table, from_table.size);
    }

    EXPORT void hash_index_rehash(Hash_Index* table, isize to_size)
    {
        Hash_Index rehashed = {0};
        hash_index_init_load_factor(&rehashed, table->allocator, table->load_factor, table->load_factor_removed);
        _hash_index_rehash(&rehashed, *table, to_size);
        hash_index_deinit(table);
        *table = rehashed;
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(_hash_index_needs_rehash(table->entries_count, to_size + table->entries_removed, table->load_factor))
            hash_index_rehash(table, to_size);
    }
    
    EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted)
    {
        hash_index_reserve(table, table->size + 1);
        return _hash_index_find_or_insert(table, hash, value_if_inserted, true);
    }

    EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value)
    {
        hash_index_reserve(table, table->size + 1);
        return _hash_index_find_or_insert(table, hash, value, false);
    }

    EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found)
    {
        ASSERT(table->size > 0);
        CHECK_BOUNDS(found, table->entries_count);
        Hash_Index_Entry removed = table->entries[found];
        table->entries[found].value = HASH_INDEX_GRAVESTONE;
        table->size -= 1;
        table->entries_removed += 1;
        ASSERT(hash_index_is_invariant(*table));

        return removed;
    }
    
    EXPORT bool hash_index_is_entry_used(Hash_Index_Entry entry)
    {
        //is not empty nor gravestone
        return (entry.value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0;
    }

    EXPORT uint64_t hash_index_escape_value(uint64_t val)
    {
        return (val & ~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE));
    }
    
    EXPORT uint64_t hash_index_escape_ptr(const void* val)
    {
        return hash_index_escape_value((uint64_t) val);
    }
    
    EXPORT void* hash_index_restore_ptr(uint64_t val)
    {
        //We use the higher bits from ptr dummy to restore 
        // the value to valid ptr
        static int dummy_ = 0;
        uint64_t dummy = (uint64_t) &dummy_;
        uint64_t mask = HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE;
        uint64_t restored = (val & ~mask) | (dummy & mask);

        return (void*) restored;
    }
#endif
