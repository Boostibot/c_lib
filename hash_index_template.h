//THIS IS A TEMPLATE FILE DONT INCLUDE IT ON ITS OWN!

//#define TEMP_TESTING

#ifdef TEMP_TESTING
#include "defines.h"
#include "allocator.h"
#endif

//To include this file the following need to be defined:
#ifndef Hash_Index
    #error Hash_Index needs to be defined to an identifier
#endif

#ifndef hash_index
    #error hash_index needs to be defined to an identifier
#endif

#ifndef Entry
    #error Entry must be defined to be a type. It needs to have .hash field with some integral or pointer value and .value field of any type
#endif

#ifndef Hash
    #error Hash must be defined to be an itegral type
#endif

#ifndef Value
    #error Hash must be defined to type
#endif

//These only need to be defined if defining implementation
#if defined(HASH_INDEX_TEMPLATE_IMPL)
#ifndef entry_set_empty
    #error entry_set_empty needs to be defined to be a function (see below SIGNATURES section for siganture)
#endif

#ifndef entry_set_gravestone
    #error entry_set_gravestone needs to be defined to be a function (see below SIGNATURES section for siganture)
#endif

#ifndef entry_is_empty
    #error entry_is_empty needs to be defined to be a function (see below SIGNATURES section for siganture)
#endif

#ifndef entry_is_gravestone
    #error entry_is_gravestone needs to be defined to be a function (see below SIGNATURES section for siganture)
#endif

#ifndef entry_hash_escape
    #error entry_hash_escape needs to be defined to be a function (see below SIGNATURES section for siganture)
#endif

#ifndef entry_value_escape
    #error entry_value_escape needs to be defined to be a function (see below SIGNATURES section for siganture)
#endif
#endif

//@REMOVE
#ifdef TEMP_TESTING
typedef struct Entry {
    Hash hash;
    Value value;
} Entry;

typedef u64 Hash;
typedef u64 Value;

#endif

#if !defined(HASH_INDEX_NO_DEFINE_TYPE) && !defined(HASH_INDEX_TEMPLATE_IMPL)
typedef struct Hash_Index
{
    Allocator* allocator;                
    Entry* entries;                          
    int32_t size;                        
    int32_t entries_count;                   
} Hash_Index;
#endif

#define CC_(a, b, c) a##b##c
#define CC2(a, b) CC_(a, b,)
#define CC3(a, b, c) CC_(a, b, c)

#define hash_index_init             CC2(hash_index,_init)
#define hash_index_deinit           CC2(hash_index,_deinit)
#define hash_index_copy             CC2(hash_index,_copy)
#define hash_index_clear            CC2(hash_index,_clear)
#define hash_index_find             CC2(hash_index,_find)
#define hash_index_find_first       CC2(hash_index,_find_first)
#define hash_index_find_next        CC2(hash_index,_find_next)
#define hash_index_find_or_insert   CC2(hash_index,_find_or_insert)
#define hash_index_rehash           CC2(hash_index,_rehash)
#define hash_index_reserve          CC2(hash_index,_reserve)
#define hash_index_insert           CC2(hash_index,_insert)
#define hash_index_needs_rehash     CC2(hash_index,_needs_rehash)
#define hash_index_remove           CC2(hash_index,_remove)
#define hash_index_is_invariant     CC2(hash_index,_is_invariant)
#define hash_index_is_entry_used    CC2(hash_index,_is_entry_used)

EXPORT void  hash_index_init(Hash_Index* table, Allocator* allocator);
EXPORT void  hash_index_deinit(Hash_Index* table);
EXPORT void  hash_index_copy(Hash_Index* to_table, Hash_Index from_table);
EXPORT void  hash_index_clear(Hash_Index* to_table);
EXPORT isize hash_index_find(Hash_Index table, Hash hash);
EXPORT isize hash_index_find_first(Hash_Index table, Hash hash, isize* finished_at);
EXPORT isize hash_index_find_next(Hash_Index table, Hash hash, isize prev_found, isize* finished_at);
EXPORT isize hash_index_find_or_insert(Hash_Index* table, Hash hash, Value value_if_inserted);
EXPORT isize hash_index_rehash(Hash_Index* table, isize to_size); //rehashes 
EXPORT void  hash_index_reserve(Hash_Index* table, isize to_size); //reserves space such that inserting up to to_size elements will not trigger rehash
EXPORT isize hash_index_insert(Hash_Index* table, Hash hash, Value value);
EXPORT bool  hash_index_needs_rehash(Hash_Index table, isize to_size);

EXPORT Entry hash_index_remove(Hash_Index* table, isize found);
EXPORT bool  hash_index_is_invariant(Hash_Index table);
EXPORT bool  hash_index_is_entry_used(Entry entry);

