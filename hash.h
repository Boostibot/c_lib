#ifndef MODULE_HASH
#define MODULE_HASH

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);
typedef struct Hash_Entry Hash_Entry;

//Growing hash table like primitive mapping 64 bit keys to 64 bit values.
//Can be used to implement more fully fledged hash tables.
typedef struct Hash {
    Allocator* allocator;                
    Hash_Entry* entries;                          
    uint32_t count;                    
    uint32_t capacity;          
    uint32_t gravestone_count;
    uint32_t rehashed_times;
    uint64_t empty_value; 
    //entries which have value = empty_value are considered empty
    //entries which have value = empty_value + 1 are considered gravestone
} Hash;

typedef struct Hash_Entry {
    uint64_t hash;
    //the value can be anything as long as it fits into 64 bits.
    union {
        uint64_t value;
        uint64_t value_u64;
        uint32_t value_u32;
        int64_t  value_i64;
        int32_t  value_i32;
        double   value_f32;
        float    value_f64;
        void*    value_ptr;
        struct { 
            uint32_t value_lo32; 
            uint32_t value_hi32;
        };
    };
} Hash_Entry;
 
//Iterator of entries with the same hash 
typedef struct Hash_Iter {
    uint32_t index;  
    uint32_t iter; 
    Hash_Entry* entry;
} Hash_Iter;

#ifndef EXTERNAL
    #define EXTERNAL
#endif

EXTERNAL void  hash_init(Hash* table, Allocator* allocator, uint64_t empty_value); 
EXTERNAL void  hash_deinit(Hash* table);
EXTERNAL void  hash_clear(Hash* to_table); 
EXTERNAL bool  hash_find(const Hash*, uint64_t hash, isize* index);
EXTERNAL bool  hash_find_or_insert(Hash* table, uint64_t hash, uint64_t value, isize* index); 
EXTERNAL bool  hash_iterate(const Hash* table, uint64_t hash, Hash_Iter* it); 
EXTERNAL isize hash_insert(Hash* table, uint64_t hash, uint64_t value); 
EXTERNAL isize hash_set(Hash* table, uint64_t hash, uint64_t value); 
EXTERNAL void  hash_reserve(Hash* table, isize to_size); 
EXTERNAL void  hash_rehash_in_place(Hash* table, isize to_size, Allocator* temp);
EXTERNAL void  hash_copy_rehash(Hash* to_table, const Hash* from_table, isize to_size); 
EXTERNAL void  hash_copy_simple(Hash* to_table, const Hash* from_table); 
EXTERNAL bool  hash_remove(Hash* table, isize found_index); 
EXTERNAL isize hash_remove_with_hash(Hash* table, uint64_t hash); 
EXTERNAL isize hash_remove_with_value(Hash* table, uint64_t hash, uint64_t value); 
EXTERNAL bool  hash_find_with_value(const Hash* table, uint64_t hash, uint64_t value, isize* index);
EXTERNAL void  hash_test_consistency(const Hash* table, bool slow_check); 

EXTERNAL void  _hash_hacky_insert(Hash* table, isize index, uint64_t hash, uint64_t value); 
static inline bool hash_entry_is_used(const Hash* table, Hash_Entry* entry)
{
    return entry->value - table->empty_value > 1;
}

//Backlink interface
// 
// This is a solution to a rather niche problem. Consider an array of items and a hash accelerating searches into it.
// Lets say we want to remove an item at particular array index. We want to also remove the item from the accelerating hash, for which we would
// need to first find it. This requires computing its hash and then doing normal hash_find or similar. When hash is actually a multihash this can
// be quite expensive operation, thus it would be nice if we had some way to simply obtain the corresponding hash index without this lookup.
// This is precisely what backlinks are - simply an index from the items back into the hash.
//
// We create backlinks by manually inserting the index of the hash entry to the given item. These backlinks remain valid until the next rehash 
// upon which the Hash_Entries get essentially randomly shuffled. Because of this we give specialized routines that also restore the backlinks
// as a part of the rehashing. These assume that the Hash stores {hash, index of item} or {hash, pointer of item} entries (thus both can be read as value)
// and calculate the address uint32_t backlink as: (uint8_t*) items_base + entry.value*item_size + item_backlink_offset. 
// When we are storing pointers to indices we set item_size=1, items_base=NULL. When we are storing indices we use the pointer to the array and sizeof item.
// 
// Be careful that normal rehash might be called when hash_insert, hash_find_or_insert require to grow the hash. If you want to prevent that, then simply call
// hash_backlink_reserve(table, table.count + 1ll, ...) before the call. 

