#ifndef JOT_HASH
#define JOT_HASH

// A simple and flexible linear-probing-hash style hash index. This file is self contained!
// 
// We use the term hash index as opposed to hash table because this structure
// doesnt provide the usual hash table interface, it merely stores indices or pointers
// to the key-value data elsewhere.
//
// =================== REASONING ====================
// 
// This approach has certain benefits most importantly enabling to simply
// write SQL style tables where every single row value can have 
// its own accelerating Hash (thats where the name comes from). 
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
// (by whatever but always the same hash algorithm) then use the owner_index 
// index to find the row. 
// 
// Only caveat being a hash collision might occur so we need to remember to always check if the 
// row we were looking for is the one we actually got! Because of this each table will most often
// implement its own functions find_by_owner(...), find_by_name(...), find_by_age(...) etc.
// 
// =================== IMPLEMENTATION ====================
// 
// We have a simple dynamic array of tuples containing the hashed key and value.
// Hashed key is 64 bit number (can be 32 bit for better perf). Value is usually 
// an index but often we want to store pointers. These pointers need to have their 
// high bits fixed. One is supposed to use hash_escape_ptr when assigning to
// Hash_Entry value and hash_restore_ptr when reading from it.
// 
// We do quadratic probing to find our entries. We use HASH_EMPTY and HASH_GRAVESTONE bitpatterns
// to mark not used slots. We rehash really at 75% by default but that can be changed.
// Note that the bit pattern for used is 0 so for indices we dont need to mask out anything.
//
// I have conducted extensive testing and selected quadratic probing with default 75% load factor
// because of its good performance in all regards and lower average probe length compared to linear. 
// This makes it slightly less prone to bad hash functions. 
// It works better than double hashing for large sizes (double hashes has awful amount of cache misses)
// and is faster than Robin Hood hashing for fifo-like usage patterns (robin hood has many branch 
// misspredicts for its complex insertion logic). Robin Hood is only better for performing lookups 
// on 'dirty' hashes (where a lot of values are gravestones) however we can solve this by rehashing 
// after heavy removals/inserts or just lower the load factor.

#if !defined(JOT_INLINE_ALLOCATOR) && !defined(JOT_ALLOCATOR) && !defined(JOT_COUPLED)
    #define JOT_INLINE_ALLOCATOR
    #include <stdint.h>
    #include <stdlib.h>
    #include <assert.h>
    #include <stdbool.h>
    
    #define EXTERNAL
    #define INTERNAL  static
    #define ASSERT(x) assert(x)

    typedef int64_t isize; //can also be usnigned if desired
    typedef struct Allocator Allocator;
    
    static Allocator* allocator_get_default() 
    { 
        return NULL; 
    }

    static void* allocator_reallocate(Allocator* from_allocator, isize new_size, void* old_ptr, isize old_size, isize align)
    {
        (void) from_allocator; (void) old_size; (void) align;
        if(new_size != 0)
            return realloc(old_ptr, (size_t) new_size);
        else
        {
            free(old_ptr);
            return NULL;
        }
    }
#else
    #include "arena_stack.h"
    #include "allocator.h"
#endif

typedef struct Hash_Entry {
    uint64_t hash;
    union {
        uint64_t value;
        uint64_t value_u64;
        uint32_t value_u32;
        int64_t  value_i64;
        int32_t  value_i32;
        double   value_f32;
        float    value_f64;
        void*    value_ptr;
    };
} Hash_Entry;

