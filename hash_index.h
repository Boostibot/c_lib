#ifndef JOT_HASH_INDEX
#define JOT_HASH_INDEX

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
// row we were looking for is the one we actually got! Because of this each table will most often
// implement its own functions find_by_owner(...), find_by_name(...), find_by_age(...) etc.
// 
// =================== IMPLEMENTATION ====================
// 
// We have a simple dynamic array of tuples containing the hashed key and value.
// Hashed key is 64 bit number (can be 32 bit for better perf). Value is usually 
// an index but often we want to store pointers. These pointers need to have their 
// high bits fixed. One is supposed to use hash_index_escape_ptr when assigning to
// Hash_Index_Entry value and hash_index_restore_ptr when reading from it.
// 
// We do quadratic probing to find our entries. We use HASH_INDEX_EMPTY and HASH_INDEX_GRAVESTONE bitpatterns
// to mark empty spots. We rehash really at 75% by default but that can be changed.
// Note that the bit pattern for used is 0 so for indeces we dont need to mask out anything.
//
// I have conducted extensive testing and selected quadratic probing with default 75% load factor
// because of its good performance in all regards and lower average probe length compared to linear. 
// This makes it slightly less prone to bad hash functions. 
// It works better than double hashing for large sizes (double hashes has awful amount of cahce misses)
// and is faster than Robin Hood hashing for fifo-like usage patterns (robin hood has many branch 
// misspredicts for its complex insertion logic). Robin Hood is only better for performing lookups 
// on 'dirty' hashes (where a lot of values are gravestones) however we can solve this by rehashing 
// after heavy removals/inserts or just lower the load factor.

#define HASH_INDEX_EMPTY        ((uint64_t) 0x2 << 62)
#define HASH_INDEX_GRAVESTONE   ((uint64_t) 0x1 << 62)
#define HASH_INDEX_VALUE_MAX    (~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) //can be used as invalid value
#define HASH_INDEX_LOAD_FACTOR_BITS 7

typedef struct Hash_Index_Entry {
    uint64_t hash;
    uint64_t value;
} Hash_Index_Entry;

typedef struct Hash_Index {
    Allocator* allocator;                
    Hash_Index_Entry* entries;                          
    int32_t size;                        
    int32_t entries_count; 
    int32_t gravestone_count;

    //purely informative. 
    //has the following layout:
    // [31...........8] [7.......0]
    // hash_collisions  load_factor
    uint32_t hash_collisions_and_load_factor;   

    //Note that we can store only up to 33_554_431 hash collisions. So for super large hash indeces
    //this stat migth not ber accurate. To have 33_554_431 hash collisions we would need to have 
    // at least 0.5GB large Hash_Index.
} Hash_Index;

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator); //Initalizes table to use the given allocator and the default load factor (75%) 
EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent); //Initalizes table to use the given allocator and the provided load factor
EXPORT void  hash_index_deinit(Hash_Index* table); //Deinitializes table
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table); //Clears to_table then inserts all entrues from from tabvle. Reallocates if too small.
EXPORT void  hash_index_clear(Hash_Index* to_table); //Clears the entrire hash index without reallocating.
EXPORT isize hash_index_find(Hash_Index table, uint64_t hash); //Finds an entry in the hash index and returns its index. If no such hash is present returns -1.
EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found); //Find next entry with the same hash starting from the index of prev_found entry. This is used to iterate all entries matching the specifed hash.
EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted); //Attempts to find an entry and return its index. If fails inserts and returns index to the newly inserted
EXPORT void  hash_index_rehash(Hash_Index* table, isize to_size); //rehashes to the nearest size gretaer then the size specified and size required to store all entries.
EXPORT void  hash_index_reserve(Hash_Index* table, isize to_size); //reserves space such that its possibel to store up to to_size elements without triggering rehash.
EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value); //Inserts an entry and returns its index. The hash can be duplicate.
EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found); //Removes already found entry. If the provided index is -1 does nothing.
EXPORT bool  hash_index_is_invariant(Hash_Index table); //Checks if the hash index is in invarint state
EXPORT bool  hash_index_is_entry_used(Hash_Index_Entry entry); //Returns if the given entry is used (and thus okay to read from or write to)
EXPORT isize hash_index_get_hash_collision_count(Hash_Index table); //Returns the stored hash collision count. Is only valid up to 33_554_431 entries!
EXPORT isize hash_index_get_load_factor(Hash_Index table); //Returns the load factor specified at ini.

