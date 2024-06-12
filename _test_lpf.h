#pragma once

#include "lpf.h"
static void test_lpf_entry_full(Lpf_Entry* entry, Lpf_Kind kind, const char* label, const char* value, i32 indentation, i32 blanks_before, i32 line)
{
    TEST(entry != NULL);
    TEST(entry->kind == kind);
    if(blanks_before != -1)
        TEST(entry->blanks_before == blanks_before);
    if(line != -1)
        TEST(entry->line == line);
    if(indentation != -1)
        TEST(entry->indentation == indentation);
    TEST(string_is_equal(entry->label, string_make(label)));
    TEST(string_is_equal(entry->value, string_make(value)));
}

static void test_lpf_entry(Lpf_Entry* entry, Lpf_Kind kind, const char* label, const char* value)
{
    test_lpf_entry_full(entry, kind, label, value, -1, -1, -1);
}

static void test_lpf_print_compariosn(String left, String right)
{
    isize max_left = 0;
    isize max_right = 0;

    for(Line_Iterator it = {0}; line_iterator_get_line(&it, left); )
        max_left = MAX(max_left, it.line.size);
        
    for(Line_Iterator it = {0}; line_iterator_get_line(&it, right); )
        max_right = MAX(max_right, it.line.size);
    
    String_Builder builder_left = {0};
    String_Builder builder_right = {0};

    builder_resize(&builder_left, max_left);
    builder_resize(&builder_left, max_left);

    Line_Iterator it_left = {0};
    Line_Iterator it_right = {0};
    for(;;)
    {
        bool has_left = line_iterator_get_line(&it_left, left);
        bool has_right = line_iterator_get_line(&it_right, right);

        if(has_left == false && has_right == false)
            break;
            
        String line_left = has_left ? it_left.line : STRING("");
        String line_right = has_right ? it_right.line : STRING("");

        builder_clear(&builder_left);
        //builder_push(&builder_left, '"');
        builder_append(&builder_left, line_left);
        //builder_push(&builder_left, '"');
        while(builder_left.size < max_left)
            builder_push(&builder_left, ' ');
            
        builder_clear(&builder_right);
        //builder_push(&builder_right, '"');
        builder_append(&builder_right, line_right);
        //builder_push(&builder_right, '"');
        while(builder_right.size < max_right)
            builder_push(&builder_right, ' ');

        if(string_is_equal(line_left, line_right))
            printf("%s == %s\n", builder_left.data, builder_right.data);
        else
        {
            printf("%s != %s\n", builder_left.data, builder_right.data);
            for(isize i = 0; i < line_left.size; i++)
            {
                if(builder_left.data[i] == ' ')
                    builder_left.data[i] = '.';
                    
                if(builder_left.data[i] == '\t')
                    builder_left.data[i] = '/';
            }
            
            for(isize i = 0; i < line_right.size; i++)
            {
                if(builder_right.data[i] == ' ')
                    builder_right.data[i] = '.';
                    
                if(builder_right.data[i] == '\t')
                    builder_right.data[i] = '/';
            }
            
            printf("%s -- %s\n", builder_left.data, builder_right.data);
        }
    }

    builder_deinit(&builder_left);
    builder_deinit(&builder_right);
}

