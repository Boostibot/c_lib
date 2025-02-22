#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

//intering string table which represents strings with IDs (32 bit). 
//It allows both ID -> string (through a simple array lookup) and 
// string -> ID mapping (through fast hash map lookup)


//we need to check - id, gen on all lookups
//we dont need to check hash at all unless we are for some reason recomputing it
//storing stuff inline has the advantage of optionality - we can 

//we need gen for validation as well as checking alive
//-> no we dont.. just use the simple scheme for now and migrate later if we actually need generation counters.

//0 - invalid id
//even - removed ids also taken to be invalid
//odd - valid
typedef uint64_t Interned;
typedef uint32_t Interned32;

typedef int64_t isize;

typedef struct Intern_String {
    const char* data;
    int64_t length;
} Intern_String;

typedef struct Intern_Block {
    struct Intern_Block* next;
    uint32_t capacity;
    uint32_t used_to;
    char data[];
} Intern_Block;

typedef struct Interned_String {
    char* string;
    uint64_t hash; 
    uint32_t length;
    uint32_t generation;
    uint32_t next;
    uint32_t prev;
} Interned_String;

typedef struct Intern{
    uint32_t* hash; 
    uint32_t hash_capacity;

    //mapping from ID to Interned_String
    Interned_String* strings;
    uint32_t strings_count; 
    uint32_t strings_capacity;
    uint32_t strings_first_free;
    uint32_t strings_removed_count;
    uint32_t strings_removed_length;

    //storage of blocks
    uint32_t default_block_capacity_or_zero;
    Intern_Block* first_block;
    Intern_Block* curr_block;
    Intern_Block* last_block;

    //output interned id packing
    uint8_t id_bits;
    uint8_t gen_bits;
} Intern;

#define INNTERN_ALLOW_GROW          1
#define INNTERN_PACK_ID_32_GEN_32   16
#define INNTERN_PACK_ID_16_GEN_16   17
#define INNTERN_PACK_ID_24_GEN_8    18
#define INNTERN_PACK_ID_32_GEN_0    19
void intern_init_with(Intern* intern, void* strings_buffer, isize strings_size, void* hash_buffer, isize entries_buffer_size, uint32_t flags);
void intern_init(Intern* intern, isize strings_size, void* hash_buffer, isize entries_buffer_size, uint32_t flags);
void intern_deinit(Intern* intern);
void intern_set_limit(Intern* intern, isize max_data_size, isize max_strings_size, uint32_t flags);
Intern_String intern_get(const Intern* intern, Interned interned);
Intern_String intern_get_or(const Intern* intern, Interned interned, Intern_String if_not_found);
Interned intern_insert(Intern* intern, const char* string, isize length);
Interned intern_find(Intern* intern, const char* string, isize length);
void intern_remove(Intern* intern, Interned interned);
void intern_defragment(Intern* intern, Interned interned, uint32_t flags);

Intern_String intern_get_hashed_or(const Intern* intern, Interned interned, uint64_t* out_hash, Intern_String if_not_found, uint64_t if_not_found_hash);
Interned intern_find_hashed(const Intern* intern, const char* data, isize length, uint64_t hash);
Interned intern_insert_hashed(Intern* intern, const char* data, isize length, uint64_t hash);

uint64_t intern_fnv_hash(const void* key, int64_t size, uint64_t seed)
{
    assert((key != NULL || size == 0) && size >= 0);
    const uint8_t* data = (const uint8_t*) key;
    uint64_t hash = seed ^ 0x27D4EB2F165667C5ULL;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ data[i];

    return hash;
}

inline static uint64_t _intern_pack(const Intern* intern, uint32_t id, uint32_t gen)
{
    ASSERT(id < (uint64_t) 1 << intern->id_bits);
    ASSERT(gen < (uint64_t) 1 << intern->gen_bits);

    return id | gen << intern->id_bits;
}
inline static void _intern_unpack(const Intern* intern, Interned interned, uint32_t* id, uint32_t* gen)
{
    uint64_t gen_mask = ((uint64_t) 1 << intern->gen_bits) - 1;
    uint64_t id_mask = ((uint64_t) 1 << intern->id_bits) - 1;
    *gen = (uint32_t) ((interned >> intern->id_bits) & gen_mask);
    *id = (uint32_t) (interned & id_mask);
}

#define ASSERT(x, ..) assert(x)
Interned intern_find_hashed(const Intern* intern, const char* string, isize string_length, uint64_t hash)
{
    uint64_t hash_i = hash & (intern->hash_capacity - 1);
    uint32_t first_id = intern->hash[hash_i];
    for(uint32_t id = first_id; id; ) {
        ASSERT(id < intern->strings_capacity);
        Interned_String* str = intern->strings[id];
        if(str->hash == hash && str->length == string_length && strcmp(str->string, string) == 0)
            return _intern_pack(intern, id, str->gen);

        id = str->next;
    }

    return (Interned) 0;
}

Intern_String intern_get_hashed_or(const Intern* intern, Interned interned, uint64_t* out_hash, Intern_String if_not_found, uint64_t if_not_found_hash)
{
    uint32_t gen = 0;
    uint32_t id = 0; 
    _intern_unpack(intern, interned, &id, &gen);

    uint64_t hash = if_not_found_hash;
    Intern_String out = if_not_found;
    if(0 < id && id < intern->strings_capacity) {
        Interned_String* str = intern->strings[id];
        if(str->gen == gen && str->string) {
            out.data = str->string;
            out.length = str->length;
            hash = str->hash;
        }
    }
    if(out_hash)
        *out_hash = hash

    return out;
}