EXPORT uint64_t hash_index_escape_value(uint64_t val); //Escapes value so that it can be safely written to Hash_Index_Entry.value
EXPORT uint64_t hash_index_escape_ptr(const void* val); //Escapes pointer so that it can be safely written to Hash_Index_Entry.value
EXPORT void*    hash_index_restore_ptr(uint64_t val); //Restores previously escaped pointer from Hash_Index_Entry.value

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_INDEX_IMPL)) && !defined(JOT_HASH_INDEX_HAS_IMPL)
#define JOT_HASH_INDEX_HAS_IMPL

    #ifndef NDEBUG
        #define HASH_INDEX_SLOW_DEBUG 1
    #else
        #define HASH_INDEX_SLOW_DEBUG 0
    #endif

    INTERNAL isize _hash_index_find_from(Hash_Index table, uint64_t hash, uint64_t start_from)
    {
        if(table.entries_count <= 0)
            return -1;

        ASSERT(table.size + table.gravestone_count < table.entries_count && "must not be completely full!");
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
        ASSERT(table->size + table->gravestone_count < table->entries_count && "there must be space for insertion");
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
        if(table->entries[insert_index].value & HASH_INDEX_GRAVESTONE)
        {
            ASSERT(table->gravestone_count > 0);
            table->gravestone_count -= 1;
        }

        //Clear the empty and gravestone bits so that it does not interfere with bookkeeping
        table->entries[insert_index].value = value & ~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE);
        table->entries[insert_index].hash = hash;
        table->size += 1;

        //Saturating add of new_counter
        table->hash_collisions_and_load_factor += (uint32_t) (counter << HASH_INDEX_LOAD_FACTOR_BITS);
        ASSERT(hash_index_is_invariant(*table));

        return insert_index;
    }
    
    EXPORT isize hash_index_get_hash_collision_count(Hash_Index table)
    {
        uint32_t out = table.hash_collisions_and_load_factor >> HASH_INDEX_LOAD_FACTOR_BITS;
        return out;
    }
    EXPORT isize hash_index_get_load_factor(Hash_Index table)
    {
        uint32_t mask = (1 << HASH_INDEX_LOAD_FACTOR_BITS) - 1;
        uint32_t out = table.hash_collisions_and_load_factor & mask;
        return out;
    }

    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        //can also be memset to byte pattern that has HASH_INDEX_EMPTY.
        for(int32_t i = 0; i < to_table->entries_count; i++)
        {
            to_table->entries[i].hash = 0;
            to_table->entries[i].value = HASH_INDEX_EMPTY;
        }

        to_table->hash_collisions_and_load_factor = (uint32_t) hash_index_get_load_factor(*to_table);
        to_table->gravestone_count = 0;
        to_table->size = 0;
    }
    
    EXPORT bool _hash_index_needs_rehash(isize current_size, isize to_size, isize load_factor)
    {
        return to_size * 100 >= current_size * load_factor;
    }

    INTERNAL void _hash_index_rehash(Hash_Index* to_table, Hash_Index from_table, isize to_size, bool exact_size)
    {
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));

        isize load_factor = hash_index_get_load_factor(*to_table);
        if(load_factor == 0)
        {
            to_table->hash_collisions_and_load_factor |= 75;
            load_factor = hash_index_get_load_factor(*to_table);
        }

        if(_hash_index_needs_rehash(to_table->entries_count, to_size, load_factor))
        {   
            if(to_table->allocator == NULL)
                hash_index_init(to_table, NULL);

            int32_t rehash_to = 16;
            if(exact_size)
            {
                while(rehash_to < to_size || rehash_to < from_table.size)
                    rehash_to *= 2;
            }
            else
            {
                while(_hash_index_needs_rehash(rehash_to, to_size, load_factor))
                    rehash_to *= 2;
            }

            int32_t elem_size = sizeof(Hash_Index_Entry);
            if(to_table->allocator == NULL)
                to_table->allocator = allocator_get_default();
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
        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.gravestone_count >= 0; 
        bool not_full_inv = (table.size + table.gravestone_count < table.entries_count) || table.entries_count == 0;
        bool entries_find_inv = true;
        bool entries_count_inv = true;
        bool allocator_inv = true;
        bool load_factor_inv = true;
        bool capacity_inv = true;

        isize load_factor = hash_index_get_load_factor(table);
        if(table.entries != NULL)
        {
            allocator_inv = table.allocator != NULL;
            load_factor_inv = 0 < load_factor && load_factor <= 100;
            capacity_inv = is_power_of_two(table.entries_count);
        }

        int32_t used_count = table.size;
        int32_t gravestone_count = table.gravestone_count;
        if(HASH_INDEX_SLOW_DEBUG)
        {
            used_count = 0;
            gravestone_count = 0;
            for(int32_t i = 0; i < table.entries_count; i++)
            {
                Hash_Index_Entry* entry = &table.entries[i];
                if(hash_index_is_entry_used(*entry))
                {
                    entries_find_inv = entries_find_inv && hash_index_find(table, entry->hash) != -1;
                    ASSERT(entries_find_inv);
                    used_count += 1;
                }

                if(entry->value & HASH_INDEX_GRAVESTONE)
                    gravestone_count += 1;
            }
        }

        entries_count_inv = used_count == table.size && gravestone_count == table.gravestone_count;
        bool is_invariant = ptr_size_inv && allocator_inv && capacity_inv && load_factor_inv && sizes_inv && not_full_inv && entries_find_inv && entries_count_inv;
        ASSERT(is_invariant);
        return is_invariant;
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

    EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent)
    {
        hash_index_deinit(table);
        table->allocator = allocator;
        if(table->allocator == NULL)
            table->allocator = allocator_get_default();
        
        if(load_factor_percent <= 0 || load_factor_percent > 100)
            table->hash_collisions_and_load_factor = 75;
        else
            table->hash_collisions_and_load_factor = (uint32_t) load_factor_percent;
    }

    EXPORT void hash_index_init(Hash_Index* table, Allocator* allocator)
    {
        hash_index_init_load_factor(table, allocator, -1);
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
        _hash_index_rehash(to_table, from_table, from_table.size, false);
    }

    EXPORT void hash_index_rehash(Hash_Index* table, isize to_size)
    {
        Hash_Index rehashed = {0};
        hash_index_init_load_factor(&rehashed, table->allocator, hash_index_get_load_factor(*table));
        _hash_index_rehash(&rehashed, *table, to_size, true);
        hash_index_deinit(table);
        *table = rehashed;
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(_hash_index_needs_rehash(table->entries_count, to_size + table->gravestone_count, hash_index_get_load_factor(*table)))
        {
            Hash_Index rehashed = {0};
            hash_index_init_load_factor(&rehashed, table->allocator, hash_index_get_load_factor(*table));
            _hash_index_rehash(&rehashed, *table, to_size, false);
            hash_index_deinit(table);
            *table = rehashed;
        }
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
        Hash_Index_Entry removed = {0};
        if(found > 0)
        {
            ASSERT(table->size > 0);
            CHECK_BOUNDS(found, table->entries_count);
            removed = table->entries[found];
            table->entries[found].value = HASH_INDEX_GRAVESTONE;
            table->size -= 1;
            table->gravestone_count += 1;
            ASSERT(hash_index_is_invariant(*table));
        }

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
