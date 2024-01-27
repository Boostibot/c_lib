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
//
// @TODO: Implement robin-hood hashing for better percantage utilization 
// @TODO: Implement a 32 bit variant for faster lookup (was not needed yet)

#define HASH_INDEX_EMPTY        ((uint64_t) 0x2 << 62)
#define HASH_INDEX_GRAVESTONE   ((uint64_t) 0x1 << 62)



typedef struct Hash_Index_Entry {
    uint64_t hash;
    uint64_t value;
} Hash_Index_Entry;

typedef struct Hash_Index {
    Allocator* allocator;                
    Hash_Index_Entry* entries;                          
    int32_t size;                        
    int32_t entries_count;                   
} Hash_Index;

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator);
EXPORT void  hash_index_deinit(Hash_Index* table);
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table);
EXPORT void  hash_index_clear(Hash_Index* to_table);
EXPORT isize hash_index_find(Hash_Index table, uint64_t hash);
EXPORT isize hash_index_find_first(Hash_Index table, uint64_t hash, isize* finished_at);
EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found, isize* finished_at);
EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted);
EXPORT isize hash_index_rehash(Hash_Index* table, isize to_size); //rehashes 
EXPORT void  hash_index_reserve(Hash_Index* table, isize to_size); //reserves space such that inserting up to to_size elements will not trigger rehash
EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value);
EXPORT bool  hash_index_needs_rehash(Hash_Index table, isize to_size);

EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found);
EXPORT bool  hash_index_is_invariant(Hash_Index table);
EXPORT bool  hash_index_is_entry_used(Hash_Index_Entry entry);