Interned intern_insert_hashed(Intern* intern, const char* data, isize length, uint64_t hash)
{
    if(length == 0)
        return 0;

    //attempt to find the string if its already interned
    Interned out = intern_find_hashed(intern, data, length, hash);
    if(out)
        return out;

    //If there is no block insert one
    uint64_t needed_len = sizeof(Interned_String) + length + 1;
    if(intern->last_block == NULL || intern->last_block->used_to + needed_len > intern->last_block->capacity)
    {
        ASSERT(intern->curr_block == intern->last_block);
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
    ASSERT(intern->first_block && intern->last_block && intern->curr_block);
    if(intern->curr_block->used_to + needed_len > intern->curr_block->capacity)
        intern->curr_block = intern->curr_block->next;

    //grab a free id
    uint32_t interned_id = 0;
    uint32_t interned_gen = 0;
    {
        //if there are no free IDs allocate some
        if(intern->strings_first_free == 0)
        {
            ASSERT(intern->strings_count == intern->strings_capacity);
            uint32_t new_strings_capacity = intern->strings_capacity ? intern->strings_capacity*2 : 64;
            uint32_t old_strings_capacity = intern->strings_capacity;

            intern->strings = (Interned_String*) realloc(intern->strings, new_strings_capacity*sizeof(Interned_String));
            intern->strings_capacity = old_strings_capacity;
            memset(intern->strings + old_strings_capacity, 0, (new_strings_capacity - old_strings_capacity)*sizeof(Interned_String));

            //fill in all the newly added strings (except for the null entry)
            for(uint32_t i = new_strings_capacity; i-- > old_strings_capacity && i != 0;) {
                intern->strings[i].next = intern->strings_first_free;
                intern->strings_first_free = i;
            }
        }    
        
        interned_id = intern->strings_first_free;
        intern->strings_first_free = intern->strings[interned_id].next_free;
        if(intern->strings[interned_id].is_present == false)
            break;
    }

    ASSERT(intern->curr_block);
    ASSERT(interned_id);

    //fill in the data
    uint8_t* string_data = intern->curr_block->data + intern->curr_block->used_to;
    intern->curr_block->used_to += needed_len;
    ASSERT(intern->curr_block->used_to <= intern->curr_block->capacity);

    //push the output string
    Interned_String* interned_string = &inter->strings[interned_id];
    interned_string->hash = hash;
    interned_string->length = length;
    interned_string->string = string_data;
    interned_string->gen += 1;

    uint32_t interned_gen = interned_string->gen;

    memcpy(string_data, &interned_id, sizeof interned_id); string_data += sizeof interned_id;
    memcpy(string_data, data, length); 
    string_data[length] = 0;

    //add to hash
    uint64_t hash_i = hash & (intern->hash_capacity - 1); 
    uint32_t next = intern->hash[hash_i];
    interned_string->next = next;
    if(next) {
        Interned_String* next_string = &inter->strings[next];
        next_string->prev = interned_id;
    }
    intern->hash[hash_i] = interned_id;

    intern->strings_count += 1;
    return _intern_pack(intern, interned_id, interned_gen);
}

bool intern_remove(Intern* intern, Interned interned)
{
    uint32_t gen = 0;
    uint32_t id = 0; 
    _intern_unpack(intern, interned, &id, &gen);

    bool out = false;
    if(0 < id && id < intern->strings_capacity) {
        Interned_String* str = intern->strings[id];
        if(str->gen == gen && str->string)
        {
            //unlink self
            ASSERT(str->next < intern->strings_capacity);
            ASSERT(str->prev < intern->strings_capacity);
            if(str->prev) intern->strings[str->prev].next = str->next;
            if(str->next) intern->strings[str->next].prev = str->prev;

            //if is first in hash chain, move it to the next one over
            if(str->prev == 0) {
                uint64_t hash_i = hash & (intern->hash_capacity - 1); 
                ASSERT(intern->hash[hash_i] == id);
                intern->hash[hash_i] = str->next;
            }

            //set the lnegth instead of the id and mark it to notify readers that
            // its the length and not the id.
            uint32_t marked_length = str->length | (uint32_t) 1 << 31;
            memcpy(str->string - sizeof marked_length, &marked_length, sizeof marked_length).

            //zero out everything except the gen which is increment
            uint64_t gen_mask = ((uint64_t) 1 << intern->gen_bits) - 1;
            uint64_t next_gen = (str->gen + 1) & gen_mask;
            memset(str, 0, sizeof *str);
            str->gen = next_gen;
            
            //add self to free list
            str->next = intern->strings_first_free;
            intern->strings_first_free = id;

            out = true;
        }
    }

    return out;
}

Intern_String intern_get_or(const Intern* intern, Interned interned, Intern_String if_not_found)
{
    return intern_get_hashed_or(intern, interned, NULL, if_not_found, 0);
}

Intern_String intern_get_or(const Intern* intern, Interned interned)
{
    Intern_String if_not_found = {""};
    return intern_get_hashed_or(intern, interned, NULL, if_not_found, 0);
}

void intern_deinit(Intern* intern)
{
    for(Intern_Block* block = intern->first_block; block;)
    {
        Intern_Block* next = block->next;
        free(block);
        block = next;
    }
    free(intern->hash);
    free(intern->strings);
    memset(intern, 0, sizeof *intern);
}

void intern_defragment(Intern* intern)
{
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