#ifdef HASH_INDEX_TEMPLATE_IMPL

    // ================= SIGNATURES =================================
    INTERNAL void entry_set_empty(Entry* entry);
    INTERNAL void entry_set_gravestone(Entry* entry);
    INTERNAL bool entry_is_empty(const Entry* entry);
    INTERNAL bool entry_is_gravestone(const Entry* entry);
    INTERNAL u64 entry_hash_escape(Hash hash);
    INTERNAL Value entry_value_escape(Value hash);
    // ==============================================================
    
    #define _hash_index_find_from       CC3(_,hash_index,_find_from)
    #define _hash_index_rehash          CC3(_,hash_index,_rehash)
    #define _hash_index_insert          CC3(_,hash_index,_insert)

    INTERNAL isize _hash_index_find_from(const Entry* entries, isize entries_size, Hash hash, isize prev_index, isize* finished_at)
    {
        if(entries_size <= 0)
        {
            *finished_at = 0;
            return -1;
        }

        CHECK_BOUNDS(prev_index, entries_size);
        isize mask = entries_size - 1;
        isize i = prev_index & mask;
        isize counter = 0;
        for(; entry_is_empty(&entries[i]) == false; i = (i + 1) & mask)
        {
            if(counter >= entries_size)
                break;
            if(entries[i].hash == hash)
            {
                *finished_at = i;
                return i;
            }

            counter += 1;
        }

        *finished_at = i;
        return -1;
    }
    
    EXPORT bool hash_index_is_entry_used(Entry entry)
    {
        return entry_is_empty(&entry) == false 
            && entry_is_gravestone(&entry) == false;
    }

    INTERNAL isize _hash_index_rehash(Entry* new_entries, isize new_entries_size, const Entry* entries, isize entries_size)
    {  
        //#define HASH_INDEX_IS_EMPTY_ZERO
        #ifdef HASH_INDEX_IS_EMPTY_ZERO
        memset(new_entries, 0, (size_t) new_entries_size * sizeof *new_entries);
        #else
        for(isize i = 0; i < new_entries_size; i++)
            entry_set_empty(&new_entries[i]);
        #endif

        isize hash_colisions = 0;
        u64 mask = (u64) new_entries_size - 1;
        for(isize i = 0; i < entries_size; i++)
        {
            Entry curr = entries[i];
            if(hash_index_is_entry_used(curr) == false)
                continue;
            
            u64 k = curr.hash & mask;
            isize counter = 0;
            for(; hash_index_is_entry_used(new_entries[k]); k = (k + 1) & mask)
            {
                hash_colisions += 1;
                ASSERT(counter < new_entries_size && "must not be completely full!");
                ASSERT(counter < entries_size && "its impossible to have more then what we started with");
                counter += 1;
            }

            new_entries[k] = curr;
        }

        return hash_colisions;
    }   

    INTERNAL isize _hash_index_insert(Entry* entries, isize entries_size, Hash hash, Value value) 
    {
        ASSERT(entries_size > 0 && "there must be space for insertion");
        isize mask = entries_size - 1;
    
        isize escaped = (isize) entry_hash_escape(hash);
        isize i = escaped & mask;
        isize counter = 0;
        for(; hash_index_is_entry_used(entries[i]); i = (i + 1) & mask)
            ASSERT(counter ++ < entries_size && "must not be completely full!");

        entries[i].hash = (Hash) escaped;
        entries[i].value = entry_value_escape(value);
        return i;
    }

    EXPORT void hash_index_init(Hash_Index* table, Allocator* allocator)
    {
        Hash_Index null = {0};
        *table = null;
        table->allocator = allocator;
    }   

    EXPORT void hash_index_deinit(Hash_Index* table)
    {
        ASSERT(hash_index_is_invariant(*table));
        if(table->entries != NULL)
            allocator_deallocate(table->allocator, table->entries, table->entries_count * (isize) sizeof *table->entries, DEF_ALIGN, SOURCE_INFO());

        Hash_Index null = {0};
        *table = null;
    }

    EXPORT bool hash_index_needs_rehash(Hash_Index table, isize to_size)
    {
        return to_size * 2 >= table.entries_count;
    }

    EXPORT void hash_index_copy(Hash_Index* to_table, Hash_Index from_table)
    {
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));

        if(hash_index_needs_rehash(*to_table, from_table.size))
        {   
            int32_t rehash_to = 16;
            while(rehash_to < from_table.size)
                rehash_to *= 2;

            int32_t elem_size = sizeof to_table->entries[0];
            if(to_table->allocator == NULL)
               to_table->allocator = allocator_get_default();
            to_table->entries = (Entry*) allocator_reallocate(to_table->allocator, rehash_to * elem_size, to_table->entries, to_table->entries_count * elem_size, DEF_ALIGN, SOURCE_INFO());
            to_table->entries_count = rehash_to;
        }
        
        to_table->size = from_table.size;
        _hash_index_rehash(to_table->entries, to_table->entries_count, from_table.entries, from_table.entries_count);
        
        ASSERT(hash_index_is_invariant(*to_table));
        ASSERT(hash_index_is_invariant(from_table));
    }

    EXPORT void hash_index_clear(Hash_Index* to_table)
    {
        memset(to_table->entries, 0, (size_t) to_table->entries_count*sizeof(*to_table->entries));
        to_table->size = 0;
    }
    
    EXPORT bool hash_index_is_invariant(Hash_Index table)
    {
        bool ptr_size_inv = (table.entries == NULL) == (table.entries_count == 0);
        bool allocator_inv = true;
        if(table.entries != NULL)
            allocator_inv = table.allocator != NULL;

        bool sizes_inv = table.size >= 0 && table.entries_count >= 0 && table.entries_count >= table.size;
        bool cap_inv = is_power_of_two_or_zero(table.entries_count);
        bool final_inv = ptr_size_inv && allocator_inv && cap_inv && sizes_inv && cap_inv;

        ASSERT(final_inv);
        return final_inv;
    }
    
    
    EXPORT isize hash_index_find_first(Hash_Index table, Hash hash, isize* finished_at)
    {
        isize escaped = (isize) entry_hash_escape(hash);
        isize mask = table.entries_count - 1;
        isize start_at = escaped & mask;
        return _hash_index_find_from(table.entries, table.entries_count, (Hash) escaped, start_at, finished_at);
    }
    
    EXPORT isize hash_index_find(Hash_Index table, Hash hash)
    {
        isize finished_at = 0;
        return hash_index_find_first(table, hash, &finished_at);
    }
    
    EXPORT isize hash_index_find_next(Hash_Index table, Hash hash, isize prev_found, isize* finished_at)
    {
        isize escaped = (isize) entry_hash_escape(hash);
        return _hash_index_find_from(table.entries, table.entries_count, (Hash) escaped, prev_found + 1, finished_at);
    }

    EXPORT isize hash_index_find_or_insert(Hash_Index* table, Hash hash, Value value_if_inserted)
    {
        hash_index_reserve(table, table->size + 1);
        isize finish_at = 0;
        isize escaped = (isize) entry_hash_escape(hash);
        isize mask = table->entries_count - 1;
        isize start_at = escaped & mask;
        isize found = _hash_index_find_from(table->entries, table->entries_count, (Hash) escaped, start_at, &finish_at);
            
        if(found == -1)
        {
            ASSERT(finish_at < table->entries_count);
            table->entries[finish_at].hash = (Hash) escaped;
            table->entries[finish_at].value = entry_value_escape(value_if_inserted);
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

        isize elem_size = sizeof table->entries[0];
        if(rehashed.allocator == NULL)
           rehashed.allocator = allocator_get_default();

        rehashed.entries = (Entry*) allocator_allocate(rehashed.allocator, rehash_to * elem_size, DEF_ALIGN, SOURCE_INFO());
        rehashed.size = table->size;
        rehashed.entries_count = (int32_t) rehash_to;


        isize hash_colisions = _hash_index_rehash(rehashed.entries, rehashed.entries_count, table->entries, table->entries_count);
        hash_index_deinit(table);
        *table = rehashed;
        
        ASSERT(hash_index_is_invariant(*table));
        return hash_colisions;
    }

    EXPORT void hash_index_reserve(Hash_Index* table, isize to_size)
    {
        if(hash_index_needs_rehash(*table, to_size))
            hash_index_rehash(table, to_size);
    }

    EXPORT isize hash_index_insert(Hash_Index* table, Hash hash, Value value)
    {
        hash_index_reserve(table, table->size + 1);

        isize out = _hash_index_insert(table->entries, table->entries_count, hash, value);
        table->size += 1;
        ASSERT(hash_index_is_invariant(*table));
        return out;
    }

    EXPORT Entry hash_index_remove(Hash_Index* table, isize found)
    {
        ASSERT(table->size > 0);
        CHECK_BOUNDS(found, table->entries_count);
        
        Entry* found_entry = &table->entries[found];
        Entry removed = *found_entry;
        entry_set_gravestone(found_entry);

        table->size -= 1;
        ASSERT(hash_index_is_invariant(*table));
        return removed;
    }
#endif


//Undef user defines
#undef Hash_Index
#undef hash_index
#undef Entry
#undef Hash
#undef Value
#undef entry_set_empty
#undef entry_set_gravestone
#undef entry_is_empty
#undef entry_is_gravestone
#undef entry_hash_escape
#undef entry_value_escape
#undef HASH_INDEX_TEMPLATE_IMPL
#undef HASH_INDEX_NO_DEFINE_TYPE
#undef HASH_INDEX_IS_EMPTY_ZERO

//Undef custom defines
#undef CC_
#undef CC2
#undef CC3

#undef hash_index_init             
#undef hash_index_deinit           
#undef hash_index_copy             
#undef hash_index_clear            
#undef hash_index_find             
#undef hash_index_find_first       
#undef hash_index_find_next        
#undef hash_index_find_or_insert   
#undef hash_index_rehash           
#undef hash_index_reserve          
#undef hash_index_insert           
#undef hash_index_needs_rehash     
#undef hash_index_remove           
#undef hash_index_is_invariant     
#undef hash_index_is_entry_used    