static void test_lpf()
{
    {
        Arena_Frame scratch = scratch_arena_acquire();

        Lpf_Entry root = lpf_read(&scratch, STRING(
            "\n first \t: value "
            "\n "
            "\n \tsecond: value\t"
            "\n         , continuation"
            "\n \t"
            "\n "
            "\n \t third*: value"
            "\n          ;  escaped"
            "\n # comment"
            "\n #  with continuation"
        ), NULL);

        test_lpf_entry_full(&root, LPF_COLLECTION, "", "", 0, 0, 0);
        TEST(root.children_count == 4);
        TEST(root.children != NULL);

        test_lpf_entry_full(&root.children[0], LPF_ENTRY,     "first", "value ",                  1, 1, 2);
        test_lpf_entry_full(&root.children[1], LPF_ENTRY,     "second", "value\t\ncontinuation",    5, 1, 4);
        test_lpf_entry_full(&root.children[2], LPF_ENTRY,     "third*", "value escaped",          6, 2, 8);
        test_lpf_entry_full(&root.children[3], LPF_COMMENT,   "", "comment\n with continuation",  1, 0, 10);

        arena_frame_release(&scratch);
    }

    {
        Arena_Frame scratch = scratch_arena_acquire();

        Lpf_Entry root = lpf_read(&scratch, STRING(
            "\n out: value "
            "\n col1 { "
            "\n    inside1: value1"
            "\n    inside2: value2"
            "\n           , continuation"
            "\n    "
            "\n    # comment"
            "\n    #  with continuation comment"
            "\n    col2 { \t"
            "\n        key: value"
            "\n    }"
            "\n     "
            "\n    col3 {}"
            "\n }"
            "\n }"
        ), NULL);

        test_lpf_entry_full(&root, LPF_COLLECTION, "", "", 0, 0, 0);
        TEST(root.children_count == 2);
        TEST(root.children != NULL);

        test_lpf_entry(&root.children[0], LPF_ENTRY, "out", "value ");
        test_lpf_entry(&root.children[1], LPF_COLLECTION, "col1", "");
        
        Lpf_Entry* col1 = &root.children[1];
        TEST(col1->children_count == 5);
        TEST(col1->children != NULL);
        Lpf_Entry* col2 = &col1->children[3];
        Lpf_Entry* col3 = &col1->children[4];

        test_lpf_entry(&col1->children[0], LPF_ENTRY, "inside1", "value1");
        test_lpf_entry(&col1->children[1], LPF_ENTRY, "inside2", "value2\ncontinuation");
        test_lpf_entry(&col1->children[2], LPF_COMMENT, "", "comment\n with continuation comment");
        test_lpf_entry(&col1->children[3], LPF_COLLECTION, "col2", "");
        test_lpf_entry(&col1->children[4], LPF_COLLECTION, "col3", "");
        
        TEST(col2->children_count == 1);
        TEST(col2->children != NULL);
        TEST(col3->children_count == 0);
        TEST(col3->children == NULL);
        test_lpf_entry(&col2->children[0], LPF_ENTRY, "key", "value");
        
        
        arena_frame_release(&scratch);
    }

    {
        Arena_Frame scratch = scratch_arena_acquire();

        Lpf_Entry root = lpf_read(&scratch, STRING(
            "\n out :value"
            "\n col1 { "
            "\n    inside : value1"
            "\n    inside_long: value2"
            "\n               , continuation_thats_too_long"
            "\n    "
            "\n    #comment"
            "\n    #  with continuation comment"
            "\n    col2 { \t"
            "\n        key: value"
            "\n    }"
            "\n     "
            "\n    col3{"
            "\n    }"
            "\n }"
        ), NULL);
    
        String expected = STRING(
            "\nout: value"
            "\ncol1 {"
            "\n   inside     : value1"
            "\n   inside_long: value2"
            "\n              , continuation_thats_"
            "\n              ; too_long"
            "\n"
            "\n   # comment"
            "\n   #  with continuation "
            "\n   # comment"
            "\n   col2 {"
            "\n      key: value"
            "\n   }"
            "\n"
            "\n   col3 {}"
            "\n}"
            "\n"
        );

        Lpf_Write_Options options = lpf_default_write_options();
        options.max_line_width = 19;
        options.indentations_per_level = 3;

        String formatted = lpf_write_from_root(&scratch, root, &options);
        if(string_is_equal(formatted, expected) == false)
            test_lpf_print_compariosn(formatted, expected);

        TEST(string_is_equal(formatted, expected));

        Lpf_Entry root_roundtrip = lpf_read(&scratch, formatted, NULL);
        String formatted_roundtrip = lpf_write_from_root(&scratch, root_roundtrip, &options);
        TEST(string_is_equal(formatted, formatted_roundtrip));
        
        arena_frame_release(&scratch);
    }
}