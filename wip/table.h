
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef MODULE_ALL_COUPLED
    #include "assert.h"
    #include "profile.h"
    #include "allocator.h"
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

#define ASSERT(x)
#define INLINE_ALWAYS inline
#define EXTERNAL

#define CHECK_BOUNDS(i, count) ASSERT(i <= count)
#define ASSERT_BOUNDS(x) ASSERT(x)
#define TEST(x) ASSERT(x)

typedef union Table_ID {
    struct {
        uint32_t index;
        uint32_t gen;
    };
    uint64_t val;
} Table_ID;

typedef struct Table_Slot {
    union {
        Table_ID id;
        uint32_t next_free;
        struct {
            uint32_t index;
            uint32_t gen;
        };
    };

    uint8_t data[];
} Table_Slot;

#define TABLE_BLOCK_SIZE 64
typedef struct Table {
    Allocator* allocator;
    uint8_t** blocks;

    uint32_t count;
    uint32_t capacity;

    uint32_t blocks_count;
    uint32_t blocks_capacity;

    uint32_t first_free;

    uint32_t slot_size;
    uint32_t allocation_granularity;
} Table;

EXTERNAL void table_init(Table* table, isize item_size, isize allocation_granularity_or_zero);
EXTERNAL void table_deinit(Table* Table);
EXTERNAL void table_reserve(Table* table, isize size);

EXTERNAL Table_ID table_id_from_ptr(void* ptr);
EXTERNAL Table_ID table_insert_nozero(Table* table, void** ptr_or_null); //inserts new empty entry 
EXTERNAL Table_ID table_insert(Table* table, void** ptr_or_null); //inserts new empty entry and memsets it to zero
EXTERNAL void* table_mark_changed(Table* table, Table_ID id); //finds the id and increments its gen counter, effectively simulating remove + insert a new copy
EXTERNAL bool table_remove(Table* table, Table_ID id); //removes the specified entry. Returns if the entry was found (and thus removed).

EXTERNAL void* table_get(Table* table, Table_ID id);
EXTERNAL void* table_get_or(Table* table, Table_ID id, void* if_not_found);
EXTERNAL void* table_at(Table* table, isize index);
EXTERNAL void* table_at_or(Table* table, isize index, Table_Slot* if_not_found);

static inline Table_Slot* table_slot_at(Table* table, isize index, isize item_size);
static inline Table_ID table_unpack(uint64_t packed, isize index_bits, isize gen_bits);
static inline uint64_t table_pack(Table_ID id, isize index_bits, isize gen_bits);

typedef struct Table_Iterator {
    Table* table;
    uint8_t* block;
    uint32_t block_i;
    uint32_t slot_i;
    Table_ID id;
    bool did_break;
} Table_Iterator;

inline static Table_Iterator _table_it_precond(Table* table, isize from_id);
inline static bool  _table_it_cond(Table_Iterator* it);
inline static void  _table_it_postcond(Table_Iterator* it);
inline static void* _table_it_per_slot(Table_Iterator* it, isize slot_size);

#define TABLE_FOR_CUSTOM(table_ptr, it, T, item, slot_size, from_id) \
    for(Table_Iterator it = _table_it_precond(table_ptr, from_id); _table_it_cond(&it); _table_it_postcond(&it)) \
        for(T* item = NULL; it.slot_i < TABLE_BLOCK_SIZE; it.slot_i++) \
            if(item = (T*) _table_it_per_slot(&it, slot_size)) \

#define TABLE_FOR_GENERIC(table_ptr, it, item) TABLE_FOR_CUSTOM((table_ptr), it, void, item, it.table->slot_size, 0)
#define TABLE_FOR(table_ptr, it, T, item) TABLE_FOR_CUSTOM((table_ptr), it, T, item, sizeof(T), 0)

inline static Table_Iterator _table_it_precond(Table* table, isize from_id)
{
    Table_Iterator it = {0};
    it.table = table;
    it.slot_i = from_id % TABLE_BLOCK_SIZE;
    it.block_i = from_id / TABLE_BLOCK_SIZE;
    return it;
}