//Growing hash table like primitive mapping 64 bit keys to 64 bit values
typedef struct Hash {
    Allocator* allocator;                
    Hash_Entry* entries;                          
    int32_t len;                    //The number of key-value pairs in the hash            
    int32_t entries_count;          //The size of the underlaying Hash_Entry array
    int32_t gravestone_count;       //The number of deleted and not-yet-overwritten key-value pairs in the hash

    //The ratio of len to entries_count that needs to be achieved for a rehash to occur. 
    //Defaults to 33%. Valid values [0, 100). Can be set at any moment
    int8_t  load_factor; 
    //The ratio of gravestone_count to entries_count that needs to be achieved for a rehash to occur. 
    //Defaults to 75%. Valid values [0, 100). Can be set at any moment
    int8_t  load_factor_gravestone; 
    //Does not allocated new space when rehashing because of too many gravestones. 
    //Can be set at any moment. Is useful for FIFO usage to prevent
    // the memory used by entries from jumping all over memory 
    // (thus fragmenting or using unlimited ammount of memory when placed in an arena)
    bool    do_in_place_rehash;    
    bool    _; //@TODO: add has collisions!

    //Purely informative. The number of rehashes that occurred so far. Only resets on hash_init.
    int32_t info_rehash_count;      
    //Purely informative. The number of extra probes required to find all keys. 
    //That means info_total_extra_probes = `sum of number of probes to find all keys` - `number of keys`.
    //This quantifies the inefficiency of the table. A value of 0 means that all
    //keys are able to be on the first probe (one memory lookup).
    int32_t info_total_extra_probes;
} Hash;

//A reference to a found or not found Hash entry
typedef struct Hash_Found {
    //Index of found entry or -1 if not found
    int32_t index;  
    //how many probes it took to find (or not find). 
    //This is needed for resetting the quadratic probing search!
    int32_t probes; 
    //The looked for hash
    uint64_t hash;
    
    //Value of the found entry. If not found is zero.
    union {
        uint64_t value;
        uint64_t value_u64;
        uint32_t value_u32;
        int64_t  value_i64;
        int32_t  value_i32;
        double   value_f32;
        float    value_f64;
        void*    value_ptr;
    };
    //Pointer to the found entry. If not found is zero.
    Hash_Entry* entry;

    //signals wether the found entry was inserted in this function.
    //This is relevant for `hash_find_or_insert` functions which may or may not insert a value.
    //Find functions always have this set to false.
    //Insert functions have this set always
    bool inserted; 
    bool _[7];
} Hash_Found;

