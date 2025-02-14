#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

//COMPLETE

//intering string table which represents strings with IDs (32 bit). 
//It allows both ID -> string (through a simple array lookup) and 
// string -> ID mapping (through fast hash map lookup)

//0 - invalid id
//even - removed ids also taken to be invalid
//odd -
typedef uint32_t Intern_ID;

typedef struct Intern_String {
    const char* data;
    int64_t length;
} Intern_String;

typedef struct Interned_String_Header {
    uint32_t id;
    uint32_t hash; 
    uint32_t next_free;
    uint32_t length;
} Interned_String_Header;

typedef struct Intern_Block {
    struct Intern_Block* next;
    uint32_t strings_count;
    uint32_t strings_total_size;
    uint32_t capacity;
    uint32_t used_to;
    char data[];
    
    //comprised of data in the following format:
    // Intern_ID id
    // string of Interned_String[id].length characters
    // null termination
} Intern_Block;

typedef struct Interned_String {
    const char* string;
    uint32_t next_free;
    uint32_t length;
    uint32_t used; 
    uint32_t hash; 
} Interned_String;

typedef struct Intern{
    uint32_t* hash; 
    uint32_t hash_capacity;

    //mapping from ID to Interned_String
    Interned_String* strings;
    uint32_t strings_count; 
    uint32_t strings_capacity;
    uint32_t strings_first_free;

    uint32_t default_block_capacity_or_zero;
    Intern_Block* first_block;
    Intern_Block* curr_block;
    Intern_Block* last_block;

    bool had_romoves;
} Intern;

uint64_t intern_fnv_hash(const void* key, int64_t size, uint32_t seed)
{
    assert((key != NULL || size == 0) && size >= 0);
    const uint8_t* data = (const uint8_t*) key;
    uint64_t hash = seed ^ 0x27D4EB2F165667C5ULL;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ (uint64_t) data[i];

    return hash;
}

Intern_ID intern_put_sized_string(Intern* intern, const char* data, int64_t length)
{
    uint64_t hash = intern_fnv_hash(data, length, 0);

}

