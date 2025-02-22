
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

typedef struct Table_ID {
    uint32_t index;
    uint32_t gen;
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
    uint32_t count;
    uint32_t capacity;

    uint8_t** blocks;
    uint32_t blocks_count;
    uint32_t blocks_capacity;

    uint32_t first_free;

    uint32_t slot_size;
    uint32_t item_align;
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
    Table_Slot* block;
    uint32_t block_i;
    uint32_t slot_i;
    Table_ID id;
    bool did_break;
} Table_Iterator;

inline static Table_Iterator _table_it_precond(Table* table, isize from_id);
inline static bool  _table_it_cond(Table_Iterator* it);
inline static void  _table_it_postcond(Table_Iterator* it);
inline static void* _table_it_per_slot(Table_Iterator* it, isize item_size);

#define TABLE_FOR_CUSTOM(table_ptr, it, T, item, item_size, from_id) \
    for(Table_Iterator it = _table_it_precond(table_ptr, from_id); _table_it_cond(&it); _table_it_postcond(&it)) \
        for(T* item = NULL; it.slot_i < TABLE_BLOCK_SIZE; it.slot_i++) \
            if(item = (T*) _table_it_per_slot(&it, item_size)) \

#define TABLE_FOR_GENERIC(table_ptr, it, item) TABLE_FOR_CUSTOM((table_ptr), it, void, item, it.table->item_size, 0)
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

inline static void* _table_it_per_slot(Table_Iterator* it, isize item_size)
{
    ASSERT(item_size + sizeof(Table_Slot) == it->table->slot_size);
    uint8_t* slot_u8 = (uint8_t*) (void*) it->block + (item_size + sizeof(Table_Slot))*it->slot_i;
    Table_Slot* slot = (Table_Slot*) (void*) slot_u8;
    if(slot->gen % 2 == 1)
    {
        it->id.index = it->slot_i + it->block_i*TABLE_BLOCK_SIZE;
        it->id.gen = slot->gen;
        return slot->data;
    }

    return NULL;
}

