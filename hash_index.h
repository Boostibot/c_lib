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

typedef struct Hash_Index_Entry {
    uint64_t hash;
    uint64_t value;
} Hash_Index_Entry;

typedef struct Hash_Index {
    Allocator* allocator;                
    Hash_Index_Entry* entries;                          
    int32_t size;                   //The number of key-value pairs in the hash            
    int32_t entries_count;          //The size of the underlaying Hash_Index_Entry array
    int32_t gravestone_count;
    int32_t info_rehash_count;      //Purely informative. The number of rehashes that occured so far.
    int32_t info_extra_probes;      //Purely informative. Contains the number of extra probes required to find all keys. That means `sum of number of probes to find all keys` - `number of keys`.

    int8_t  load_factor;            //defaults to 75%. Valid values [0, 100)
    int8_t  load_factor_gravestone; //defaults to 33%. Valid values [0, 100)
    bool    do_in_place_rehash;     //Does not allocated new space when rehashing because of too many gravestones. Can be set at any moment. Is useful for FIFO usage when placed in an arena.
    bool    _padding;
} Hash_Index;

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator); //Initalizes table to use the given allocator and the default load factor (75%) 
EXPORT void  hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent, isize load_factor_gravestone_percent); //Initalizes table to use the given allocator and the provided load factor
EXPORT void  hash_index_deinit(Hash_Index* table); //Deinitializes table
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table); //Clears to_table then inserts all entrues from from tabvle. Reallocates if too small.
EXPORT void  hash_index_clear(Hash_Index* to_table); //Clears the entrire hash index without reallocating.
EXPORT isize hash_index_find(Hash_Index table, uint64_t hash); //Finds an entry in the hash index and returns its index. If no such hash is present returns -1.
EXPORT isize hash_index_find_next(Hash_Index table, uint64_t hash, isize prev_found); //Find next entry with the same hash starting from the index of prev_found entry. This is used to iterate all entries matching the specifed hash.
EXPORT isize hash_index_find_or_insert(Hash_Index* table, uint64_t hash, uint64_t value_if_inserted); //Attempts to find an entry and return its index. If fails inserts and returns index to the newly inserted
EXPORT void  hash_index_rehash(Hash_Index* table, isize to_size); //rehashes to the nearest size gretaer then the size specified and size required to store all entries.
EXPORT void  hash_index_rehash_in_place(Hash_Index* table);
EXPORT void  hash_index_reserve(Hash_Index* table, isize to_size); //reserves space such that its possibel to store up to to_size elements without triggering rehash.
EXPORT isize hash_index_insert(Hash_Index* table, uint64_t hash, uint64_t value); //Inserts an entry and returns its index. The hash can be duplicate.
EXPORT Hash_Index_Entry hash_index_remove(Hash_Index* table, isize found); //Removes already found entry. If the provided index is -1 does nothing.
EXPORT bool  hash_index_is_invariant(Hash_Index table, bool slow_check); //Checks if the hash index is in invarint state
EXPORT bool  hash_index_is_entry_used(Hash_Index_Entry entry); //Returns if the given entry is used (and thus okay to read from or write to)

