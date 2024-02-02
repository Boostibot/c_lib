

#if 1

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
    int32_t entries_removed;

    int8_t load_factor;
    int8_t load_factor_removed;
    int32_t hash_collisions;   //purely informative. 

    //Is very possible that hash_collisions overflows for big Hash_Index and 
    //in that case a function for calculating hash collisions should be called.
} Hash_Index;

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator);
EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent, isize load_factor_removed_percent);
EXPORT void  hash_index_deinit(Hash_Index* table);
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table);
EXPORT void  hash_index_clear(Hash_Index* to_table);
EXPORT isize hash_index_find(Hash_Index table, uint64_t hash);
EXPORT isize hash_index_find_first(Hash_Index table, uint64_t hash, isize* finished_at);
EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found, isize* finished_at);
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

    typedef struct Stepping {
        uint64_t i;
        isize counter;
        uint64_t step;
        uint64_t mask;
    } Stepping;
    
    INTERNAL void _lin_probe_step(Stepping* stepping, uint64_t hash, isize size, bool is_first)
    {
        if(is_first)
        {
            stepping->mask = (uint64_t) size - 1;
            stepping->i = hash & stepping->mask;
            stepping->counter = 1;

            //Linear probing
            //stepping->step = 1;

            //Double hashing
            stepping->step = (hash >> 58) | 1;

            //Quadratic probing
            //stepping->step = 0;

        }
        else
        {
            //Linear/Double hashing
            stepping->i = (stepping->i + stepping->step) & stepping->mask; 
            
            //Quadratic probing
            //stepping->i = (stepping->i + stepping->counter) & stepping->mask; 

            stepping->counter += 1;
        }

        return ;
    }

    INTERNAL isize _lin_probe_hash_find_from(const Hash_Index_Entry* entries, isize entries_size, uint64_t hash, isize prev_index, isize* finished_at)
    {
        if(entries_size <= 0)
        {
            *finished_at = 0;
            return -1;
        }

        Stepping step = {0};
        _lin_probe_step(&step, prev_index, entries_size, true);

        //while not found empty slot && didnt make a full rotation (c < entries_size) 
        for(;; _lin_probe_step(&step, prev_index, entries_size, false))
        {
            if(step.counter > entries_size || (entries[step.i].value & HASH_INDEX_EMPTY) != 0)
                break;

            if(entries[step.i].hash == hash && (entries[step.i].value & HASH_INDEX_GRAVESTONE) == 0)
            {
                *finished_at = step.i;
                return step.i;
            }
        }

        *finished_at = step.i;
        return -1;
    }

    INTERNAL isize _lin_probe_hash_insert(Hash_Index_Entry* entries, isize entries_size, uint64_t hash, uint64_t value, bool* found_hash_or_null, int32_t* collisions) 
    {
        ASSERT(entries_size > 0 && "there must be space for insertion");
        Stepping step = {0};
        _lin_probe_step(&step, hash, entries_size, true);

        for(; ;_lin_probe_step(&step, hash, entries_size, false))
        {
            if((entries[step.i].value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) != 0)
                break;

            if(found_hash_or_null != NULL && entries[step.i].hash == hash)
            {
                *found_hash_or_null = true;
                return step.i;
            }

            ASSERT(step.counter <= entries_size && "must not be completely full!");
        }

        //Clear the empty and gravestone bits so that it does not interfere with bookkeeping
        entries[step.i].value = value & ~(HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE);
        entries[step.i].hash = hash;

        if(found_hash_or_null)
            *found_hash_or_null = false;

        *collisions += (int32_t) step.counter - 1;
        return step.i;
    }
    
    INTERNAL int32_t _lin_probe_hash_rehash(Hash_Index_Entry* new_entries, isize new_entries_size, const Hash_Index_Entry* entries, isize entries_size)
    {  
        for(int32_t i = 0; i < new_entries_size; i++)
        {
            new_entries[i].hash = 0;
            new_entries[i].value = HASH_INDEX_EMPTY;
        }
        
        int32_t collisions = 0;
        for(isize i = 0; i < entries_size; i++)
        {
            Hash_Index_Entry curr = entries[i];
            if((curr.value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0)
            {
                _lin_probe_hash_insert(new_entries, new_entries_size, curr.hash, curr.value, NULL, &collisions);
            }
        }

        return collisions;
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

    EXPORT bool hash_index_needs_rehash(isize current_size, isize to_size, isize load_factor)
    {
        return to_size * 100 >= current_size * load_factor;
    }

    EXPORT void hash_index_copy(Hash_Index* to_table, Hash_Index from_table)
    {
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));

        if(hash_index_needs_rehash(to_table->entries_count, from_table.size, to_table->load_factor))
        {   
            if(to_table->allocator == NULL)
                hash_index_init(to_table, NULL);

            int32_t rehash_to = 16;
            while(hash_index_needs_rehash(rehash_to, from_table.size, to_table->load_factor))
                rehash_to *= 2;

            int32_t elem_size = sizeof(Hash_Index_Entry);
            to_table->entries = (Hash_Index_Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, DEF_ALIGN, SOURCE_INFO());
            to_table->entries_count = rehash_to;
        }
        
        to_table->size = from_table.size;
        to_table->hash_collisions = _lin_probe_hash_rehash(to_table->entries, to_table->entries_count, from_table.entries, from_table.entries_count);
        to_table->entries_removed = 0;
        
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));
    }

    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        to_table->hash_collisions = _lin_probe_hash_rehash(to_table->entries, to_table->entries_count, NULL, 0);
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

    EXPORT bool hash_index_is_invariant(Hash_Index table)
    {
        bool ptr_size_inv = (table.entries == NULL) == (table.entries_count == 0);
        bool allocator_inv = true;
        if(table.entries != NULL)
            allocator_inv = table.allocator != NULL;

        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.entries_count >= table.size;
        bool cap_inv = is_power_of_two_or_zero(table.entries_count);
        
        #ifndef NDEBUG
        #define HASH_INDEX_SLOW_DEBUG 0
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
                    sizes_inv = sizes_inv && hash_index_find(table, entry->hash) != -1;
                    ASSERT(sizes_inv);
                    used_count += 1;
                }
            }

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


    EXPORT void hash_index_rehash(Hash_Index* table, isize to_size)
    {
        ASSERT(hash_index_is_invariant(*table));
        Hash_Index rehashed = {0};
        hash_index_init_load_factor(&rehashed, table->allocator, table->load_factor, table->load_factor_removed);
        
        isize rehash_to = 16;
        while(hash_index_needs_rehash(rehash_to, to_size, rehashed.load_factor))
            rehash_to *= 2;

        //Cannot shrink beyond needed size
        if(rehash_to <= table->size)
            return;

        rehashed.entries = (Hash_Index_Entry*) allocator_allocate(rehashed.allocator, rehash_to * sizeof(Hash_Index_Entry), DEF_ALIGN, SOURCE_INFO());
        rehashed.size = table->size;
        rehashed.entries_count = (int32_t) rehash_to;

        rehashed.hash_collisions = _lin_probe_hash_rehash(rehashed.entries, rehashed.entries_count, table->entries, table->entries_count);
        hash_index_deinit(table);
        *table = rehashed;
        
        ASSERT(hash_index_is_invariant(*table));
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(hash_index_needs_rehash(table->entries_count, to_size, table->load_factor) 
            || hash_index_needs_rehash(table->entries_count, table->entries_removed, table->load_factor_removed))
        {
            //int32_t entry_count_before = table->entries_count;
            hash_index_rehash(table, to_size);
            //ASSERT(entry_count_before < table->entries_count && "must have grown!");
        }
    }
    
    INTERNAL void _hash_index_reserve_probabilistic(Hash_Index* table, isize to_size, uint64_t hash)
    {
        //hash_index_reserve(table, to_size);
        //return;

        bool rehash = hash_index_needs_rehash(table->entries_count, to_size, table->load_factor);

        //It is very unclear where to make the cutoff point for fullness of removed entries.
        // The problem is that no metter which metric we chose for calculating a hard cut off
        // point x, there is always gonna be a pathological case for when size is x - 1 and we never
        // rehash and suffer worse lookup performance. The only thing we can do is make the trnasition
        // somehow soft. We achive this by assigning each state a probability. If no removed entries
        // the probability is 0, if more then some number N removed entries, the probability is 1. We use the given hash
        // to sample this probability. When the sampled probability is higher then load_factor we rehash.
        if(rehash == false)
        {
            //H := ((hash & 0xFFFF) / 0xFFFF) //in range [0, 1]
            //R := (table.entries_removed / table.entries_count) //in range [0, 1]
            //L := load factor for removed mapped to range [0, 1]
            // 
            //P[rehash because of removed entries] := P[(H * R) > L]

            //After rearenging of terms (so that all denominators are turned to the other side) we get:
            rehash = ((hash & 0xFFFF) * table->entries_removed * 100) >= (uint64_t) table->load_factor_removed * table->entries_count * 0xFFFF; //used to be table->load_factor
        }

        if(rehash)
            hash_index_rehash(table, to_size);
    }
    
    EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted)
    {
        _hash_index_reserve_probabilistic(table, table->size + 1, hash);
        bool was_found = false;
        isize found = _lin_probe_hash_insert(table->entries, table->entries_count, hash, value_if_inserted, &was_found, &table->hash_collisions);
        if(was_found == false)
        {
            table->size += 1;
            ASSERT(hash_index_is_invariant(*table));
        }

        return found;
    }

    EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value)
    {
        _hash_index_reserve_probabilistic(table, table->size + 1, hash);

        isize out = _lin_probe_hash_insert(table->entries, table->entries_count, hash, value, NULL, &table->hash_collisions);
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
        table->entries_removed += 1;
        ASSERT(hash_index_is_invariant(*table));
        return removed;
    }