EXTERNAL void hash_init(Hash* table, Allocator* allocator); //Initalizes table to use the given allocator and the default load factor (75%) 
EXTERNAL void hash_init_load_factor(Hash* table, Allocator* allocator, isize load_factor_percent, isize load_factor_gravestone_percent); //Initalizes table to use the given allocator and the provided load factor
EXTERNAL void hash_deinit(Hash* table); //Deinitializes table
EXTERNAL void hash_copy(Hash* to_table, Hash from_table); //Clears to_table then inserts all entries from from table. Reallocates if too small.
EXTERNAL void hash_clear(Hash* to_table); //Clears the entire hash index without reallocating.
EXTERNAL Hash_Found hash_find(Hash table, uint64_t hash); //Finds an entry in the hash index and returns its index. If no such hash is present returns -1.
//Find next entry with the same hash starting from the index of prev_found entry.  
//This can be used to iterate all entries matching the specifed hash in a multihash.
EXTERNAL Hash_Found hash_find_next(Hash table, Hash_Found prev_found); 
//Attempts to find an entry and return reference to it. If fails inserts a new entry and returns reference to it.
//The inserted value must be valid according to hash_is_valid_value (asserts).
EXTERNAL Hash_Found hash_find_or_insert(Hash* table, uint64_t hash, uint64_t value_if_inserted); 
//Attempts to find next entry with the provided hash and return reference to it. If fails inserts a new entry and returns reference to it.
//Starts where previous invocation of `hash_find(_next)` or `hash_find_or_insert(_next)` left off. This is useful for
// implementing 'find or insert' or 'assign or insert' operations in hash tables built on top of this primitive.
//The inserted value must be valid according to hash_is_valid_value (asserts).
EXTERNAL Hash_Found hash_find_or_insert_next(Hash* table, Hash_Found prev_found, uint64_t value_if_inserted); 
//Inserts an entry and returns its index. This happens even if an entry with this hash exists, thus creates a multihash.
//The inserted value must be valid according to hash_is_valid_value (asserts).
EXTERNAL Hash_Found hash_insert(Hash* table, uint64_t hash, uint64_t value); 
//Inserts an entry and returns its index. This happens even if an entry with this hash exists, thus creates a multihash.
//The entry is inserted after the previously found prev_found entry.
//The inserted value must be valid according to hash_is_valid_value (asserts).
EXTERNAL Hash_Found hash_insert_next(Hash* table, Hash_Found prev_found, uint64_t value); 
//rehashes to the nearest power of two size greater then the size specified and size required to store all entries. Possibly moves the backing memory to a new location.
EXTERNAL void  hash_rehash(Hash* table, isize to_size); 
//Rehashes to the same size without changing the adress of the backing memory. This is achieved by rehashing to a new location and then copying back.
EXTERNAL void  hash_rehash_in_place(Hash* table);
//Ensures that its possible to store up to to_size elements without triggering rehash. If it is already possible does nothing
EXTERNAL void  hash_reserve(Hash* table, isize to_size); 
//Removes already found entry referenced through found_index and returns its value. 
// The provided found_index needs to reference a valid entry or be negative in which case the function does nothing and returns zero initialized entry. 
EXTERNAL Hash_Entry hash_remove_found(Hash* table, isize found_index); 
//Finds and removes an entry. Returns true if an entry with the provided hash was found and removed, false otherwise. 
//If removed_or_null is not null saves into it the value of the removed entry. If entry was not found saves into it zero initialized entry instead.
EXTERNAL bool hash_remove(Hash* table, uint64_t hash, Hash_Entry* removed_or_null);  
//Returns whether the table is in invariant state. In debug builds, for easier debugging, also asserts as soon as a problem is detected.
EXTERNAL bool hash_is_invariant(Hash table, bool slow_check); 
//Returns whether the given entry is used (and thus okay to read from or write to)
EXTERNAL bool hash_is_entry_used(Hash_Entry entry); 
//Returns whether the value can be inserted into a table (ie. is not HASH_EMPTY or HASH_GRAVESTONE).
EXTERNAL bool hash_is_valid_value(uint64_t val);

//"The most unlikely values". We need 2 special values to represent empty and gravestone values.
// We would prefer to be able to use the hash index value directly whenever possible 
//  (as opposed to store an index/ptr to separately allocated memory containing the values themselves).
// Because of this we are looking to use values that will be the least conflicting with 
// most data types. We will describe the data types we care about and how to accommodate them within
// the special value below:
// 
// - less then 64 bit types: If we set the highest bit to 1 all types with less bits then 64 extended to 64 bits using 0s will not conflict.
// - unsigned integers:      By setting the highest bit to 1 the resulting integer will be extremely high thus extremely improbable to be used.
// - signed integers:        By setting the highest but to 1 and the low bits to 0 the resulting negative value will be very small thus extremely improbable to be used.
// - 64 bit floats:          By setting the exponent to all 1s and mantissa to non zero a unique nan is formed. 
//                           This means we will not conflict with any float value except the two specific nan representations.
// - pointers:               On all architectures we care about only bottom 48 bits are used for addresses. The top bits are either ignored or need to have a specific value. 
//                           By setting only the top 16 bits we ensure that the resulting pointer is equivalent yet distinct to NULL, on architectures where the top bits are ignored.
//                           Additionally by using a strange patter of the top 16 bits the pointer is also malformed, further decreasing the chance that someone would also use this value.
// 
// Last consideration is the ease of comparison. We want the expression "val is empty or is gravestone" to be cheap to perform. By setting the two values only
// one bit apart means we can shift everything down (thus masking the lowest bit) and perform one comparison.
#ifndef HASH_EMPTY
    #define HASH_EMPTY      ((uint64_t) 0xFFF4000000000000)
    #define HASH_GRAVESTONE ((uint64_t) 0xFFF4000000000001)