EXPORT uint64_t hash_index_escape_value(uint64_t val);
EXPORT uint64_t hash_index_escape_ptr(const void* val);
EXPORT void*    hash_index_restore_ptr(uint64_t val);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_INDEX_IMPL)) && !defined(JOT_HASH_INDEX_HAS_IMPL)
#define JOT_HASH_INDEX_HAS_IMPL

    INTERNAL isize _lin_probe_hash_find_from(const Hash_Index_Entry* entries, isize entries_size, uint64_t hash, isize prev_index, isize* finished_at)
    {
        if(entries_size <= 0)
        {
            *finished_at = 0;
            return -1;
        }

        uint64_t mask = (uint64_t) entries_size - 1;
        uint64_t i = prev_index & mask;
        
        //while not found empty slot && didnt make a full rotation (c < entries_size) 
        for(isize c = 0; c < entries_size && (entries[i].value & HASH_INDEX_EMPTY) == 0; i = (i + 1) & mask, c ++)
        {
            if(entries[i].hash == hash && (entries[i].value & HASH_INDEX_GRAVESTONE) == 0)
            {
                *finished_at = i;
                return i;
            }
        }

        *finished_at = i;
        return -1;
    }

    INTERNAL isize _lin_probe_hash_rehash(Hash_Index_Entry* new_entries, isize new_entries_size, const Hash_Index_Entry* entries, isize entries_size)
    {  
        if(entries_size > 0)
            ASSERT(new_entries_size != 0);
        
        for(int32_t i = 0; i < new_entries_size; i++)
        {
            new_entries[i].hash = 0;
            new_entries[i].value = HASH_INDEX_EMPTY;
        }
        
        int32_t had_used = 0;
        isize hash_colisions = 0;
        uint64_t mask = (uint64_t) new_entries_size - 1;
        for(isize i = 0; i < entries_size; i++)
        {
            Hash_Index_Entry curr = entries[i];
            if((curr.value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0)
            {
                had_used += 1;
                uint64_t k = curr.hash & mask;
                isize counter = 0;
                for(; (new_entries[k].value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0; k = (k + 1) & mask)
                {
                    hash_colisions += 1;
                    ASSERT(counter < new_entries_size && "must not be completely full!");
                    ASSERT(counter < entries_size && "its impossible to have more then what we started with");
                    counter += 1;
                }

                ASSERT(new_entries[k].hash == 0);
                new_entries[k] = curr;
            }
        }

        return hash_colisions;
    }   

    INTERNAL isize _lin_probe_hash_insert(Hash_Index_Entry* entries, isize entries_size, uint64_t hash, uint64_t value) 
    {
        ASSERT(entries_size > 0 && "there must be space for insertion");
        uint64_t mask = (uint64_t) entries_size - 1;
    
        uint64_t i = hash & mask;
        isize counter = 0;

        //while not empty or gravestone
        for(; (entries[i].value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0; i = (i + 1) & mask)
            ASSERT(counter ++ < entries_size && "must not be completely full!");

        //Clear the empty and gravestone bits so that it does not interfere with bookkeeping
        entries[i].value = value & ~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE);
        entries[i].hash = hash;
        return i;
    }
    
    INTERNAL void _lin_probe_hash_remove(Hash_Index_Entry* entries, isize entries_size, isize found) 
    {
        CHECK_BOUNDS(found, entries_size);

        Hash_Index_Entry* found_entry = &entries[found];
        found_entry->value = HASH_INDEX_GRAVESTONE;
    }

    EXPORT void hash_index_deinit(Hash_Index* table)
    {
        ASSERT(hash_index_is_invariant(*table));
        allocator_deallocate(table->allocator, table->entries, table->entries_count * sizeof *table->entries, DEF_ALIGN, SOURCE_INFO());
        
        Hash_Index null = {0};
        *table = null;
    }

    EXPORT void hash_index_init(Hash_Index* table, Allocator* allocator)
    {
        hash_index_deinit(table);
        table->allocator = allocator;
        if(table->allocator == NULL)
            table->allocator = allocator_get_default();
    }   

    EXPORT bool hash_index_needs_rehash(Hash_Index table, isize to_size)
    {
        return to_size * 2 > table.entries_count;
    }

    EXPORT void hash_index_copy(Hash_Index* to_table, Hash_Index from_table)
    {
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));

        if(hash_index_needs_rehash(*to_table, from_table.size))
        {   
            //Rehash to must be power of two!
            int32_t rehash_to = 16;
            while(rehash_to < from_table.size)
                rehash_to *= 2;

            int32_t elem_size = sizeof(Hash_Index_Entry);
            if(to_table->allocator == NULL)
               to_table->allocator = allocator_get_default();
            to_table->entries = (Hash_Index_Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, DEF_ALIGN, SOURCE_INFO());
            to_table->entries_count = rehash_to;
        }
        
        to_table->size = from_table.size;
        _lin_probe_hash_rehash(to_table->entries, to_table->entries_count, from_table.entries, from_table.entries_count);
        
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));
    }

    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        _lin_probe_hash_rehash(to_table->entries, to_table->entries_count, NULL, 0);
        to_table->size = 0;
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

    #define HASH_INDEX_SLOW_DEBUG 1
    EXPORT bool hash_index_is_invariant(Hash_Index table)
    {
        bool ptr_size_inv = (table.entries == NULL) == (table.entries_count == 0);
        bool allocator_inv = true;
        if(table.entries != NULL)
            allocator_inv = table.allocator != NULL;

        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.entries_count >= table.size;
        bool cap_inv = is_power_of_two_or_zero(table.entries_count);

        if(HASH_INDEX_SLOW_DEBUG)
        {
            int32_t used_count = 0;
            for(int32_t i = 0; i < table.entries_count; i++)
                if(hash_index_is_entry_used(table.entries[i]))
                    used_count += 1;

            sizes_inv = sizes_inv && used_count == table.size;
            ASSERT(sizes_inv);
            ASSERT(sizes_inv);
        }

        bool final_inv = ptr_size_inv && allocator_inv && cap_inv && sizes_inv && cap_inv;
        ASSERT(final_inv);
        return final_inv;
    }
    
    EXPORT isize hash_index_find_first(Hash_Index table, uint64_t hash, isize* finished_at)
    {
        return _lin_probe_hash_find_from(table.entries, table.entries_count, hash, hash, finished_at);
    }
    
    EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found, isize* finished_at)
    {
        CHECK_BOUNDS(prev_found, table.entries_count);
        return _lin_probe_hash_find_from(table.entries, table.entries_count, hash, prev_found + 1, finished_at);
    }

    EXPORT isize hash_index_find(Hash_Index table, uint64_t hash)
    {
        isize finished_at = 0;
        return hash_index_find_first(table, hash, &finished_at);
    }

    EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted)
    {
        hash_index_reserve(table, table->size + 1);
        isize finish_at = 0;
        uint64_t mask = (uint64_t) table->entries_count - 1;
        uint64_t start_at = hash & mask;
        isize found = _lin_probe_hash_find_from(table->entries, table->entries_count, hash, start_at, &finish_at);
            
        if(found == -1)
        {
            ASSERT(finish_at < table->entries_count);
            table->entries[finish_at].hash = hash;
            table->entries[finish_at].value = value_if_inserted & ~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE);
            table->size += 1;
            found = finish_at;
        }

        return found;
    }

    EXPORT isize hash_index_rehash(Hash_Index* table, isize to_size)
    {
        ASSERT(hash_index_is_invariant(*table));
        Hash_Index rehashed = {0};
        hash_index_init(&rehashed, table->allocator);
        
        isize rehash_to = 16;
        while(rehash_to < to_size)
            rehash_to *= 2;

        //Cannot shrink beyond needed size
        if(rehash_to <= table->size)
            return 0;

        rehashed.entries = (Hash_Index_Entry*) allocator_allocate(rehashed.allocator, rehash_to * sizeof(Hash_Index_Entry), DEF_ALIGN, SOURCE_INFO());
        rehashed.size = table->size;
        rehashed.entries_count = (int32_t) rehash_to;

        isize hash_colisions = _lin_probe_hash_rehash(rehashed.entries, rehashed.entries_count, table->entries, table->entries_count);
        ASSERT(hash_index_is_invariant(rehashed));
        hash_index_deinit(table);
        *table = rehashed;
        
        ASSERT(hash_index_is_invariant(*table));
        return hash_colisions;
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(hash_index_needs_rehash(*table, to_size))
            hash_index_rehash(table, to_size*2);
    }

    EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value)
    {
        hash_index_reserve(table, table->size + 1);

        isize out = _lin_probe_hash_insert(table->entries, table->entries_count, hash, value);
        table->size += 1;
        ASSERT(hash_index_is_invariant(*table));
        return out;
    }

    EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found)
    {
        ASSERT(table->size > 0);
        CHECK_BOUNDS(found, table->entries_count);
        Hash_Index_Entry removed = table->entries[found];
        _lin_probe_hash_remove(table->entries, table->entries_count, found);
        table->size -= 1;
        ASSERT(hash_index_is_invariant(*table));
        return removed;
    }

#endif