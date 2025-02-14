#include <stdint.h>
#include <stdbool.h>

#define ASSERT(x)
#define INLINE_ALWAYS inline

typedef int64_t isize;

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
            uint32_t generation;
        };
    };

    uint8_t data[];
} Table_Slot;

#define TABLE_BLOCK_SIZE 64
typedef struct Table {
    void* allocator;

    Table_Slot** blocks;
    bool*    blocks_alloced;
    uint32_t blocks_count;
    uint32_t blocks_capacity;

    uint32_t first_free;
    uint32_t count;
    uint32_t capacity;

    uint32_t item_size;
    uint32_t allocation_granularity;
} Table;

void table_init(Table* table, isize size, isize allocation_granularity_or_zero);
void table_deinit(Table* Table);
void table_reserve(Table* table, isize size);

Table_ID table_id_from_ptr(void* ptr);
Table_ID table_insert_nozero(Table* table, void** ptr_or_null); //inserts new empty entry 
Table_ID table_insert(Table* table, void** ptr_or_null); //inserts new empty entry and memsets it to zero
void* table_mark_changed(Table* table, Table_ID id); //finds the id and increments its generation counter, effectively simulating remove + insert a new copy
bool table_remove(Table* table, Table_ID id); //removes the specified entry. Returns if the entry was found (and thus removed).

void* table_get(Table* table, Table_ID id);
void* table_get_or(Table* table, Table_ID id, void* if_not_found);

Table_Slot* table_get_by_index(Table* table, isize index);
Table_Slot* table_get_by_index_or(Table* table, isize index, Table_Slot* if_not_found);

typedef struct Table_Iterator {
    Table* table;
    Table_Slot* block;
    uint32_t block_i;
    uint32_t slot_i;
    Table_ID id;
    bool did_break;
} Table_Iterator;

static INLINE_ALWAYS Table_Iterator _table_it_precond(Table* table, isize from_id);
static INLINE_ALWAYS bool  _table_it_cond(Table_Iterator* it);
static INLINE_ALWAYS void  _table_it_postcond(Table_Iterator* it);
static INLINE_ALWAYS void* _table_it_per_slot(Table_Iterator* it, isize item_size);

#define TABLE_FOR_CUSTOM(table_ptr, it, T, item, item_size, from_id) \
    for(Table_Iterator it = _table_it_precond(table_ptr, from_id); _table_it_cond(&it); _table_it_postcond(&it)) \
        for(T* item = NULL; it.slot_i < TABLE_BLOCK_SIZE; it.slot_i++) \
            if(item = (T*) _table_it_per_slot(&it, item_size)) \

#define TABLE_FOR_GENERIC(table_ptr, it, item) TABLE_FOR_CUSTOM((table_ptr), it, void, item, it.table->item_size, 0)
#define TABLE_FOR(table_ptr, it, T, item) TABLE_FOR_CUSTOM((table_ptr), it, T, item, sizeof(T), 0)

static Table_Iterator _table_it_precond(Table* table, isize from_id)
{
    Table_Iterator it = {0};
    it.table = table;
    it.slot_i = from_id % TABLE_BLOCK_SIZE;
    it.block_i = from_id % TABLE_BLOCK_SIZE;
    return it;
}

static bool _table_it_cond(Table_Iterator* it)
{
    if(it->did_break == false && it->block_i < it->table->blocks_count)
    {
        it->block = it->table->blocks[it->block_i];
        return true;
    }
    return false;
}

static void _table_it_postcond(Table_Iterator* it)
{
    it->did_break = (it->slot_i != TABLE_BLOCK_SIZE);
    it->slot_i = 0;
    it->block_i++;
}

static void* _table_it_per_slot(Table_Iterator* it, isize item_size)
{
    ASSERT(item_size == it->table->item_size);
    uint8_t* slot_u8 = (uint8_t*) (void*) it->block + (item_size + sizeof(Table_Slot))*it->slot_i;
    Table_Slot* slot = (Table_Slot*) (void*) slot_u8;
    if(slot->generation % 2 == 1)
    {
        it->id.index = it->slot_i + it->block_i*TABLE_BLOCK_SIZE;
        it->id.gen = slot->generation;
        return slot->data;
    }

    return NULL;
}