Intern_ID intern_put_custom(Intern* intern, const char* data, int64_t length, uint64_t hash, bool trust_hashes)
{
    if(length == 0)
        return 0;

    //if is more than 75% full rehash
    if(intern->strings_count*3/4 >= intern->hash_capacity)
    {
        uint64_t new_capacity = intern->hash_capacity ? intern->hash_capacity*2 : 64;
        uint64_t old_capacity = intern->hash_capacity;
        
        uint64_t* old_hash = intern->hash;
        uint64_t* new_hash = (uint64_t*) calloc(2*new_capacity*sizeof(uint64_t), 1);

        for(uint64_t k = 0; k < old_capacity; k++)
        {
            uint64_t curr_hash = old_hash[k*2];
            uint64_t curr_ptr = old_hash[k*2 + 1];
            if(curr_ptr == 0)
                continue;
            
            uint64_t i = curr_hash & (new_capacity - 1); 
            for(uint64_t step = 1;; step += 1) {
                if(new_hash[i*2] == 0)
                    break;
                    
                i = (i + step) & (new_capacity - 1);
            }

            new_hash[i*2] = curr_hash;
            new_hash[i*2 + 1] = curr_ptr;
        }

        intern->hash = new_hash;
        intern->hash_capacity = new_capacity;
        free(old_hash);
    }

    //attempt to find the string if its already interned
    uint64_t hash_i = hash & (intern->hash_capacity - 1); //power of two %
    for(uint64_t step = 1;; step += 1) {
        uint64_t curr_hash = intern->hash[hash_i*2];
        Interned_String* curr_ptr = (Interned_String*) intern->hash[hash_i*2 + 1];
        if(curr_ptr == 0)
            break;

        if(curr_hash == hash && curr_ptr->length == length)
            if(trust_hashes || memcmp(curr_ptr->data, data, length) == 0)
            {
                if(intern->had_romoves)
                    if(intern->strings[curr_ptr->id].is_present == false) {
                        intern->strings[curr_ptr->id].is_present = true;
                        intern->strings_count += 1;
                    }

                return curr_ptr->id;
            }

        //quadratic probing
        hash_i = (hash_i + step) & (intern->hash_capacity - 1);
    }

    //interned string wasnt found, insert it intern slot i....

    //If there is no block insert one
    uint64_t needed_len = sizeof(Interned_String) + length + 1;
    if(intern->last_block == NULL || intern->last_block->used_to + needed_len > intern->last_block->capacity)
    {
        assert(intern->curr_block == intern->last_block);
        uint32_t block_capacity = intern->default_block_capacity_or_zero ? intern->default_block_capacity_or_zero : 64*1024;
        if(block_capacity < sizeof(Intern_Block) + needed_len)
            block_capacity = sizeof(Intern_Block) + needed_len;

        Intern_Block* block = (Intern_Block*) malloc(block_capacity);
        memset(block, 0, sizeof(Intern_Block));
        block->capacity = block_capacity - sizeof(Intern_Block);

        if(intern->curr_block)
            intern->curr_block->next = block;

        intern->curr_block = block;
        intern->last_block = block;
    }
    
    //grab a free block
    assert(intern->first_block);
    assert(intern->last_block);
    assert(intern->curr_block);
    if(intern->curr_block->used_to + needed_len > intern->curr_block->capacity)
        intern->curr_block = intern->curr_block->next;

    //grab a free id
    Intern_ID interned_id = 0;
    for(;;) {
        //if there are no free IDs allocate some
        if(intern->strings_first_free == 0)
        {
            assert(intern->strings_count == intern->strings_capacity);
            uint32_t new_strings_capacity = intern->strings_capacity ? intern->strings_capacity*2 : 64;
            size_t combined_size = new_strings_capacity*(sizeof(_Interned_String_Ptr));

            intern->strings = (_Interned_String_Ptr*) realloc(intern->strings, combined_size);;

            //fill in all the newly added strings (except for the null entry)
            uint32_t til = intern->strings_capacity ? intern->strings_capacity : 1;
            for(uint32_t i = new_strings_capacity; i-- > til;) {
                intern->strings[i].string = NULL;
                intern->strings[i].next_free = intern->strings_first_free;
                intern->strings[i].used = false;
                intern->strings_first_free = i;
            }

            //write null entry
            intern->strings[0].string = NULL;
            intern->strings[0].next_free = 0;
            intern->strings[0].used = false;
        }    
        
        interned_id = intern->strings_first_free;
        intern->strings_first_free = intern->strings[interned_id].next_free;
        if(intern->strings[interned_id].is_present == false)
            break;
    }

    assert(intern->curr_block);
    assert(intern->interned_id);

    //push the output string
    Interned_String* interned_string = (Interned_String*) (intern->curr_block->data + intern->curr_block->used_to);
    intern->curr_block->used_to += needed_len;
    assert(intern->curr_block->used_to <= intern->curr_block->capacity);

    interned_string->length = length;
    interned_string->id = interned_id;
    memcpy(interned_string->data, data, length);
    interned_string->data[length] = '\0';
    
    //Add to hash map
    assert(intern->hash[i*2 + 1] == 0);
    intern->hash[hash_i*2] = hash;
    intern->hash[hash_i*2 + 1] = (uint64_t) interned_string;

    //Fill the string ptr
    _Interned_String_Ptr* interned_ptr = &intern->strings[interned_id];
    interned_ptr->next_free = 0;
    interned_ptr->is_present = true;
    interned_ptr->string = interned_string;
    intern->strings_count += 1;

    return interned_id;
}