EXPORT uint64_t hash_index_escape_value(uint64_t val); //Escapes value so that it can be safely written to Hash_Index_Entry.value
EXPORT uint64_t hash_index_escape_ptr(const void* val); //Escapes pointer so that it can be safely written to Hash_Index_Entry.value
EXPORT void*    hash_index_restore_ptr(uint64_t val); //Restores previously escaped pointer from Hash_Index_Entry.value

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_INDEX_IMPL)) && !defined(JOT_HASH_INDEX_HAS_IMPL)
#define JOT_HASH_INDEX_HAS_IMPL
    
    #ifndef HASH_INDEX_DEBUG
        #ifdef DO_ASSERTS_SLOW
            #define HASH_INDEX_DEBUG 1
        #else
            #define HASH_INDEX_DEBUG 0
        #endif
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
                return (isize) i;
                
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
                    return (isize) i;
                
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
        
        if(insert_index == (uint64_t) -1)
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
        table->info_extra_probes += (int32_t) counter;
        ASSERT(hash_index_is_invariant(*table, HASH_INDEX_DEBUG));

        return (isize) insert_index;
    }
    

    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        //can also be memset to byte pattern that has HASH_INDEX_EMPTY.
        for(int32_t i = 0; i < to_table->entries_count; i++)
        {
            to_table->entries[i].hash = 0;
            to_table->entries[i].value = HASH_INDEX_EMPTY;
        }

        to_table->info_extra_probes = 0;
        to_table->gravestone_count = 0;
        to_table->size = 0;
    }
    
    EXPORT bool _hash_index_needs_rehash(isize current_size, isize to_size, isize load_factor)
    {
        return to_size * 100 >= current_size * load_factor;
    }
    
    INTERNAL void _hash_index_init_if_not_init(Hash_Index* table, Allocator* allocator, isize load_factor_percent, isize load_factor_gravestone_percent)
    {
        table->allocator = allocator;
        if(table->allocator == NULL)
            table->allocator = allocator_get_default();
        
        if(load_factor_percent <= 0 || load_factor_percent >= 100)
            table->load_factor = 75;
        else
            table->load_factor = (int8_t) load_factor_percent;
            
        if(load_factor_gravestone_percent <= 0 || load_factor_gravestone_percent >= 100)
            table->load_factor_gravestone = 33;
        else
            table->load_factor_gravestone = (int8_t) load_factor_percent;
    }
    EXPORT void hash_index_init_load_factor(Hash_Index* table, Allocator* allocator, isize load_factor_percent, isize load_factor_gravestone_percent)
    {
        hash_index_deinit(table);
        _hash_index_init_if_not_init(table, allocator, load_factor_percent, load_factor_gravestone_percent);
    }

    EXPORT void hash_index_init(Hash_Index* table, Allocator* allocator)
    {
        hash_index_init_load_factor(table, allocator, -1, -1);
    }   

    INTERNAL void _hash_index_rehash_copy(Hash_Index* to_table, Hash_Index from_table, isize to_size, bool size_is_capacity)
    {
        ASSERT(hash_index_is_invariant(*to_table, HASH_INDEX_DEBUG));
        ASSERT(hash_index_is_invariant(from_table, HASH_INDEX_DEBUG));

        _hash_index_init_if_not_init(to_table, to_table->allocator, to_table->load_factor, to_table->load_factor_gravestone);

        isize required = to_size > from_table.size ? to_size : from_table.size;
        isize rehash_to = required;
        if(size_is_capacity == false)
        {
            rehash_to = 16;
            while(_hash_index_needs_rehash(rehash_to, required, to_table->load_factor))
                rehash_to *= 2;
        }

        if(rehash_to > to_table->entries_count)
        {   
            isize elem_size = sizeof(Hash_Index_Entry);
            to_table->entries = (Hash_Index_Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, DEF_ALIGN);
            to_table->entries_count = (int32_t) rehash_to;
        }
        
        hash_index_clear(to_table);
        for(isize i = 0; i < from_table.entries_count; i++)
        {
            Hash_Index_Entry curr = from_table.entries[i];
            if((curr.value & (HASH_INDEX_EMPTY | HASH_INDEX_GRAVESTONE)) == 0)
                _hash_index_find_or_insert(to_table, curr.hash, curr.value, false);
        }

        to_table->info_rehash_count += 1;

        ASSERT(hash_index_is_invariant(*to_table, HASH_INDEX_DEBUG));
    }
    
    EXPORT bool hash_index_is_invariant(Hash_Index table, bool slow_check)
    {
        bool ptr_size_inv = (table.entries == NULL) == (table.entries_count == 0);
        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.gravestone_count >= 0; 
        bool not_full_inv = (table.size + table.gravestone_count < table.entries_count) || table.entries_count == 0;
        bool entries_find_inv = true;
        bool entries_count_inv = true;
        bool allocator_inv = true;
        bool load_factor_inv = true;
        bool load_factor_gravestone_inv = true;
        bool capacity_inv = true;
        bool fullness_inv = true;

        if(table.entries != NULL)
        {
            allocator_inv = table.allocator != NULL;
            load_factor_inv = 0 < table.load_factor && table.load_factor <= 100;
            load_factor_gravestone_inv = 0 < table.load_factor_gravestone && table.load_factor_gravestone <= 100;
            capacity_inv = is_power_of_two(table.entries_count);
            fullness_inv = _hash_index_needs_rehash(table.entries_count, table.size, table.load_factor) == false;
        }

        int32_t used_count = table.size;
        int32_t gravestone_count = table.gravestone_count;
        if(slow_check)
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
        bool is_invariant = ptr_size_inv && allocator_inv && capacity_inv 
            && load_factor_inv && load_factor_gravestone_inv && fullness_inv 
            && sizes_inv && not_full_inv && entries_find_inv && entries_count_inv;
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
        return _hash_index_find_from(table, hash, (uint64_t) prev_found + 1);
    }
    
    EXPORT void hash_index_deinit(Hash_Index* table)
    {
        ASSERT(hash_index_is_invariant(*table, HASH_INDEX_DEBUG));
        allocator_deallocate(table->allocator, table->entries, table->entries_count * (isize) sizeof *table->entries, DEF_ALIGN);
        
        Hash_Index null = {0};
        *table = null;
    }

    EXPORT void hash_index_copy(Hash_Index* to_table, Hash_Index from_table)
    {
        _hash_index_rehash_copy(to_table, from_table, from_table.size, false);
    }
    
    EXPORT void hash_index_rehash_in_place(Hash_Index* table)
    {
        if(table->entries_count > 0)
        {
            Arena arena = scratch_arena_acquire();
            Hash_Index copy = *table;
            copy.entries = arena_push_nonzero_inline(&arena, table->entries_count * isizeof(Hash_Index_Entry), __alignof(Hash_Index_Entry));
            memcpy(copy.entries, table->entries, (size_t) table->entries_count * sizeof(Hash_Index_Entry));

            _hash_index_rehash_copy(table, copy, table->entries_count, true);
            arena_release(&arena);
        }
    }

    EXPORT void _hash_index_rehash(Hash_Index* table, isize to_size, bool size_is_capacity)
    {
        Hash_Index rehashed = {0};
        hash_index_init_load_factor(&rehashed, table->allocator, table->load_factor, table->load_factor_gravestone);
        rehashed.do_in_place_rehash = table->do_in_place_rehash;
        _hash_index_rehash_copy(&rehashed, *table, to_size, size_is_capacity);
        hash_index_deinit(table);
        *table = rehashed;
    }
    
    EXPORT void hash_index_rehash(Hash_Index* table, isize to_size)
    {
        _hash_index_rehash(table, to_size, false);
    }

    INTERNAL ATTRIBUTE_INLINE_NEVER void hash_index_grow(Hash_Index* table, isize to_size)
    {
        _hash_index_init_if_not_init(table, table->allocator, table->load_factor, table->load_factor_gravestone);

        isize required = to_size > table->size ? to_size : table->size;
        isize rehash_to = 16;
        while(_hash_index_needs_rehash(rehash_to, required, table->load_factor))
            rehash_to *= 2;

        //If the result would be smaller OR the size would be the same but we have sufficient ammount of gravestones to clear
        // then do cleaning rehash to exactly the same capacity 
        if(rehash_to < table->entries_count || (rehash_to == table->entries_count && table->gravestone_count * 100 >= table->entries_count*table->load_factor_gravestone) )
        {
            PERF_COUNTER_START(in_place);
            if(table->do_in_place_rehash)
                hash_index_rehash_in_place(table);
            else
                _hash_index_rehash(table, table->entries_count, true);
            PERF_COUNTER_END(in_place);
        }
        else
        {
            PERF_COUNTER_START(normal);
            //If we have for example a single gravestone and we want insert another entry
            // its better to rehash to a bigger size right away 
            if(rehash_to == table->entries_count)
                rehash_to *= 2;
            
            _hash_index_rehash(table, rehash_to, true);
            PERF_COUNTER_END(normal);
        }
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(_hash_index_needs_rehash(table->entries_count, to_size + table->gravestone_count, table->load_factor))
            hash_index_grow(table, to_size);
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
        if(found >= 0)
        {
            ASSERT(table->size > 0);
            CHECK_BOUNDS(found, table->entries_count);
            removed = table->entries[found];
            table->entries[found].value = HASH_INDEX_GRAVESTONE;
            table->size -= 1;
            table->gravestone_count += 1;
            ASSERT(hash_index_is_invariant(*table, HASH_INDEX_DEBUG));
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