#endif

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_IMPL)) && !defined(JOT_HASH_HAS_IMPL)
#define JOT_HASH_HAS_IMPL
    
    #ifndef HASH_DEBUG
        #ifdef DO_ASSERTS_SLOW
            #define HASH_DEBUG 1
        #else
            #define HASH_DEBUG 0
        #endif
    #endif

    #ifndef PROFILE_START
        #define PROFILE_START(...)
        #define PROFILE_END(...)
    #endif

    #ifndef ATTRIBUTE_INLINE_NEVER
        #define ATTRIBUTE_INLINE_NEVER
    #endif

    INTERNAL Hash_Found _hash_find(Hash table, uint64_t hash, uint64_t start_from, int32_t probes)
    {
        PROFILE_START();
        Hash_Found out = {-1, probes, hash};
        if(table.entries_count > 0)
        {
            ASSERT(table.len + table.gravestone_count < table.entries_count && "must not be completely full!");
            uint64_t mod = (uint64_t) table.entries_count - 1;
            uint64_t i = start_from & mod;
            for(;;)
            {
                if(table.entries[i].value == HASH_EMPTY)
                    break;

                if(table.entries[i].value != HASH_GRAVESTONE && table.entries[i].hash == hash)
                {
                    out.index = (int32_t) i;
                    out.entry = &table.entries[i];
                    out.value = table.entries[i].value;
                    break;
                }
                
                ASSERT(out.probes < table.entries_count && "must not be completely full!");
                out.probes += 1; 
                i = (i + (uint64_t) out.probes) & mod;
            }
        }
        PROFILE_END();
        return out;
    }
    
    INTERNAL Hash_Found _hash_find_or_insert(Hash* table, uint64_t hash, uint64_t start_from, uint64_t value, int32_t probes, bool stop_if_found) 
    {
        PROFILE_START();
        
        Hash_Found out = {-1, probes, hash};
        ASSERT(table->len + table->gravestone_count < table->entries_count && "there must be space for insertion");
        ASSERT(table->entries_count > 0);

        uint64_t mod = (uint64_t) table->entries_count - 1;
        uint64_t i = start_from & mod;
        uint64_t insert_index = (uint64_t) -1;
        for(;;)
        {
            //@NOTE: While stop_if_found we need to traverse even gravestone entries to find the 
            // true entry if there is one. If not found we would then place the entry to the next slot.
            // That would however mean we would never replace any gravestones while using stop_if_found.
            // We keep insert_index that gets set to the last gravestone. This is most of the tine also the 
            // first and thus optimal gravestone. No matter the case we replace some gravestone which keeps
            // us from rehashing too much.
            if(stop_if_found)
            {
                if(table->entries[i].hash == hash)
                {
                    out.index = (int32_t) i;
                    out.entry = &table->entries[i];
                    out.value = table->entries[i].value;
                    goto end;
                }
                
                if(table->entries[i].value == HASH_GRAVESTONE)
                    insert_index = i;

                if(table->entries[i].value == HASH_EMPTY)
                    break;
            }
            else
            {
                if(table->entries[i].value == HASH_EMPTY || table->entries[i].value == HASH_GRAVESTONE)
                    break;
            }
            
            ASSERT(out.probes < table->entries_count && "must not be completely full!");
            out.probes += 1; 
            i = (i + (uint64_t) out.probes) & mod;
        }
        
        if(insert_index == (uint64_t) -1)
            insert_index = i;

        //If writing over a gravestone reduce the gravestone counter
        table->gravestone_count -= table->entries[insert_index].value == HASH_GRAVESTONE;

        //Push the entry
        table->entries[insert_index].value = value;
        table->entries[insert_index].hash = hash;
        table->len += 1;
        table->info_total_extra_probes += out.probes;
        
        ASSERT(hash_is_invariant(*table, HASH_DEBUG));

        out.inserted = true;
        out.index = (int32_t) insert_index;
        out.value = value;
        out.entry = &table->entries[insert_index];
        
        end:
        PROFILE_END();
        return out;
    }

    
    INTERNAL bool _hash_needs_rehash(isize current_size, isize to_size, isize load_factor)
    {
        return to_size * 100 >= current_size * load_factor;
    }

    INTERNAL void _hash_init_if_not_init(Hash* table, Allocator* allocator, isize load_factor_percent, isize load_factor_gravestone_percent)
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
    
    EXTERNAL void hash_deinit(Hash* table)
    {
        PROFILE_START();
        ASSERT(hash_is_invariant(*table, HASH_DEBUG));
        if(table->allocator != NULL)
            allocator_reallocate(table->allocator, 0, table->entries, table->entries_count * (isize) sizeof *table->entries, sizeof(Hash_Entry));
        
        Hash null = {0};
        *table = null;
        PROFILE_END();
    }

    EXTERNAL void hash_init_load_factor(Hash* table, Allocator* allocator, isize load_factor_percent, isize load_factor_gravestone_percent)
    {
        hash_deinit(table);
        PROFILE_START();
        _hash_init_if_not_init(table, allocator, load_factor_percent, load_factor_gravestone_percent);
        PROFILE_END();
    }

    EXTERNAL void hash_init(Hash* table, Allocator* allocator)
    {
        hash_init_load_factor(table, allocator, (isize) -1, (isize) -1);
    }   

    INTERNAL void _hash_rehash_copy(Hash* to_table, Hash from_table, isize to_size, bool size_is_capacity)
    {   
        PROFILE_START();
        ASSERT(hash_is_invariant(*to_table, HASH_DEBUG));
        ASSERT(hash_is_invariant(from_table, HASH_DEBUG));

        _hash_init_if_not_init(to_table, to_table->allocator, to_table->load_factor, to_table->load_factor_gravestone);

        isize required = to_size > from_table.len ? to_size : from_table.len;
        isize rehash_to = required;
        if(size_is_capacity == false)
        {
            rehash_to = 16;
            while(_hash_needs_rehash(rehash_to, required, to_table->load_factor))
                rehash_to *= 2;
        }

        if(rehash_to > to_table->entries_count)
        {   
            isize elem_size = sizeof(Hash_Entry);
            to_table->entries = (Hash_Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, sizeof(Hash_Entry));
            to_table->entries_count = (int32_t) rehash_to;
        }
        
        hash_clear(to_table);
        for(isize i = 0; i < from_table.entries_count; i++)
        {
            Hash_Entry entry = from_table.entries[i];
            if(hash_is_entry_used(entry))
                _hash_find_or_insert(to_table, entry.hash, entry.hash, entry.value, 0, false);
        }

        to_table->info_rehash_count += 1;

        ASSERT(hash_is_invariant(*to_table, HASH_DEBUG));
        PROFILE_END();
    }
    
    EXTERNAL void hash_clear(Hash* to_table)
    {
        PROFILE_START();
        for(int32_t i = 0; i < to_table->entries_count; i++)
        {
            to_table->entries[i].hash = 0;
            to_table->entries[i].value = HASH_EMPTY;
        }

        to_table->info_total_extra_probes = 0;
        to_table->gravestone_count = 0;
        to_table->len = 0;
        PROFILE_END();
    }

    EXTERNAL bool hash_is_invariant(Hash table, bool slow_check)
    {
        bool is_invariant = false;
        PROFILE_START();
        {
            #define TESTI(x) if(!(x)) {ASSERT((x)); goto end;}

            TESTI((table.entries == NULL) == (table.entries_count == 0));
            TESTI((table.len >= 0 && table.entries_count >= 0 && table.gravestone_count >= 0)); 
            TESTI((table.len + table.gravestone_count < table.entries_count) || table.entries_count == 0);
            TESTI(((uint64_t) table.entries_count & ((uint64_t) table.entries_count-1)) == 0); // table.entries_count needs to be power of two or zero
            TESTI(0 <= table.load_factor && table.load_factor <= 100);
            TESTI(0 <= table.load_factor_gravestone && table.load_factor_gravestone <= 100);

            if(table.entries != NULL)
            {
                TESTI(table.allocator != NULL);
                TESTI(_hash_needs_rehash(table.entries_count, table.len, table.load_factor) == false);
            }

            if(slow_check)
            {
                int32_t used_count = 0;
                int32_t gravestone_count = 0;
                for(int32_t i = 0; i < table.entries_count; i++)
                {
                    Hash_Entry entry = table.entries[i];
                    if(hash_is_entry_used(entry))
                    {
                        TESTI(_hash_find(table, entry.hash, entry.hash, 0).index != (isize) -1);
                        used_count += 1;
                    }

                    if(entry.value == HASH_GRAVESTONE)
                        gravestone_count += 1;
                }

                TESTI(used_count == table.len);
                TESTI(gravestone_count == table.gravestone_count);
            }

            is_invariant = true;
            #undef TESTI
        }
        end:
        PROFILE_END();
        return is_invariant;
    }
    
    EXTERNAL Hash_Found hash_find(Hash table, uint64_t hash)
    {
        return _hash_find(table, hash, hash, 0);
    }
    
    EXTERNAL Hash_Found hash_find_next(Hash table, Hash_Found prev_found)
    {
        ASSERT(0 <= prev_found.index && prev_found.index < table.entries_count);
        return _hash_find(table, prev_found.hash, (uint64_t) prev_found.index + (uint64_t) prev_found.probes + 1, prev_found.probes + 1);
    }
    

    EXTERNAL void hash_copy(Hash* to_table, Hash from_table)
    {
        _hash_rehash_copy(to_table, from_table, from_table.len, false);
    }
    
    EXTERNAL void hash_rehash_in_place(Hash* table)
    {
        if(table->entries_count > 0)
        {
            #ifdef JOT_ARENA_STACK
            SCRATCH_ARENA(arena)
            {
                Hash copy = *table;
                copy.entries = (Hash_Entry*) arena_frame_push_nonzero(&arena, table->entries_count * isizeof(Hash_Entry), __alignof(Hash_Entry));
                memcpy(copy.entries, table->entries, (size_t) table->entries_count * sizeof(Hash_Entry));

                _hash_rehash_copy(table, copy, table->entries_count, true);
            }
            #endif
        }
    }

    EXTERNAL void _hash_rehash(Hash* table, isize to_size, bool size_is_capacity)
    {
        Hash rehashed = {0};
        hash_init_load_factor(&rehashed, table->allocator, table->load_factor, table->load_factor_gravestone);
        rehashed.do_in_place_rehash = table->do_in_place_rehash;
        _hash_rehash_copy(&rehashed, *table, to_size, size_is_capacity);
        hash_deinit(table);
        *table = rehashed;
    }
    
    EXTERNAL void hash_rehash(Hash* table, isize to_size)
    {
        _hash_rehash(table, to_size, false);
    }

    INTERNAL ATTRIBUTE_INLINE_NEVER void _hash_grow(Hash* table, isize to_size)
    {
        _hash_init_if_not_init(table, table->allocator, table->load_factor, table->load_factor_gravestone);

        isize required = to_size > table->len ? to_size : table->len;
        isize rehash_to = 16;
        while(_hash_needs_rehash(rehash_to, required, table->load_factor))
            rehash_to *= 2;

        //If the result would be smaller OR the size would be the same but we have sufficient amount of gravestones to clear,
        // then do cleaning rehash to exactly the same entries_count 
        if(rehash_to < table->entries_count || (rehash_to == table->entries_count && table->gravestone_count * 100 >= table->entries_count*table->load_factor_gravestone) )
        {
            PROFILE_START(in_place);
            #ifdef JOT_ARENA_STACK
            if(table->do_in_place_rehash)
                hash_rehash_in_place(table);
            else
            #endif
                _hash_rehash(table, table->entries_count, true);
            PROFILE_END(in_place);
        }
        else
        {
            PROFILE_START(normal);
            //If we have for example a single gravestone and we want insert another entry
            // its better to rehash to a bigger size right away 
            if(rehash_to == table->entries_count)
                rehash_to *= 2;
            
            _hash_rehash(table, rehash_to, true);
            PROFILE_END(normal);
        }
    }

    EXTERNAL void hash_reserve(Hash* table, isize to_size)
    {
        if(_hash_needs_rehash(table->entries_count, to_size + table->gravestone_count, table->load_factor))
            _hash_grow(table, to_size);
    }
    
    EXTERNAL Hash_Found hash_find_or_insert_next(Hash* table, Hash_Found prev_found, uint64_t value_if_inserted)
    {
        ASSERT(hash_is_valid_value(value_if_inserted));
        ASSERT(0 <= prev_found.index && prev_found.index < table->entries_count);
        hash_reserve(table, table->len + 1);
        return _hash_find_or_insert(table, prev_found.hash, (uint64_t) prev_found.index + (uint64_t) prev_found.probes + 1, value_if_inserted, prev_found.probes + 1, true);
    }

    EXTERNAL Hash_Found hash_insert_next(Hash* table, Hash_Found prev_found, uint64_t value_if_inserted)
    {
        ASSERT(hash_is_valid_value(value_if_inserted));
        ASSERT(0 <= prev_found.index && prev_found.index < table->entries_count);
        hash_reserve(table, table->len + 1);
        return _hash_find_or_insert(table, prev_found.hash, (uint64_t) prev_found.index + (uint64_t) prev_found.probes + 1, value_if_inserted, prev_found.probes + 1, false);
    }

    EXTERNAL Hash_Found hash_find_or_insert(Hash* table, uint64_t hash, uint64_t value_if_inserted)
    {
        ASSERT(hash_is_valid_value(value_if_inserted));
        hash_reserve(table, table->len + 1);
        return _hash_find_or_insert(table, hash, hash, value_if_inserted, 0, true);
    }
    
    EXTERNAL Hash_Found hash_insert(Hash* table, uint64_t hash, uint64_t value)
    {
        ASSERT(hash_is_valid_value(value));
        hash_reserve(table, table->len + 1);
        return _hash_find_or_insert(table, hash, hash, value, 0, false);
    }

    EXTERNAL Hash_Entry hash_remove_found(Hash* table, isize found)
    {
        PROFILE_START();
        Hash_Entry removed = {0};
        if(found >= 0)
        {
            ASSERT(table->len > 0);
            ASSERT(found < table->entries_count);
            removed = table->entries[found];
            table->entries[found].value = HASH_GRAVESTONE;
            table->len -= 1;
            table->gravestone_count += 1;
            ASSERT(hash_is_invariant(*table, HASH_DEBUG));
        }

        PROFILE_END();
        return removed;
    }
    
    EXTERNAL bool hash_remove(Hash* table, uint64_t hash, Hash_Entry* removed_or_null)
    {
        PROFILE_START();
        isize found = hash_find(*table, hash).index;
        Hash_Entry removed = hash_remove_found(table, found);
        if(removed_or_null)
            *removed_or_null = removed;
        PROFILE_END();
        return found != -1;
    }
    
    EXTERNAL int32_t hash_remove_all(Hash* table, uint64_t hash)
    {
        int32_t removed_count = 0;
        for(Hash_Found found = hash_find(*table, hash); found.index != -1; found = hash_find_next(*table, found))
        {
            hash_remove_found(table, found.index);
            removed_count += 1;
        }

        return removed_count;
    }
    
    EXTERNAL bool hash_is_valid_value(uint64_t val)
    {
        return val != HASH_EMPTY && val != HASH_GRAVESTONE;
    }

    EXTERNAL bool hash_is_entry_used(Hash_Entry entry)
    {
        return hash_is_valid_value(entry.value);
    }

#endif