void intern_compact(Intern* intern)
{
    //compact ptrs array
    uint32_t compacted_ids_count = 0;
    for(uint32_t original_i = 0; original_i < intern->strings_capacity; original_i++)
    {
        _Interned_String_Ptr interned = intern->strings[original_i];
        intern->strings[compacted_ids_count] = interned;
        compacted_ids_count += interned.is_present;
    }

    //rebuild next free list
    uint32_t til = compacted_ids_count ? compacted_ids_count : 1;
    for(uint32_t i = intern->strings_capacity; i-- > til;) {
        intern->strings[i].string = NULL;
        intern->strings[i].next_free = intern->strings_first_free;
        intern->strings[i].removed = false;
        intern->strings_first_free = i;
    }

    //write null entry
    intern->strings[0].string = NULL;
    intern->strings[0].next_free = 0;
    intern->strings[0].removed = false;

    //iterate all blocks and compact while rebuilding the hash table
    uint64_t new_capacity = 64;
    while(new_capacity <= intern->strings_count*4/3)
        new_capacity *= 2;

    uint64_t* old_hash = intern->hash;
    uint64_t* new_hash = (uint64_t*) calloc(2*new_capacity*sizeof(uint64_t), 1);

    for(Intern_Block* curr = intern->first_block;; curr = curr->next)
    {
        assert(curr);
        uint32_t compacted_i = 0;
        uint32_t original_i = 0;

        bool had_non_present = false;
        for(; original_i < curr->used_to; )
        {
            uint32_t id = 0; memcpy(&id, curr->data + original_i, 4); original_i += 4;
            uint32_t len = 0; memcpy(&len, curr->data + original_i, 4); original_i += 4;
            uint32_t is_present = intern->strings[id].is_present;
            if(had_non_present) {
                
                //
                {
                    uint64_t i = curr_hash & (new_capacity - 1); 
                    for(uint64_t step = 1;; step += 1) {
                        if(new_hash[i*2] == 0)
                            break;
                    
                        i = (i + step) & (new_capacity - 1);
                    }

                    new_hash[i*2] = curr_hash;
                    new_hash[i*2 + 1] = curr_ptr;
                }
                
                memcpy(curr->data + compacted_i, &id, 4); compacted_i += 4;
                memcpy(curr->data + compacted_i, &len, 4); compacted_i += 4;
                memcpy(curr->data + compacted_i, curr->data + original_i, len + 1); 
                compacted_i += len + 1;
                original_i += len + 1;
            }
            else
            {
                original_i += len + 1;
                compacted_i = original_i; 
            }

            had_non_present = had_non_present && is_present;

            assert(original_i <= curr->used_to);
            assert(original_i <= curr->capacity);
        }

        curr->used_to = compacted_i;
        if(curr == intern->curr_block)
            break;
    }

}

Intern_String intern_get_native_string_or(Intern* intern, Intern_ID id, Intern_String def)
{
    Intern_String out = def;
    if(id < intern->strings_capacity) {
        _Interned_String_Ptr* interned_ptr = &intern->strings[id];
        if(interned_ptr->is_present)
        {
            char* addr = (char*) interned_ptr->string;
            uint32_t len = 0;
            memcpy(&len, addr + 4, 4);

            out.data = addr + 8;
            out.length = len;
            return out;
        }
    }
    return out;
}

Intern_String intern_get_native_string(Intern* intern, Intern_ID id)
{
    Intern_String def_struct = {0};
    return intern_get_native_string_or(intern, id, def_struct);
}

const char* intern_get_cstring(Intern* intern, Intern_ID id)
{
    Intern_String def_struct = {""};
    return intern_get_native_string_or(intern, id, def_struct).data;
}

bool intern_remove(Intern* intern, Intern_ID id)
{
    if(id < intern->strings_capacity) {
        _Interned_String_Ptr* interned_ptr = &intern->strings[id];
        if(interned_ptr->is_present)
        {
            interned_ptr->is_present = false;
            interned_ptr->next_free = intern->strings_first_free;
            intern->strings_first_free = id;
            intern->strings_count -= 1;
            intern->had_romoves = true;
            return true;
        }
    }

    return false;
}