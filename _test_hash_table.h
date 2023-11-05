#pragma once

#include "hash_table.h"
#include "string.h"
#include "hash.h"

#include "_test.h"
#include "allocator_debug.h"

INTERNAL void test_hash_table_stress(f64 max_seconds)
{
    (void) max_seconds;

    Debug_Allocator debug_allocator = {0};
    debug_allocator_init_use(&debug_allocator, DEBUG_ALLOCATOR_DEINIT_LEAK_CHECK | DEBUG_ALLOCATOR_PRINT | DEBUG_ALLOCATOR_CONTINUOUS);

    i64_Hash_Table table = {0};
    hash_table_init(&table, &debug_allocator.allocator, sizeof(i64), 0);

    String keys[] = {STRING("Key1"), STRING("Key2"), STRING("Long Long Key"), STRING("Key4"), }; 
    i64 values[] = {1, 2, 3, 4};

    Hash_Found found = {0};
    found = hash_table_insert(&table, keys[0], &values[0]);
    found = hash_table_insert(&table, keys[1], &values[1]);
    found = hash_table_insert(&table, keys[2], &values[2]);
    found = hash_table_insert(&table, keys[3], &values[3]);
    TEST(table.size == 4);
    TEST(found.entry != -1);

    i64* val4 = hash_table_get(&table, keys[3]);
    TEST(val4 != NULL && *val4 == values[3]);

    Hash_Found found3 = hash_table_find(&table, keys[2]);
    TEST(found3.entry != -1);

    i64 removed_value = 0;

    hash_table_remove_found(&table, found3, &removed_value);
    found3 = hash_table_find(&table, keys[2]);
    TEST(removed_value == values[2]);
    TEST(found3.entry == -1);
    TEST(table.size == 3);


    hash_table_deinit(&table);

    debug_allocator_deinit(&debug_allocator);
}