inline static bool _table_it_cond(Table_Iterator* it)
{
    if(it->did_break == false && it->block_i < it->table->blocks_count) {
        it->block = it->table->blocks[it->block_i];
        return true;
    }
    return false;
}

inline static void _table_it_postcond(Table_Iterator* it)
{
    it->did_break = (it->slot_i != TABLE_BLOCK_SIZE);
    it->slot_i = 0;
    it->block_i++;
}

inline static void* _table_it_per_slot(Table_Iterator* it, isize slot_size)
{
    ASSERT(slot_size == it->table->slot_size);
    uint8_t* slot_u8 = it->block + slot_size*it->slot_i;
    Table_Slot* slot = (Table_Slot*) (void*) slot_u8;
    if(slot->gen % 2 == 1)
    {
        it->id.index = it->slot_i + it->block_i*TABLE_BLOCK_SIZE;
        it->id.gen = slot->gen;
        return slot->data;
    }

    return NULL;
}

static inline Table_Slot* table_slot_at(Table* table, isize index, isize slot_size)
{
    uint64_t uindex = (uint64_t) index;
    ASSERT(slot_size == it->table->slot_size);
    ASSERT(uindex < table->capacity);

    uint8_t* block = table->blocks[uindex/TABLE_BLOCK_SIZE];
    Table_Slot* slot = (Table_Slot*) (block + slot_size*(uindex%TABLE_BLOCK_SIZE));
    ASSERT(slot->id.gen % 2 == 1);
    return slot;
}

#ifndef INTERNAL
    #define INTERNAL static inline 
#endif