EXTERNAL void  hash_backlink_reserve(Hash* table, isize to_size, void* items_base, isize item_size, isize item_backlink_offset); 
EXTERNAL void  hash_backlink_rehash_in_place(Hash* table, isize to_size, Allocator* temp, void* items_base, isize item_size, isize item_backlink_offset);
EXTERNAL void  hash_backlink_copy_rehash(Hash* to_table, const Hash* from_table, isize to_size, void* items_base, isize item_size, isize item_backlink_offset); 

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_HASH)) && !defined(MODULE_HAS_IMPL_HASH)
#define MODULE_HAS_IMPL_HASH

    #ifndef PROFILE_START
        #define PROFILE_START(...)
        #define PROFILE_STOP(...)
    #endif

    #ifndef ATTRIBUTE_INLINE_NEVER
        #define ATTRIBUTE_INLINE_NEVER
    #endif
    
    #ifndef ASSERT
        #include <assert.h>
        #define ASSERT(x, ...) assert(x)
    #endif
    #ifndef TEST
        #include <stdio.h>
        #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
    #endif
    
    #ifndef INTERNAL
        #define INTERNAL inline static
    #endif

    INTERNAL void _hash_check_consistency(const Hash* table)
    {
        #ifndef HASH_DEBUG
            #if defined(DO_ASSERTS_SLOW)
                #define HASH_DEBUG 2
            #elif !defined(NDEBUG)
                #define HASH_DEBUG 1
            #else
                #define HASH_DEBUG 0
            #endif
        #endif

        (void) table;
        #if MAP_DEBUG > 0
            hash_test_consistency(table, HASH_DEBUG > 1);
        #endif
    }

    INTERNAL Hash_Iter _hash_it_make(const Hash* table, uint64_t hash)
    {
        Hash_Iter it = {hash & (table->capacity - 1), 1};
        return it;
    }

    INTERNAL bool _hash_find_next(const Hash* table, uint64_t hash, Hash_Iter* it)
    {
        if(table->count > 0)
        {
            uint64_t empty = table->empty_value;
            uint64_t removed = table->empty_value + 1;
            uint64_t mask = (uint64_t) table->capacity - 1;
            for(;;) {
                it->entry = &table->entries[it->index];
                if(it->entry->value == empty)
                    break;
                
                if(it->entry->hash == hash)
                    if(it->entry->value != removed)
                        return true;
                
                ASSERT(it->iter <= table->capacity && "must not be completely full!");
                it->index = (it->index + (uint64_t) it->iter) & mask;
                it->iter += 1; 
            }
        }
        it->entry = NULL;
        return false;
    }
    
    //lowlevel insert into a slot without any guarantee that its the right. (well, except consistency)
    //Sometimes this comes in handy
    EXTERNAL void  _hash_hacky_insert(Hash* table, isize index, uint64_t hash, uint64_t value)
    {
        _hash_check_consistency(table);
        uint64_t empty = table->empty_value;
        uint64_t removed = table->empty_value + 1;
        
        Hash_Entry* entry = &table->entries[index];
        ASSERT(0 <= index && index < table->capacity);
        ASSERT(value != empty && value != removed);
        ASSERT(entry->value == empty && entry->value != removed);

        table->gravestone_count -= entry->value == removed;
        table->count += 1;
        entry->value = value;
        entry->hash = hash;
        _hash_check_consistency(table);
    }

    INTERNAL bool _hash_find_or_insert(Hash* table, uint64_t hash, uint64_t value, bool insert_only, isize* index) 
    {
        hash_reserve(table, table->count + 1);

        uint64_t empty = table->empty_value;
        uint64_t removed = table->empty_value + 1;
        ASSERT(value != empty && value != removed);

        uint64_t mask = (uint64_t) table->capacity - 1;
        uint64_t i = hash & mask;
        uint64_t empty_index = (uint64_t) -1;
        for(uint64_t it = 1;; it++) {
            if(insert_only)
            {
                if(table->entries[i].value - empty <= 1)
                    break;
            }
            else
            {
                if(table->entries[i].value == empty) {
                    if(empty_index != (uint64_t) -1)
                        i = empty_index;
                    break;
                }
                
                if(table->entries[i].value == removed)
                    empty_index = i;
                else if(table->entries[i].hash == hash) {
                    *index = i;
                    return false;
                }
            }
            
            ASSERT(it <= table->capacity && "must not be completely full!");
            i = (i + it) & mask;
        }
        
        //If writing over a gravestone reduce the gravestone counter
        table->gravestone_count -= table->entries[i].value == removed;

        //Push the entry
        table->entries[i].value = value;
        table->entries[i].hash = hash;
        table->count += 1;
        *index = i;
        _hash_check_consistency(table);
        return true;
    }
    
    EXTERNAL void hash_clear(Hash* to_table)
    {
        for(uint32_t i = 0; i < to_table->capacity; i++)
        {
            to_table->entries[i].hash = 0;
            to_table->entries[i].value = to_table->empty_value;
        }

        to_table->gravestone_count = 0;
        to_table->count = 0;
        _hash_check_consistency(to_table);
    }
    
    INTERNAL void* _hash_alloc(Allocator* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
    {
        #ifndef USE_MALLOC
            ASSERT(alloc);
            return (*alloc)(alloc, 0, new_size, old_ptr, old_size, align, NULL);
        #else
            if(new_size != 0) {
                void* out = realloc(old_ptr, new_size);
                TEST(out);
                return out;
            }
            else
                free(old_ptr);
        #endif
    }

    EXTERNAL void hash_deinit(Hash* table)
    {
        if(table->allocator != NULL)
            _hash_alloc(table->allocator, 0, table->entries, table->capacity*sizeof(Hash_Entry), sizeof(Hash_Entry));
        
        memset(table, 0, sizeof *table);
    }

    EXTERNAL void hash_init(Hash* table, Allocator* allocator, uint64_t empty_value)
    {
        hash_deinit(table);
        table->allocator = allocator;
        table->empty_value = empty_value;
    }   

    INTERNAL void _hash_copy_rehash(Hash* to_table, const Hash* from_table, void* items_base, isize item_size, isize item_backlink_offset)
    {   
        hash_clear(to_table);
        uint8_t* base = (uint8_t*) items_base + item_backlink_offset;
        uint32_t mask = to_table->capacity - 1;
        for(uint32_t j = 0; j < from_table->capacity; j++)
        {
            Hash_Entry entry = from_table->entries[j];
            if(entry.value - from_table->empty_value > 1)
            {
                uint32_t i = (uint32_t) entry.hash & mask;
                for(uint32_t it = 1;; it++) {
                    if(to_table->entries[i].value == to_table->empty_value) {
                        to_table->entries[i] = entry;

                        //do backlinks if given
                        if(item_size > 0)
                            memcpy(entry.value*item_size + base, &i, sizeof i);
                        break;
                    }

                    i = (i + it) & mask;
                }
            }
        }

        to_table->count = from_table->count;
        to_table->rehashed_times += 1;
    }

    ATTRIBUTE_INLINE_NEVER
    EXTERNAL void hash_backlink_copy_rehash(Hash* to_table, const Hash* from_table, isize to_size, void* items_base, isize item_size, isize item_backlink_offset)
    {
        PROFILE_START();
        _hash_check_consistency(to_table);
        _hash_check_consistency(from_table);

        isize required = from_table->gravestone_count + from_table->count;
        if(from_table->gravestone_count > from_table->count)
            required = from_table->count;
          
        if(required < to_size)
            required = to_size;

        isize rehash_to = 16;
        while(rehash_to*3/4 < required)
            rehash_to *= 2;

        TEST(rehash_to <= UINT32_MAX);

        //we can call the rehash with to_table and from_table being the same
        // thing. We should handle those cases gracefully.
        if(to_table->entries == from_table->entries)
        {
            Hash old_copy = *from_table;
            to_table->entries = (Hash_Entry*) _hash_alloc(to_table->allocator, rehash_to*sizeof(Hash_Entry), NULL, 0, sizeof(Hash_Entry));
            to_table->capacity = (int32_t) rehash_to;
            _hash_copy_rehash(to_table, &old_copy, items_base, item_size, item_backlink_offset);
            hash_deinit(&old_copy);
        }
        else
        {
            if(rehash_to > to_table->capacity)
            {   
                to_table->entries = (Hash_Entry*) _hash_alloc(to_table->allocator, rehash_to*sizeof(Hash_Entry), to_table->entries, to_table->capacity*sizeof(Hash_Entry), sizeof(Hash_Entry));
                to_table->capacity = (int32_t) rehash_to;
            }
            _hash_copy_rehash(to_table, from_table, items_base, item_size, item_backlink_offset);
        }
        _hash_check_consistency(to_table);
        _hash_check_consistency(from_table);
        PROFILE_STOP();
    }
    
    ATTRIBUTE_INLINE_NEVER
    EXTERNAL void hash_copy_rehash(Hash* to_table, const Hash* from_table, isize to_size)
    {
        hash_backlink_copy_rehash(to_table, from_table, to_size, 0, 0, 0);
    }
    
    EXTERNAL void hash_copy_simple(Hash* to_table, const Hash* from_table)
    {
        PROFILE_START();
        _hash_check_consistency(to_table);
        _hash_check_consistency(from_table);
        if(to_table->entries == from_table->entries)
            return;

        if(to_table->capacity != from_table->capacity) {
            to_table->entries = (Hash_Entry*) _hash_alloc(to_table->allocator, from_table->capacity*sizeof(Hash_Entry), to_table->entries, to_table->capacity*sizeof(Hash_Entry), sizeof(Hash_Entry));
            to_table->capacity = (int32_t) from_table->capacity;
        }
        memcpy(to_table->entries, from_table->entries, from_table->capacity*sizeof(Hash_Entry));
        to_table->gravestone_count = from_table->gravestone_count;
        to_table->empty_value = from_table->empty_value;
        _hash_check_consistency(to_table);
        _hash_check_consistency(from_table);
        PROFILE_STOP();
    }
    
    EXTERNAL void hash_backlink_rehash_in_place(Hash* table, isize to_size, Allocator* temp_alloc, void* items_base, isize item_size, isize item_backlink_offset)
    {
        Hash temp = {0};
        hash_init(&temp, temp_alloc, table->empty_value);
        hash_copy_simple(&temp, table);
        hash_backlink_copy_rehash(table, &temp, to_size, items_base, item_size, item_backlink_offset);
        hash_deinit(&temp);
    }

    EXTERNAL void hash_rehash_in_place(Hash* table, isize to_size, Allocator* temp_alloc)
    {
        hash_backlink_rehash_in_place(table, to_size, temp_alloc, 0, 0, 0);
    }

    EXTERNAL void hash_reserve(Hash* table, isize to_size)
    {
        _hash_check_consistency(table);
        if(table->capacity*3/4 <= to_size + table->gravestone_count)
            hash_copy_rehash(table, table, to_size);
    }
    
    EXTERNAL void hash_backlink_reserve(Hash* table, isize to_size, void* items_base, isize item_size, isize item_backlink_offset)
    {
        _hash_check_consistency(table);
        if(table->capacity*3/4 <= to_size + table->gravestone_count)
            hash_backlink_copy_rehash(table, table, to_size, items_base, item_size, item_backlink_offset);
    }

    EXTERNAL bool hash_find(const Hash* table, uint64_t hash, isize* index)
    {
        _hash_check_consistency(table);
        Hash_Iter it = _hash_it_make(table, hash);
        bool out = _hash_find_next(table, hash, &it);
        if(index)
            *index = it.index;
        return out;
    }

    EXTERNAL bool hash_iterate(const Hash* table, uint64_t hash, Hash_Iter* it)
    {
        _hash_check_consistency(table);
        if(it->iter == 0)
            *it = _hash_it_make(table, hash);
        else {
            it->index = (it->index + (uint64_t) it->iter) & (table->capacity - 1);
            it->iter += 1; 
        }
        return _hash_find_next(table, hash, it);
    }
    
        
    EXTERNAL isize hash_remove_with_hash(Hash* table, uint64_t hash)
    {
        isize count = 0;
        for(Hash_Iter it = _hash_it_make(table, hash); _hash_find_next(table, hash, &it); count++)
            hash_remove(table, it.index);
        return count;
    }
    EXTERNAL isize hash_remove_with_value(Hash* table, uint64_t hash, uint64_t value)
    {
        isize count = 0;
        for(Hash_Iter it = _hash_it_make(table, hash); _hash_find_next(table, hash, &it); )
            if(it.entry->value == value)
                count += hash_remove(table, it.index);
        return count;
    }
    EXTERNAL bool hash_find_with_value(const Hash* table, uint64_t hash, uint64_t value, isize* index)
    {
        for(Hash_Iter it = _hash_it_make(table, hash); _hash_find_next(table, hash, &it); )
            if(it.entry->value == value)
            {
                if(index) *index = it.index;
                return true;
            }
        return false;
    }

    EXTERNAL bool hash_find_or_insert(Hash* table, uint64_t hash, uint64_t value, isize* index)
    {
        return _hash_find_or_insert(table, hash, value, false, index);
    }

    EXTERNAL isize hash_insert(Hash* table, uint64_t hash, uint64_t value)
    {
        isize index = 0;
        _hash_find_or_insert(table, hash, value, true, &index);
        return index;
    }

    EXTERNAL isize hash_set(Hash* table, uint64_t hash, uint64_t value)
    {
        isize index = 0;
        if(_hash_find_or_insert(table, hash, value, false, &index) == false)
            table->entries[index].value = value;
        return index;
    }

    EXTERNAL bool hash_remove(Hash* table, isize found)
    {
        if((uint64_t) found < table->capacity)
        {
            ASSERT(table->count > 0);
            table->entries[found].value = table->empty_value + 1;
            table->count -= 1;
            table->gravestone_count += 1;
            return true;
        }
        return false;
    }
    
    EXTERNAL void hash_test_consistency(const Hash* table, bool slow_check)
    {
        PROFILE_START();
        TEST((table->entries == NULL) == (table->capacity == 0));
        TEST((table->count >= 0 && table->capacity >= 0 && table->gravestone_count >= 0)); 
        TEST(((uint64_t) table->capacity & ((uint64_t) table->capacity-1)) == 0); // capacity needs to be power of two or zero
        TEST(table->capacity*3/4 >= table->count + table->gravestone_count);

        if(table->entries != NULL)
            TEST(table->allocator != NULL);

        if(slow_check)
        {
            uint32_t used_count = 0;
            uint32_t gravestone_count = 0;
            for(uint32_t i = 0; i < table->capacity; i++)
            {
                Hash_Entry entry = table->entries[i];
                if(hash_entry_is_used(table, &entry)) {
                    Hash_Iter it = _hash_it_make(table, entry.hash);
                    TEST(_hash_find_next(table, entry.hash, &it));
                    used_count += 1;
                }
                else if(entry.value == table->empty_value + 1)
                    gravestone_count += 1;
            }

            TEST(used_count == table->count);
            TEST(gravestone_count == table->gravestone_count);
        }
        PROFILE_STOP();
    }
#endif