static inline Table_Slot* table_slot_at(Table* table, isize index, isize item_size)
{
    uint64_t uindex = (uint64_t) index;
    uint64_t slot_size = (uint64_t) item_size + sizeof(Table_Slot);
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

EXTERNAL void table_init(Table* table, isize item_size, isize allocation_granularity_or_zero)
{
    table_deinit(table);
    table->item_size = (u32) item_size;
    table->allocation_granularity = (u32) allocation_granularity_or_zero;
}
EXTERNAL void table_deinit(Table* table)
{
    _table_array_check_invariants(table);
    uint64_t* was_alloced = (uint64_t*) (void*) (table->blocks + table->blocks_capacity);
    for(uint32_t i = 0; i < table->blocks_count; )
    {
        uint32_t k = i;
        for(i += 1; i < table->blocks_count; i++) 
            if(was_alloced[i/64] & (1ull << (i%64)))
                break;

        _table_alloc(table->allocator, 0, table->blocks[k].ptr, (i - k)*TABLE_BLOCK_SIZE*table->item_size, table->item_align);
    }

    if(table->blocks_count)
        _table_alloc(table->allocator, 0, table->blocks, table->blocks_count*sizeof(table_Array_Block), 8);
    memset(table, 0, sizeof *table);
}
EXTERNAL void table_reserve(Table* table, isize to_count)
{
    if(to_count > table_capacity(table))
    {
        _table_check_invariants(table);
        
        isize desired_items = table->allocation_size/table->item_size;
        if(desired_items < to_count)
            desired_items = to_count;
        isize added_blocks = (desired_items + TABLE_BLOCK_SIZE - 1)/TABLE_BLOCK_SIZE;

        //If the ptr array needs reallocating
        if(table->blocks_count + added_blocks > table->blocks_capacity)
        {
            isize old_capacity = table->blocks_capacity;
            isize new_capacity = 16;
            while(new_capacity < table->blocks_count + added_blocks)
                new_capacity *= 2;

            isize old_alloced = old_capacity*sizeof(Table_Slot*) + old_capacity/64*sizeof(uint64_t);
            isize new_alloced = new_capacity*sizeof(Table_Slot*) + new_capacity/64*sizeof(uint64_t);
            Table_Slot** blocks = (Table_Slot**) _table_alloc(table->allocator, new_alloced, table->blocks, old_alloced, 8);

            //copy over the was_allocated info
            uint64_t* old_was_alloced = (uint64_t*) (void*) (blocks + old_capacity);
            uint64_t* new_was_alloced = (uint64_t*) (void*) (blocks + new_capacity);
            for(isize i = 0; i < (new_capacity + 63)/64; i++)
                new_was_alloced = i < (old_capacity + 63)/64 ? old_was_alloced[i] : 0;

            //clear the rest
            memset(blocks + old_capacity, 0, (size_t) (new_capacity - old_capacity)*sizeof(Table_Slot*));
            table->blocks = blocks;
            table->blocks_capacity = (uint32_t) new_capacity;
        }

        ASSERT(table->blocks_count < table->blocks_capacity);

        //allocate the actual storage blocks 
        isize alloced_blocks_bytes = added_blocks*table->item_size*TABLE_BLOCK_SIZE;
        uint8_t* alloced_blocks = (uint8_t*) _table_alloc(table->allocator, alloced_blocks_bytes, NULL, 0, table->item_align);
        memset(alloced_blocks, 0, (size_t) alloced_blocks_bytes);

        //Add the blocks into our array (backwards so that the next added item has lowest index)
        for(uint32_t i = (uint32_t) added_blocks; i-- > 0;)
        {
            table->blocks[i + table->blocks_count] = alloced_blocks + i*table->item_size*TABLE_BLOCK_SIZE;
            table->first_free = i + table->blocks_count + 1;
        }
        
        //mark the first block as alloced
        uint64_t* was_alloced = (uint64_t*) (void*) (blocks + old_capacity);
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
    uint32_t hash;
    uint32_t gen;
    Table_Slot* table_slot;
} Table_Index_Slot;

typedef struct Table_Index {
    void* allocator;
    Table_Index_Slot* slots;
    uint32_t count;
    uint32_t mask; //one minus capacity
    uint32_t removed_count;
    uint16_t load_factor;
    uint16_t rehash_count;
} Table_Index;

#define TABLE_INDEX_EMPTY 0
#define TABLE_INDEX_REMOVED 1

typedef struct Table_Index_Info {
    uint32_t row_field_offset;
    uint32_t back_link_offset;
    bool (*is_eq)(const void* stored, const void* looked_up);
} Table_Index_Info;

//out of line
void table_index_reserve(Table_Index* index, uint32_t to_size, Table_Index_Info info);
void table_index_rehash(Table_Index* index, uint32_t to_size, Table_Index_Info info);

//inline
Table_Index_Slot* table_get_backlink(Table_Index* index, uint32_t backlink);
Table_Index_Slot* table_index_insert(Table_Index* index, const void* item, uint32_t gen, uint32_t hash, Table_Index_Info info);
Table_Index_Slot* table_index_find(Table_Index* index, const void* item, uint32_t hash, Table_Index_Info info);
void* table_index_find_get(Table_Index* index, const void* item, uint32_t hash, Table_ID* out_id_or_null, Table_Index_Info info);
bool table_index_remove(Table_Index_Slot* found)
bool table_index_remove_backlink(Table_Index* index, uint32_t backlink);

typedef struct Table_Index_Iter {
    uint32_t hash; //already escaped!
    uint32_t index;
    uint32_t iter;
    Table_Index_Slot* slot;
} Table_Index_Iter;

Table_Index_Iter table_index_find_next_make(Table_Index* index, uint32_t hash);
Table_Index_Slot* table_index_find_next(Table_Index* index, const void* item, Table_Index_Iter* find, Table_Index_Info info);
Table_Index_Slot* table_index_iterate(Table_Index* index, const void* item, uint32_t hash, Table_Index_Iter* find, Table_Index_Info info);

Table_Index_Slot* table_index_insert(Table_Index* index, const void* item, uint32_t gen, uint32_t hash, Table_Index_Info info)
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
    index->removed_count -= slot->gen == TABLE_INDEX_REMOVED;

    slot->gen = gen;
    slot->hash = hash;
    slot->table_slot = (Table_Slot*) item - 1;

    if(info.back_link_offset != (uint32_t) -1) {
        uint8_t* back_link = slot->table_slot->data + info.back_link_offset;
        memcpy(back_link, &i, sizeof i);
    }
    
    return slot;
}

Table_Index_Iter table_index_find_next_make(Table_Index* index, uint32_t hash)
{
    if(hash < 2)
        hash += 2;

    return Table_Index_Iter{hash, hash & index->mask, 1};
}

Table_Index_Slot* table_index_find_next(Table_Index* index, const void* item, Table_Index_Iter* find, Table_Index_Info info)
{
    for(;find->iter <= index->mask; find->iter += 1)
    {
        ASSERT(find->iter <= index->mask + 1);
        Table_Index_Slot* slot = &index->slots[find->index];
        
        if(slot->hash == find->hash) {
            Table_Slot* table_slot = slot->table_slot;
            if(table_slot->gen == slot->gen)
                if(info.is_eq(table_slot->data + info.row_field_offset, item))
                    return slot;

            //data changed...
            slot->hash = TABLE_INDEX_REMOVED;
        }
        else if(slot->hash == TABLE_INDEX_EMPTY)
            break;
        
        find->index = (find->index + find->iter) & index->mask;
    }

    return NULL;
}

Table_Index_Slot* table_index_find(Table_Index* index, const void* item, uint32_t hash, Table_Index_Info info)
{
    Table_Index_Iter find = table_index_find_next_make(index, hash);
    return table_index_find_next(index, item, &find, info);
}

void* table_index_find_get(Table_Index* index, const void* item, uint32_t hash, Table_ID* out_id_or_null, Table_Index_Info info)
{
    Table_Index_Slot* slot = table_index_find(index, item, hash, info);
    if(slot == NULL)
        return NULL;
    if(out_id_or_null)
        *out_id_or_null = slot->table_slot->id;
    return slot->table_slot->data;
}

bool table_index_remove(Table_Index_Slot* found)
{
    if(found)
        found->hash = TABLE_INDEX_REMOVED;
    return found != NULL;
}

bool table_index_remove_backlink(Table_Index* index, uint32_t backlink)
{   
    return table_index_remove(table_get_backlink(index, backlink));
}

#include "../string.h"
bool table_help_builder_string_eq(const void* builder, const void* str);
bool table_help_string_string_eq(const void* str1, const void* str2);
bool table_help_64_64_eq(const void* a, const void* b);
bool table_help_32_32_eq(const void* a, const void* b);
bool table_help_16_16_eq(const void* a, const void* b);
bool table_help_8_8_eq(const void* a, const void* b);

#include "../hash_func.h"
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
            table_index_insert(&table->name_index, row, id.gen, hash32_fnv(row->name.data, row->name.len, 0), MY_TABLE_NAME_INFO);
            table_index_insert(&table->path_index, row, id.gen, hash32_fnv(row->path.data, row->path.len, 0), MY_TABLE_PATH_INFO);
            table_index_insert(&table->priority_index, row, id.gen, hash32_bijective(row->priority), MY_TABLE_PRIORITY_INFO);
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
    row->name = builder_from_string(table->allocator, name);
    row->path = builder_from_string(table->allocator, path);
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
    uint32_t hash = hash32_fnv(name.data, name.len, 0);
    return (My_Table_Row*) table_index_find_get(&table->name_index, &name, hash, out_id, MY_TABLE_NAME_INFO);
}

My_Table_Row* my_table_get_by_path(My_Table* table, String path, Table_ID* out_id)
{
    uint32_t hash = hash32_fnv(path.data, path.len, 0);
    return (My_Table_Row*) table_index_find_get(&table->path_index, &path, hash, out_id, MY_TABLE_PATH_INFO);
}

My_Table_Row* my_table_get_by_priority(My_Table* table, int priority, Table_ID* out_id)
{
    uint32_t hash = hash32_bijective(priority);
    return (My_Table_Row*) table_index_find_get(&table->priority_index, &priority, hash, out_id, MY_TABLE_PRIORITY_INFO);
}