INTERNAL void* _table_alloc(Allocator* alloc, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
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

INTERNAL void _table_check_invariants(Table* table)
{

}
EXTERNAL void table_init(Table* table, isize item_size, isize allocation_granularity_or_zero)
{
    table_deinit(table);
    table->slot_size = (uint32_t) item_size + sizeof(Table_Slot);
    table->allocation_granularity = (uint32_t) allocation_granularity_or_zero;
}
EXTERNAL void table_deinit(Table* table)
{
    _table_check_invariants(table);
    uint64_t* was_alloced = (uint64_t*) (void*) (table->blocks + table->blocks_capacity);
    for(uint32_t i = 0; i < table->blocks_count; )
    {
        uint32_t k = i;
        for(i += 1; i < table->blocks_count; i++) 
            if(was_alloced[i/64] & (1ull << (i%64)))
                break;

        _table_alloc(table->allocator, 0, table->blocks[k], (i - k)*TABLE_BLOCK_SIZE*table->slot_size, 64);
    }

    if(table->blocks_capacity) {
        isize old_alloced = table->blocks_capacity*sizeof(Table_Slot*) + table->blocks_capacity/64*sizeof(uint64_t);
        _table_alloc(table->allocator, 0, table->blocks, old_alloced, 8);
    }
    memset(table, 0, sizeof *table);
}
EXTERNAL void table_reserve(Table* table, isize to_count)
{
    if(to_count > table->capacity)
    {
        _table_check_invariants(table);
        
        isize desired_items = table->allocation_granularity/table->slot_size;
        if(desired_items < to_count)
            desired_items = to_count;
        isize added_blocks = (desired_items + TABLE_BLOCK_SIZE - 1)/TABLE_BLOCK_SIZE;

        //If the ptr array needs reallocating
        isize blocks_count = table->capacity/TABLE_BLOCK_SIZE;
        if(blocks_count + added_blocks > table->blocks_capacity)
        {
            isize old_capacity = table->blocks_capacity;
            isize new_capacity = 16;
            while(new_capacity < blocks_count + added_blocks)
                new_capacity *= 2;

            isize old_alloced = old_capacity*sizeof(Table_Slot*) + old_capacity/64*sizeof(uint64_t);
            isize new_alloced = new_capacity*sizeof(Table_Slot*) + new_capacity/64*sizeof(uint64_t);
            uint8_t** blocks = (uint8_t**) _table_alloc(table->allocator, new_alloced, table->blocks, old_alloced, 8);

            //copy over the was_allocated info
            uint64_t* old_was_alloced = (uint64_t*) (void*) (blocks + old_capacity);
            uint64_t* new_was_alloced = (uint64_t*) (void*) (blocks + new_capacity);
            for(isize i = 0; i < (new_capacity + 63)/64; i++)
                new_was_alloced[i] = i < (old_capacity + 63)/64 ? old_was_alloced[i] : 0;

            //clear the rest
            memset(blocks + old_capacity, 0, (size_t) (new_capacity - old_capacity)*sizeof(Table_Slot*));
            table->blocks = blocks;
            table->blocks_capacity = (uint32_t) new_capacity;
        }

        ASSERT(table->blocks_count < table->blocks_capacity);

        //allocate the actual storage blocks 
        isize alloced_blocks_bytes = added_blocks*table->slot_size*TABLE_BLOCK_SIZE;
        uint8_t* alloced_blocks = (uint8_t*) _table_alloc(table->allocator, alloced_blocks_bytes, NULL, 0, 64);
        memset(alloced_blocks, 0, (size_t) alloced_blocks_bytes);

        //Add the blocks into our array (backwards so that the next added item has lowest index)
        for(uint32_t i = (uint32_t) added_blocks; i-- > 0;)
        {
            table->blocks[i + table->blocks_count] = alloced_blocks + i*table->slot_size*TABLE_BLOCK_SIZE;
            table->first_free = i + table->blocks_count + 1;
        }
        
        //mark the first block as alloced
        uint64_t* was_alloced = (uint64_t*) (void*) (table->blocks + table->blocks_capacity);
        was_alloced[table->blocks_count/64] |= 1ull << (table->blocks_count%64);

        table->blocks_count += (uint32_t) added_blocks;
        _table_check_invariants(table);
    }

    ASSERT(table->first_free != 0, "needs to have a place thats not filled when we reserved one!");
}

EXTERNAL Table_ID table_id_from_ptr(void* ptr)
{
    return ((Table_Slot*) ptr - 1)->id;
}

INTERNAL Table_ID _table_insert(Table* table, void** ptr_or_null, bool clear)
{
    if(table->count >= table->capacity)
        table_reserve(table, table->count + 1);

    ASSERT(table->first_free < table->capacity);
    uint8_t* block = table->blocks[table->first_free/TABLE_BLOCK_SIZE];
    Table_Slot* slot = (Table_Slot*) (block + table->slot_size*(table->first_free%TABLE_BLOCK_SIZE));
    slot->id.gen += 1;
    ASSERT(slot->id.gen % 2 == 1 && table->first_free == slot->id.index);
    
    table->first_free = slot->next_free;
    if(ptr_or_null)
        *ptr_or_null = slot + 1;
        
    if(clear)
        memset(slot + 1, 0, table->slot_size - sizeof(Table_Slot));

    return slot->id;
}

EXTERNAL Table_ID table_insert_nozero(Table* table, void** ptr_or_null)
{
    return _table_insert(table, ptr_or_null, false);
}
EXTERNAL Table_ID table_insert(Table* table, void** ptr_or_null)
{
    return _table_insert(table, ptr_or_null, true);
}

EXTERNAL void* table_mark_changed(Table* table, Table_ID id)
{
    void* ptr = table_get_or(table, id, NULL);
    if(ptr) {
        Table_Slot* slot = (Table_Slot*) ptr - 1;
        slot->id.gen += 2;
    }
    return ptr;
}
EXTERNAL bool table_remove(Table* table, Table_ID id)
{
    void* ptr = table_get_or(table, id, NULL);
    if(ptr == NULL) 
        return false;

    Table_Slot* slot = (Table_Slot*) ptr - 1;
    slot->id.gen += 1;
    slot->next_free = table->first_free;
    table->first_free = id.index;
    ASSERT(slot->id.gen % 2 == 0);
    return true;
}


EXTERNAL void* table_get(Table* table, Table_ID id)
{
    CHECK_BOUNDS(id.index, table->capacity);
    uint8_t* block = table->blocks[id.index/TABLE_BLOCK_SIZE];
    Table_Slot* slot = (Table_Slot*) (block + table->slot_size*(id.index%TABLE_BLOCK_SIZE));
    ASSERT_BOUNDS(slot->gen == id.gen && id.gen % 2 == 1);
    return slot + 1;
}
EXTERNAL void* table_get_or(Table* table, Table_ID id, void* if_not_found)
{
    if(id.index < table->capacity && id.gen % 2 == 1) {
        uint8_t* block = table->blocks[id.index/TABLE_BLOCK_SIZE];
        Table_Slot* slot = (Table_Slot*) (block + table->slot_size*(id.index%TABLE_BLOCK_SIZE));
        if(slot->gen == id.gen) 
            return slot + 1;
    }

    return if_not_found;
}

EXTERNAL void* table_at(Table* table, isize index)
{
    CHECK_BOUNDS(index, table->capacity);
    uint8_t* block = table->blocks[uindex/TABLE_BLOCK_SIZE];
    Table_Slot* slot = (Table_Slot*) (block + table->slot_size*((uint64_t) index%TABLE_BLOCK_SIZE));
    ASSERT_BOUNDS(slot->gen % 2 == 1);
    return slot + 1;
}   

EXTERNAL void* table_at_or(Table* table, isize index, Table_Slot* if_not_found)
{
    uint64_t uindex = (uint64_t) index;
    if(uindex < table->capacity) {
    uint8_t* block = table->blocks[uindex/TABLE_BLOCK_SIZE];
        Table_Slot* slot = (Table_Slot*) (block + table->slot_size*(uindex%TABLE_BLOCK_SIZE));
        if(slot->gen % 2 == 1) 
            return slot + 1;
    }

    return if_not_found;
}

typedef struct Table_Index_Slot {
    uint64_t hash;
    Table_Slot* table_slot;
} Table_Index_Slot;

typedef struct Table_Index {
    Allocator* allocator;
    Table_Index_Slot* slots;
    uint32_t count;
    uint32_t mask; //one minus capacity
    uint32_t gravestones;
    uint32_t rehash_count;
} Table_Index;

#define TABLE_INDEX_EMPTY 0
#define TABLE_INDEX_REMOVED 1

typedef struct Table_Index_Info {
    uint32_t row_field_offset;
    uint32_t back_link_offset;
    bool     (*is_eq)(const void* stored, const void* looked_up);
    uint64_t (*hash)(const void* stored);
} Table_Index_Info;

typedef struct Table_Index_Iter {
    uint64_t hash; //already escaped!
    uint32_t index;
    uint32_t iter;
    Table_Index_Slot* slot;
} Table_Index_Iter;

//out of line
void table_index_reserve(Table_Index* index, uint32_t to_size, Table_Index_Info info);
void table_index_rehash(Table_Index* index, uint32_t to_size, Table_Index_Info info);

//inline
Table_Index_Slot* table_get_backlink(Table_Index* index, uint32_t backlink);
Table_Index_Slot* table_index_insert(Table_Index* index, const void* item, uint64_t hash, Table_Index_Info info);
Table_Index_Slot* table_index_find(Table_Index* index, const void* item, uint64_t hash, Table_Index_Info info);
void* table_index_find_get(Table_Index* index, const void* item, uint64_t hash, Table_ID* out_id_or_null, Table_Index_Info info);
bool table_index_remove(Table_Index_Slot* found);
bool table_index_remove_backlink(Table_Index* index, uint32_t backlink);

Table_Index_Iter  table_index_find_next_make(Table_Index* index, uint64_t hash);
Table_Index_Slot* table_index_find_next(Table_Index* index, const void* item, Table_Index_Iter* find, Table_Index_Info info);
Table_Index_Slot* table_index_iterate(Table_Index* index, const void* item, uint64_t hash, Table_Index_Iter* find, Table_Index_Info info);
Table_Index_Slot* table_index_search(Table* table, Table_Index* index, const void* item, uint64_t hash, Table_Index_Info info);


void table_index_reserve(Table_Index* index, uint32_t to_size, Table_Index_Info info)
{

}


void table_index_rehash(Table_Index* index, uint32_t requested_capacity)
{
    TEST(requested_capacity <= UINT32_MAX);
    
    //Unless there are many many gravestones, count them into the lest size.
    //This prevents a porblem where if the map has 11 entries and one removed entry, we will rehash
    // to the same capacity (16). If we then insert an item, remove an item we are back where we were.
    //Essentially for some unlucky sizes (one before the rehash will triger) there would be a rehash on
    // every second operation - this is of course very bad for perf
    isize least_size = map->gavestones > map->count ? map->count : map->gavestones + map->count;
    if(least_size < requested_capacity)
        least_size = requested_capacity;

    isize new_cap = 16;
    while(new_cap*3/4 <= least_size)
        new_cap *= 2;
    
    // allocate new slots and set all to empty
    uint64_t new_mask = (uint64_t) new_cap - 1;
    uint8_t* new_entries = (uint8_t*) _table_alloc(map->alloc, new_cap*sizeof(Table_Index_Slot), NULL, 0, sizeof(Table_Index_Slot));
    memset(new_entries, 0, new_cap*sizeof(Table_Index_Slot)); 

    uint64_t mask = (uint64_t) to_table->capacity - 1;
    for(uint32_t j = 0; j < from_table->capacity; j++)
    {
        Hash_Entry entry = from_table->entries[j];
        if(entry.value - from_table->empty_value > 1)
        {
            uint64_t i = entry.hash & mask;
            for(uint64_t it = 1;; it++) {
                if(to_table->entries[i].value == to_table->empty_value) {
                    to_table->entries[i] = entry;
                    break;
                }

                i = (i + it) & mask;
            }
        }
    }

    to_table->count = from_table->count;
    to_table->rehashed_times += 1;
}

Table_Index_Slot* table_index_insert(Table_Index* index, const void* item, uint64_t hash, Table_Index_Info info)
{
    table_index_reserve(index, index->count + 1, info);
    if(hash < 2)
        hash += 2;

    uint32_t i = hash & index->mask;
    for(uint32_t k = 1; ; k++)
    {
        ASSERT(k <= index->mask);
        Table_Index_Slot* slot = &index->slots[i];
        if(slot->hash == TABLE_INDEX_EMPTY || slot->hash == TABLE_INDEX_REMOVED)
            break;
        i = (i + k) & index->mask;
    }

    Table_Index_Slot* slot = &index->slots[i];
    index->count += 1;
    index->gavestones -= slot->hash == TABLE_INDEX_REMOVED;

    slot->hash = hash;
    slot->table_slot = (Table_Slot*) item - 1;

    if(info.back_link_offset != (uint32_t) -1) {
        uint8_t* back_link = slot->table_slot->data + info.back_link_offset;
        memcpy(back_link, &i, sizeof i);
    }
    
    return slot;
}

Table_Index_Slot* table_index_search(Table* table, Table_Index* index, const void* item, uint64_t hash, Table_Index_Info info)
{
    Table_Index_Slot* slot = table_index_find(index, item, hash, info);
    if(slot)
        return slot;

    TABLE_FOR_GENERIC(table, it, slot) {
        if(info.is_eq((uint8_t*) slot + info.row_field_offset, item))
        {
            table_index_insert(index, item, hash, info);
            return (Table_Index_Slot*) slot;
        }
    }

    return NULL;
}

Table_Index_Iter table_index_find_next_make(Table_Index* index, uint64_t hash)
{
    if(hash < 2)
        hash += 2;

    return Table_Index_Iter{hash, (uint32_t) hash & index->mask, 1};
}

Table_Index_Slot* table_index_find_next(Table_Index* index, const void* item, Table_Index_Iter* find, Table_Index_Info info)
{
    for(;find->iter <= index->mask; find->iter += 1)
    {
        ASSERT(find->iter <= index->mask + 1);
        Table_Index_Slot* slot = &index->slots[find->index];
        
        if(slot->hash == find->hash) {
            uint8_t* stored = slot->table_slot->data + info.row_field_offset;
            if(info.is_eq(stored, item)) {
                find->slot = slot;
                return slot;
            }
            //If the hash does not correspond to the stored data anymore
            // remove it since its outdated. If info.hash == NULL doesnt perform this
            else if(info.hash && info.hash(stored) != find->hash) 
                slot->hash = TABLE_INDEX_REMOVED;
        }
        else if(slot->hash == TABLE_INDEX_EMPTY)
            break;
        
        find->index = (find->index + find->iter) & index->mask;
    }

    find->slot = NULL;
    return NULL;
}


Table_Index_Slot* table_index_iterate(Table_Index* index, const void* item, uint64_t hash, Table_Index_Iter* find, Table_Index_Info info)
{
    if(find->iter == 0) 
        *find = table_index_find_next_make(index, hash);
    else
        find->index = (find->index + find->iter) & index->mask;
    return table_index_find_next(index, item, find, info);
}

Table_Index_Slot* table_index_find(Table_Index* index, const void* item, uint64_t hash, Table_Index_Info info)
{
    Table_Index_Iter find = table_index_find_next_make(index, hash);
    return table_index_find_next(index, item, &find, info);
}

void* table_index_find_get(Table_Index* index, const void* item, uint64_t hash, Table_ID* out_id_or_null, Table_Index_Info info)
{
    Table_Index_Slot* slot = table_index_find(index, item, hash, info);
    if(slot == NULL)
        return NULL;
    if(out_id_or_null)
        *out_id_or_null = slot->table_slot->id;
    return slot->table_slot->data;
}

bool table_index_remove(Table_Index* index, Table_Index_Slot* found)
{
    if(found) {
        found->hash = TABLE_INDEX_REMOVED;
        index->count -= 1;
        index->gravestones -= 1;
    }
    return found != NULL;
}

bool table_index_remove_backlink(Table_Index* index, uint32_t backlink)
{   
    return table_index_remove(table_get_backlink(index, backlink));
}

Table_Index_Slot* table_get_backlink(Table_Index* index, uint32_t backlink)
{
    if(backlink <= index->mask)
        return &index->slots[backlink];
    return NULL;
}

#include "../string.h"
bool table_help_builder_string_eq(const void* builder, const void* str);
bool table_help_string_string_eq(const void* str1, const void* str2);
bool table_help_64_64_eq(const void* a, const void* b);
bool table_help_32_32_eq(const void* a, const void* b);
bool table_help_16_16_eq(const void* a, const void* b);
bool table_help_8_8_eq(const void* a, const void* b);

#include "../hash_func.h"

#if 0
typedef struct My_Table_Row {
    String_Builder name;
    String_Builder path;
    int priority;

    uint32_t name_back_link;
    uint32_t path_back_link;
    uint32_t priority_back_link;
} My_Table_Row;

typedef struct My_Table {
    Allocator* allocator;
    Table table;
    Table_Index name_index;
    Table_Index path_index;
    Table_Index priority_index;
} My_Table;

#define MY_TABLE_NAME_INFO      Table_Index_Info{offsetof(My_Table_Row, name), offsetof(My_Table_Row, name_back_link), table_help_builder_string_eq}
#define MY_TABLE_PATH_INFO      Table_Index_Info{offsetof(My_Table_Row, path), offsetof(My_Table_Row, path_back_link), table_help_builder_string_eq}
#define MY_TABLE_PRIORITY_INFO  Table_Index_Info{offsetof(My_Table_Row, priority), offsetof(My_Table_Row, priority_back_link), table_help_32_32_eq}

My_Table_Row* my_table_get(My_Table* table, Table_ID id)
{
    return (My_Table_Row*) table_get(&table->table, id);
}

void _my_table_update(My_Table* table, My_Table_Row* row, Table_ID id, bool reinsert)
{
    if(row != NULL) {
        ASSERT(row == my_table_get(table, id) && table);
        table_index_remove_backlink(&table->name_index, row->name_back_link);
        table_index_remove_backlink(&table->path_index, row->path_back_link);
        table_index_remove_backlink(&table->priority_index, row->priority_back_link);

        if(reinsert) {
            table_index_insert(&table->name_index, row, hash32_fnv(row->name.data, row->name.len, 0), MY_TABLE_NAME_INFO);
            table_index_insert(&table->path_index, row, hash32_fnv(row->path.data, row->path.len, 0), MY_TABLE_PATH_INFO);
            table_index_insert(&table->priority_index, row, hash32_bijective(row->priority), MY_TABLE_PRIORITY_INFO);
        }
    }
}

void my_table_remove(My_Table* table, Table_ID id)
{
    My_Table_Row* row = my_table_get(table, id);
    _my_table_update(table, row, id, false);
    table_remove(&table->table, id);
}

Table_ID my_table_insert(My_Table* table, String name, String path, int priority)
{
    My_Table_Row* row = NULL;
    Table_ID id = table_insert(&table->table, (void**) &row);
    row->name = builder_of(table->allocator, name);
    row->path = builder_of(table->allocator, path);
    row->priority = priority;

    _my_table_update(table, row, id, true);
    return id;
}

My_Table_Row* my_table_update(My_Table* table, Table_ID id)
{
    My_Table_Row* row = my_table_get(table, id);
    _my_table_update(table, row, id, true);
    return row;
}

My_Table_Row* my_table_get_by_name(My_Table* table, String name, Table_ID* out_id)
{
    uint32_t hash = hash32_fnv(name.data, name.count, 0);
    return (My_Table_Row*) table_index_find_get(&table->name_index, &name, hash, out_id, MY_TABLE_NAME_INFO);
}

My_Table_Row* my_table_get_by_path(My_Table* table, String path, Table_ID* out_id)
{
    uint32_t hash = hash32_fnv(path.data, path.count, 0);
    return (My_Table_Row*) table_index_find_get(&table->path_index, &path, hash, out_id, MY_TABLE_PATH_INFO);
}

My_Table_Row* my_table_get_by_priority(My_Table* table, int priority, Table_ID* out_id)
{
    uint32_t hash = hash32_bijective(priority);
    return (My_Table_Row*) table_index_find_get(&table->priority_index, &priority, hash, out_id, MY_TABLE_PRIORITY_INFO);
}
#else

#include "../stable_array.h"
#include "../hash.h"
#include "../hash_string.h"

typedef struct My_Table_Row {
    Table_ID id;
    String_Builder name;
    String_Builder path;
    int priority;
    
    uint32_t name_backlink;
    uint32_t path_backlink;
    uint32_t priority_backlink;
} My_Table_Row;

typedef struct My_Table {
    Allocator* allocator;
    Stable_Array table;
    Hash name_hash;
    Hash path_hash;
    Hash priority_hash;
} My_Table;

My_Table_Row* my_table_get(My_Table* table, Table_ID id)
{
    return (My_Table_Row*) table_get(&table->table, id);
}

void _my_table_remove_hashes(My_Table* table, My_Table_Row* row)
{
    if(row != NULL) {
        hash_remove(&table->name_hash, row->name_backlink);
        hash_remove(&table->path_hash, row->path_backlink);
        hash_remove(&table->priority_hash, row->priority_backlink);

        row->name_backlink = -1;
        row->path_backlink = -1;
        row->priority_backlink = -1;
    }
}

void _my_table_insert_hashes(My_Table* table, My_Table_Row* row)
{
    if(row != NULL) {
        hash_backlink_reserve(&table->name_hash, table->name_hash.count + 1ll, NULL, 1, offsetof(My_Table_Row, name_backlink));
        hash_backlink_reserve(&table->path_hash, table->path_hash.count + 1ll, NULL, 1, offsetof(My_Table_Row, path_backlink));
        hash_backlink_reserve(&table->priority_hash, table->priority_hash.count + 1ll, NULL, 1, offsetof(My_Table_Row, priority_backlink));

        row->name_backlink = hash_insert(&table->name_hash, hash_string(row->name.string), (uint64_t) row);
        row->path_backlink = hash_insert(&table->path_hash, hash_string(row->path.string), (uint64_t) row);
        row->priority_backlink = hash_insert(&table->priority_hash, hash32_bijective(row->priority), (uint64_t) row);
    }
}

bool my_table_remove(My_Table* table, Table_ID id)
{
    My_Table_Row* row = my_table_get(table, id);
    if(row) {
        row->id.gen += 1;
        _my_table_remove_hashes(table, row);
        stable_array_remove(&table->table, id.index);
    }
    return row != NULL;
}

Table_ID my_table_insert(My_Table* table, String name, String path, int priority)
{
    My_Table_Row* row = NULL;
    stable_array_insert_zero_from(&table->table, (void**) &row, offsetof(My_Table_Row, id));
    row->name = builder_of(table->allocator, name);
    row->path = builder_of(table->allocator, path);
    row->priority = priority;

    _my_table_insert_hashes(table, row);
    return row->id;
}

My_Table_Row* my_table_update(My_Table* table, Table_ID id)
{
    My_Table_Row* row = my_table_get(table, id);
    _my_table_remove_hashes(table, row);
    _my_table_insert_hashes(table, row);
    return row;
}

My_Table_Row* my_table_get_by_name(My_Table* table, String name, Table_ID* out_id)
{
    uint32_t hash = hash32_fnv(name.data, name.count, 0);
    for(Hash_It it = {0}; hash_iterate(&table->name_hash, hash, &it); ) {
        My_Table_Row* row = (My_Table_Row*) it.entry->value_ptr;
        if(string_is_equal(row->name.string, name)){
            if(out_id) 
                *out_id = row->id; 
            return row;
        }
    }
    return NULL;
}

My_Table_Row* my_table_get_by_path(My_Table* table, String path, Table_ID* out_id)
{
    uint32_t hash = hash32_fnv(path.data, path.count, 0);
    for(Hash_It it = {0}; hash_iterate(&table->path_hash, hash, &it); ) {
        My_Table_Row* row = (My_Table_Row*) it.entry->value_ptr;
        if(string_is_equal(row->path.string, path)) {
            if(out_id) 
                *out_id = row->id; 
            return row;
        }
    }
    return NULL;
}

My_Table_Row* my_table_get_by_priority(My_Table* table, int priority, Table_ID* out_id)
{
    uint32_t hash = hash32_bijective(priority);
    for(Hash_It it = {0}; hash_iterate(&table->path_hash, hash, &it); ) {
        My_Table_Row* row = (My_Table_Row*) it.entry->value_ptr;
        if(out_id) 
            *out_id = row->id; 
        return row;
    }
    return NULL;
}

#endif



typedef struct Game_Door {
    Table_ID id;

    Table_ID level_from_id;
    Table_ID level_to_id;
    Table_ID lock_id;

    String door_name;
    isize activation_count;
} Game_Door;

typedef struct Game_Door_Table {
    union {
        Table table;
        struct {
            Allocator* allocator;
            Game_Door** slots;
        };
    };

    Table_Index level_from_index;
    Table_Index level_to_index;
    Table_Index lock_index;
    Table_Index name_index;
};