#endif

#else

#ifndef JOT_HASH2_INDEX
#define JOT_HASH2_INDEX

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

#define HASH_INDEX_HOOD_EMPTY        ((uint64_t) 1 << 63)

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
    int32_t hash_collisions;   //purely informative. 
} Hash_Index;

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator);
EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor, isize load_factor_remove);
EXPORT void  hash_index_deinit(Hash_Index* table);
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table);
EXPORT void  hash_index_clear(Hash_Index* to_table);
EXPORT isize hash_index_find(Hash_Index table, uint64_t hash);
EXPORT isize hash_index_find_first(Hash_Index table, uint64_t hash, isize* finished_at);
EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found, isize* finished_at);
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

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH2_INDEX_IMPL)) && !defined(JOT_HASH2_INDEX_HAS_IMPL)
#define JOT_HASH2_INDEX_HAS_IMPL

    INTERNAL isize _robin_hood_hash_find_from(const Hash_Index_Entry* entries, isize entries_size, uint64_t hash, isize prev_index, isize* finished_at)
    {
        if(entries_size <= 0)
        {
            *finished_at = 0;
            return -1;
        }

        uint64_t mod = (uint64_t) entries_size - 1;
        uint64_t i = prev_index & mod;
        
        uint64_t inserted_slot_dist = 0;
        //while not found empty slot && didnt make a full rotation (c < entries_size) 
        for(isize counter = 0; ; inserted_slot_dist ++, i = (i + 1) & mod)
        {
            const Hash_Index_Entry* entry = &entries[i];
            uint64_t curr_dist = (i - entry->hash) & mod;

            if(curr_dist < inserted_slot_dist || (entry->value & HASH_INDEX_HOOD_EMPTY) != 0)
                break;
                
            if(entry->hash == hash)
            {
                *finished_at = i;
                return i;
            }

            ASSERT(counter ++ < entries_size && "must not be completely full!");
        }

        *finished_at = i;
        return -1;
    }
    
    INTERNAL isize _robin_hood_hash_insert(Hash_Index_Entry* entries, isize entries_size, uint64_t hash, uint64_t value, bool* found_hash_or_null, int32_t* hash_collisions) 
    {
        ASSERT(entries_size > 0 && "there must be space for insertion");
        uint64_t mod = (uint64_t) entries_size - 1;
    
        Hash_Index_Entry inserted = {0};
        inserted.hash = hash;
        inserted.value = value & ~HASH_INDEX_HOOD_EMPTY;

        isize out_slot = -1;
        uint64_t i = hash & mod;
        uint64_t inserted_slot_dist = 0;
        isize counter = 1;
        for(; ; inserted_slot_dist ++, i = (i + 1) & mod, counter ++)
        {
            Hash_Index_Entry* entry = &entries[i];

            if(found_hash_or_null != NULL && entry->hash == hash)
            {
                *found_hash_or_null = true;
                return i;
            }

            //if found empty slot break
            if((entry->value & HASH_INDEX_HOOD_EMPTY) != 0)
                break;
            
            uint64_t curr_dist = (i - entry->hash) & mod;

            //@TODO: comment!
            if(inserted_slot_dist > curr_dist)
            {
                Hash_Index_Entry swap = *entry;
                *entry = inserted;
                inserted = swap;

                if(out_slot == -1)
                    out_slot = i;

                inserted_slot_dist = curr_dist;
            }

            ASSERT(counter <= entries_size && "must not be completely full!");
        }

        entries[i] = inserted;
        ASSERT((inserted.value & HASH_INDEX_HOOD_EMPTY) == 0);

        if(found_hash_or_null)
            *found_hash_or_null = false;

        *hash_collisions += (i32) counter - 1; 
        if(out_slot == -1)
            out_slot = i;
        return out_slot;
    }

    INTERNAL int32_t _robin_hood_hash_rehash(Hash_Index_Entry* new_entries, isize new_entries_size, const Hash_Index_Entry* entries, isize entries_size)
    {  
        //Clears all entries
        for(int32_t i = 0; i < new_entries_size; i++)
        {
            new_entries[i].hash = 0;
            new_entries[i].value = HASH_INDEX_HOOD_EMPTY;
        }
        int32_t collisions = 0;
        //Iterates all alive entries and adds them to the new hash
        for(isize i = 0; i < entries_size; i++)
        {
            Hash_Index_Entry curr = entries[i];
            if((curr.value & HASH_INDEX_HOOD_EMPTY) == 0)
                _robin_hood_hash_insert(new_entries, new_entries_size, curr.hash, curr.value, NULL, &collisions);
        }

        return collisions;
    }   
    
    INTERNAL void _robin_hood_hash_remove(Hash_Index_Entry* entries, isize entries_size, isize found) 
    {
        CHECK_BOUNDS(found, entries_size);

        //We do backshifting to not have to use gravestones
        
        entries[found].value = HASH_INDEX_HOOD_EMPTY;
        uint64_t mod = (uint64_t) entries_size - 1;
        uint64_t prev_i = (uint64_t) found;
        uint64_t i = (prev_i + 1) & mod;
        for(isize counter = 0; ; prev_i = i, i = (i + 1) & mod)
        {
            Hash_Index_Entry* entry = &entries[i];
            uint64_t curr_dist = (i - entry->hash) & mod;

            ASSERT((curr_dist == 0) == ((entry->hash & mod) == i));

            //if found empty slot or slot thats already at its desired index stop.
            //(No need to shift empty entries, cannot shift entries out of their desired positions
            if(curr_dist == 0 || (entry->value & HASH_INDEX_HOOD_EMPTY) != 0)
                break;

            //Shift entry back
            entries[prev_i] = *entry;
            entries[i].value = HASH_INDEX_HOOD_EMPTY; //@TODO: this can be done once!
            ASSERT(counter ++ < entries_size && "must not shift everything");
        }
        
        //Reset the final entry 
        //entries[found].value = HASH_INDEX_HOOD_EMPTY;
    }

    EXPORT void hash_index_deinit(Hash_Index* table)
    {
        ASSERT(hash_index_is_invariant(*table));
        allocator_deallocate(table->allocator, table->entries, table->entries_count * sizeof *table->entries, DEF_ALIGN, SOURCE_INFO());
        
        Hash_Index null = {0};
        *table = null;
    }

    EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor, isize load_factor_remove)
    {
        hash_index_deinit(table);
        table->allocator = allocator;
        if(table->allocator == NULL)
            table->allocator = allocator_get_default();

        (void) load_factor_remove;
        table->load_factor = (int8_t) (load_factor);
        if(table->load_factor <= 0 || table->load_factor >= 100)
            table->load_factor = 75;
    }

    EXPORT void hash_index_init(Hash_Index* table, Allocator* allocator)
    {
        hash_index_init_load_factor(table, allocator, 0, 0);
    }   
    
    EXPORT bool _hash_index_needs_rehash(isize current_size, isize to_size, isize load_factor_of_256)
    {
        return to_size * 100 >= current_size * load_factor_of_256;
    }

    EXPORT void hash_index_copy(Hash_Index* to_table, Hash_Index from_table)
    {
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));

        if(_hash_index_needs_rehash(to_table->entries_count, from_table.size, to_table->load_factor))
        {   
            if(to_table->allocator == NULL)
                hash_index_init(to_table, NULL);

            //Rehash to must be power of two!
            int32_t rehash_to = 16;
            while(_hash_index_needs_rehash(rehash_to, from_table.size, to_table->load_factor))
                rehash_to *= 2;

            int32_t elem_size = sizeof(Hash_Index_Entry);
            to_table->entries = (Hash_Index_Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, DEF_ALIGN, SOURCE_INFO());
            to_table->entries_count = rehash_to;
        }
        
        to_table->size = from_table.size;
        to_table->hash_collisions = _robin_hood_hash_rehash(to_table->entries, to_table->entries_count, from_table.entries, from_table.entries_count);
        
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));
    }

    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        to_table->hash_collisions = _robin_hood_hash_rehash(to_table->entries, to_table->entries_count, NULL, 0);
        to_table->size = 0;
    }
    
    EXPORT bool hash_index_is_entry_used(Hash_Index_Entry entry)
    {
        //is not empty
        return (entry.value & HASH_INDEX_HOOD_EMPTY) == 0;
    }

    EXPORT uint64_t hash_index_escape_value(uint64_t val)
    {
        return (val & ~HASH_INDEX_HOOD_EMPTY);
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
        uint64_t restored = (val & ~HASH_INDEX_HOOD_EMPTY) | (dummy & HASH_INDEX_HOOD_EMPTY);

        return (void*) restored;
    }

    EXPORT bool hash_index_is_invariant(Hash_Index table)
    {
        bool ptr_size_inv = (table.entries == NULL) == (table.entries_count == 0);
        bool allocator_inv = true;
        if(table.entries != NULL)
            allocator_inv = table.allocator != NULL;

        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.entries_count >= table.size;
        bool cap_inv = is_power_of_two_or_zero(table.entries_count);

        #ifndef NDEBUG
        #define HASH_INDEX_HOOD_SLOW_DEBUG 0
        #else
        #define HASH_INDEX_HOOD_SLOW_DEBUG 0
        #endif // !NDEBUG

        if(HASH_INDEX_HOOD_SLOW_DEBUG)
        {
            int32_t used_count = 0;
            for(int32_t i = 0; i < table.entries_count; i++)
                if(hash_index_is_entry_used(table.entries[i]))
                    used_count += 1;

            sizes_inv = sizes_inv && used_count == table.size;
            ASSERT(sizes_inv);
            ASSERT(sizes_inv);
        }

        bool final_inv = ptr_size_inv && allocator_inv && cap_inv && sizes_inv;
        ASSERT(final_inv);
        return final_inv;
    }
    
    EXPORT isize hash_index_find_first(Hash_Index table, uint64_t hash, isize* finished_at)
    {
        return _robin_hood_hash_find_from(table.entries, table.entries_count, hash, hash, finished_at);
    }
    
    EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found, isize* finished_at)
    {
        CHECK_BOUNDS(prev_found, table.entries_count);
        return _robin_hood_hash_find_from(table.entries, table.entries_count, hash, prev_found + 1, finished_at);
    }

    EXPORT isize hash_index_find(Hash_Index table, uint64_t hash)
    {
        isize finished_at = 0;
        return hash_index_find_first(table, hash, &finished_at);
    }


    EXPORT void hash_index_rehash(Hash_Index* table, isize to_size)
    {
        ASSERT(hash_index_is_invariant(*table));
        Hash_Index rehashed = {0};
        hash_index_init_load_factor(&rehashed, table->allocator, table->load_factor, table->load_factor_removed);
        
        isize rehash_to = 16;
        while(_hash_index_needs_rehash(rehash_to, to_size, rehashed.load_factor))
            rehash_to *= 2;

        //Cannot shrink beyond needed size
        if(rehash_to <= table->size)
            return;

        rehashed.entries = (Hash_Index_Entry*) allocator_allocate(rehashed.allocator, rehash_to * sizeof(Hash_Index_Entry), DEF_ALIGN, SOURCE_INFO());
        rehashed.size = table->size;
        rehashed.entries_count = (int32_t) rehash_to;

        _robin_hood_hash_rehash(rehashed.entries, rehashed.entries_count, table->entries, table->entries_count);
        hash_index_deinit(table);
        *table = rehashed;
        
        ASSERT(hash_index_is_invariant(*table));
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(_hash_index_needs_rehash(table->entries_count, to_size, table->load_factor))
        {
            int32_t entry_count_before = table->entries_count;
            hash_index_rehash(table, to_size);
            ASSERT(entry_count_before < table->entries_count && "must have grown!");
        }
    }

    EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value)
    {
        hash_index_reserve(table, table->size + 1);

        isize out = _robin_hood_hash_insert(table->entries, table->entries_count, hash, value, NULL, &table->hash_collisions);
        table->size += 1;
        ASSERT(hash_index_is_invariant(*table));
        return out;
    }
    
    EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted)
    {
        hash_index_reserve(table, table->size + 1);
        bool was_found = false;
        isize out = _robin_hood_hash_insert(table->entries, table->entries_count, hash, value_if_inserted, &was_found, &table->hash_collisions);
        if(was_found == false)
        {
            table->size += 1;
            ASSERT(hash_index_is_invariant(*table));
        }

        return out;
    }

    EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found)
    {
        ASSERT(table->size > 0);
        CHECK_BOUNDS(found, table->entries_count);
        Hash_Index_Entry removed = table->entries[found];

        _robin_hood_hash_remove(table->entries, table->entries_count, found);
        table->size -= 1;
        ASSERT(hash_index_is_invariant(*table));
        return removed;
    }

#endif
#endif