typedef struct Table_Index_Slot {
    uint32_t hash;
    uint32_t generation;
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

#include "array.h"
typedef Array(Table_Slot*) Table_Slot_Ptr_Array;
typedef Array(Table_ID) Table_ID_Array;

void table_index_reserve(Table_Index* index, uint32_t to_size, Table_Index_Info info);
void table_index_rehash(Table_Index* index, uint32_t to_size, Table_Index_Info info);

Table_Index_Slot* table_index_insert(Table_Index* index, const void* item, uint32_t gen, uint32_t hash, Table_Index_Info info);
Table_Index_Slot* table_index_find(Table_Index* index, const void* item, uint32_t hash, Table_Index_Info info);
void* table_index_find_get(Table_Index* index, const void* item, uint32_t hash, Table_ID* out_id_or_null, Table_Index_Info info);
isize table_index_find_all_append_ptrs(Table_Index* index, Table_Slot_Ptr_Array* into, const void* item, uint32_t hash, Table_Index_Info info);
isize table_index_find_all_append_ids(Table_Index* index, Table_ID_Array* into, const void* item, uint32_t hash, Table_Index_Info info);
Table_Slot_Ptr_Array table_index_find_all_ptrs(Table_Index* index, Allocator* alloc, const void* item, uint32_t hash, Table_Index_Info info);
Table_ID_Array table_index_find_all_ids(Table_Index* index, Allocator* alloc, const void* item, uint32_t hash, Table_Index_Info info);

void* table_index_find_get(Table_Index* index, const void* item, uint32_t hash, Table_ID* out_id_or_null, Table_Index_Info info);
bool  table_index_remove(Table_Index* index, void* item, uint32_t hash, Table_Index_Info info);
isize table_index_remove_all(Table_Index* index, const void* item, uint32_t hash, Table_Index_Info info);
bool table_index_remove_found(Table_Index_Slot* found);
bool table_index_remove_backlink(Table_Index* index, uint32_t backlink);

typedef struct Table_Index_Find_It {
    uint32_t hash; //already escaped!
    uint32_t index;
    uint32_t iter;
} Table_Index_Find_It;

Table_Index_Slot* table_index_find_next(Table_Index* index, const void* item, Table_Index_Find_It* find, Table_Index_Info info);

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
    index->removed_count -= slot->generation == TABLE_INDEX_REMOVED;

    slot->generation = gen;
    slot->hash = hash;
    slot->table_slot = (Table_Slot*) item - 1;

    if(info.back_link_offset != (uint32_t) -1) {
        uint8_t* back_link = slot->table_slot->data + info.back_link_offset;
        memcpy(back_link, &i, sizeof i);
    }
    
    return slot;
}


Table_Index_Find_It table_index_find_next_make(Table_Index* index, uint32_t hash)
{
    if(hash < 2)
        hash += 2;

    return Table_Index_Find_It{hash, hash & index->mask, 1};
}
Table_Index_Slot* table_index_find_next(Table_Index* index, const void* item, Table_Index_Find_It* find, Table_Index_Info info)
{
    for(;find->iter <= index->mask; find->iter += 1)
    {
        ASSERT(find->iter <= index->mask + 1);
        Table_Index_Slot* slot = &index->slots[find->index];
        
        if(slot->hash == find->hash) {
            Table_Slot* table_slot = slot->table_slot;
            if(table_slot->generation == slot->generation)
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
    Table_Index_Find_It find = table_index_find_next_make(index, hash);
    return table_index_find_next(index, item, &find, info);
}

isize table_index_remove_all(Table_Index* index, const void* item, uint32_t hash, Table_Index_Info info)
{
    Table_Index_Find_It find = table_index_find_next_make(index, hash);
    isize count = 0;
    for(Table_Index_Slot* slot = NULL; slot = table_index_find_next(index, item, &find, info); count += 1)
        table_index_remove_found(slot);

    return count;
}

isize table_index_find_all_append_ptrs(Table_Index* index, Table_Slot_Ptr_Array* into, const void* item, uint32_t hash, Table_Index_Info info)
{
    Table_Index_Find_It find = table_index_find_next_make(index, hash);
    isize count = 0;
    for(Table_Index_Slot* slot = NULL; slot = table_index_find_next(index, item, &find, info); count += 1)
        array_push(into, slot->table_slot);

    return count;
}

isize table_index_find_all_append_ids(Table_Index* index, Table_ID_Array* into, const void* item, uint32_t hash, Table_Index_Info info)
{
    Table_Index_Find_It find = table_index_find_next_make(index, hash);
    isize count = 0;
    for(Table_Index_Slot* slot = NULL; slot = table_index_find_next(index, item, &find, info); count += 1)
        array_push(into, slot->table_slot->id);

    return count;
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

Table_Index_Slot* table_index_find_or_linear(Table* table, Table_Index* index, const void* item, uint32_t hash, Table_Index_Info info)
{
    Table_Index_Slot* found = table_index_find(index, item, hash, info);
    if(found)
        return found;

    TABLE_FOR_GENERIC(table, it, curr_item) {
        if(info.is_eq((uint8_t*) curr_item + info.row_field_offset, item))
            return table_index_insert(index, item, it.id.gen, hash, info);
    }
    return NULL;
}

bool table_index_remove(Table_Index* index, void* item, uint32_t hash, Table_Index_Info info)
{
    Table_Index_Slot* slot = table_index_find(index, item, hash, info);
    return table_index_remove_found(slot);
}

bool table_index_remove_found(Table_Index_Slot* found)
{
    if(found)
        found->hash = TABLE_INDEX_REMOVED;
    return found != NULL;
}

#include "string.h"
bool table_help_builder_string_eq(const void* builder, const void* str);
bool table_help_string_string_eq(const void* str1, const void* str2);
bool table_help_64_64_eq(const void* a, const void* b);
bool table_help_32_32_eq(const void* a, const void* b);
bool table_help_16_16_eq(const void* a, const void* b);
bool table_help_8_8_eq(const void* a, const void* b);

#include "hash_func.h"
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
