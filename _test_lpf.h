#pragma once
#include "_test.h"
#include "format_lpf.h"
#include "string.h"
#include "serialize.h"

typedef struct Lpf_Test_Entry {
    Lpf_Kind kind;

    const char* label;
    const char* type;
    const char* value;
    const char* comment;

    Lpf_Error error;
} Lpf_Test_Entry;

Lpf_Test_Entry lpf_test_entry_error(Lpf_Kind kind, Lpf_Error error)
{
    Lpf_Test_Entry entry = {kind, "", "", "", "", error};
    return entry;
}

Lpf_Test_Entry lpf_test_entry(Lpf_Kind kind, const char* label, const char* type, const char* value, const char* comment)
{
    Lpf_Test_Entry entry = {kind, label, type, value, comment};
    return entry;
}

void lpf_test_string_eq(String expected, String obtained)
{
    if(string_is_equal(expected, obtained) == false)
    {
        file_write_entire(STRING("_lpf_test_failed_expected.txt"), expected);
        file_write_entire(STRING("_lpf_test_failed_obtained.txt"), obtained);
    }
    TEST_MSG(string_is_equal(expected, obtained),
            "expected: '\n"STRING_FMT"\n'\n"
            "obtained: '\n"STRING_FMT"\n'", 
            STRING_PRINT(expected),
            STRING_PRINT(obtained));
}

void lpf_test_lowlevel_read(const char* ctext, Lpf_Test_Entry test_entry)
{
    String text = string_make(ctext);
    Lpf_Entry entry = {0};
    isize finished_at = lpf_lowlevel_read_entry(text, 0, &entry);
    (void) finished_at;

    //TEST(finished_at == text.size);
    TEST(entry.error == test_entry.error);
    if(entry.error == LPF_ERROR_NONE)
    {
        TEST(string_is_equal(entry.label,   string_make(test_entry.label)));
        TEST(string_is_equal(entry.type,    string_make(test_entry.type)));
        TEST(string_is_equal(entry.value,   string_make(test_entry.value)));
        TEST(string_is_equal(entry.comment, string_make(test_entry.comment)));
    }
}

void lpf_test_write(Lpf_Format_Options options, Lpf_Test_Entry test_entry, const char* ctext, u16 flags)
{
    String_Builder into = {0};
    array_init_backed(&into, allocator_get_scratch(), 256);

    Lpf_Entry entry = {0};
    entry.label = string_make(test_entry.label);
    entry.type = string_make(test_entry.type);
    entry.value = string_make(test_entry.value);
    entry.comment = string_make(test_entry.comment);
    entry.kind = test_entry.kind;
    entry.format_flags = flags;

    Lpf_Writer writer = {0};

    lpf_write_entry(&writer, &into, entry, &options);

    String expected = string_make(ctext);
    String obtained = string_from_builder(into);
    lpf_test_string_eq(expected, obtained);
    array_deinit(&into);
}


void lpf_test_read_lowlevel_entry()
{
    //Okay values
    lpf_test_lowlevel_read(":hello world!",                  lpf_test_entry(LPF_KIND_ENTRY, "", "", "hello world!", ""));
    lpf_test_lowlevel_read("  ;hello world!#",               lpf_test_entry(LPF_KIND_ESCAPED_CONTINUATION, "", "", "hello world!", ""));
    lpf_test_lowlevel_read("  ,hello world!",                lpf_test_entry(LPF_KIND_CONTINUATION, "", "", "hello world!", ""));
    lpf_test_lowlevel_read("label:...value...\n814814\n",    lpf_test_entry(LPF_KIND_ENTRY, "label", "", "...value...", ""));
    lpf_test_lowlevel_read("label type:...value...#comment", lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "...value...", "comment"));

    lpf_test_lowlevel_read("#this is a texture declaration##\n", lpf_test_entry(LPF_KIND_COMMENT, "", "", "", "this is a texture declaration##"));
    lpf_test_lowlevel_read(" {   ",                          lpf_test_entry(LPF_KIND_SCOPE_START, "", "", "", ""));
    lpf_test_lowlevel_read(" map {   ",                      lpf_test_entry(LPF_KIND_SCOPE_START, "map", "", "", ""));
    lpf_test_lowlevel_read("texture   TEX { #some comment",  lpf_test_entry(LPF_KIND_SCOPE_START, "texture", "TEX", "", "some comment"));
    lpf_test_lowlevel_read(" }",                             lpf_test_entry(LPF_KIND_SCOPE_END, "", "", "", ""));
    lpf_test_lowlevel_read(" } #some comment",               lpf_test_entry(LPF_KIND_SCOPE_END, "", "", "", "some comment"));

    lpf_test_lowlevel_read("",                               lpf_test_entry(LPF_KIND_BLANK, "", "", "", ""));
    lpf_test_lowlevel_read("  \t \v \f",                     lpf_test_entry(LPF_KIND_BLANK, "", "", "", ""));
    
    //Errors 
    lpf_test_lowlevel_read("label ",                         lpf_test_entry_error(LPF_KIND_BLANK, LPF_ERROR_ENTRY_MISSING_START));
    lpf_test_lowlevel_read("label t",                        lpf_test_entry_error(LPF_KIND_BLANK, LPF_ERROR_ENTRY_MISSING_START));
    lpf_test_lowlevel_read("label t1 t2:",                   lpf_test_entry_error(LPF_KIND_ENTRY, LPF_ERROR_ENTRY_MULTIPLE_TYPES));
    lpf_test_lowlevel_read("label ,",                        lpf_test_entry_error(LPF_KIND_CONTINUATION, LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL));
    lpf_test_lowlevel_read("label t2 ,",                     lpf_test_entry_error(LPF_KIND_CONTINUATION, LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL));
    lpf_test_lowlevel_read("label t2 ;",                     lpf_test_entry_error(LPF_KIND_CONTINUATION, LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL));
    lpf_test_lowlevel_read("label t2 t3 ;",                  lpf_test_entry_error(LPF_KIND_CONTINUATION, LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL));

    lpf_test_lowlevel_read("texture TEX 12 { #some comment", lpf_test_entry_error(LPF_KIND_SCOPE_START, LPF_ERROR_SCOPE_MULTIPLE_TYPES));
    lpf_test_lowlevel_read("texture TEX { val ",             lpf_test_entry_error(LPF_KIND_SCOPE_START, LPF_ERROR_SCOPE_CONTENT_AFTER_START));
    lpf_test_lowlevel_read("} # #val ",                      lpf_test_entry_error(LPF_KIND_SCOPE_END, LPF_ERROR_SCOPE_CONTENT_AFTER_END));
    lpf_test_lowlevel_read(" some_label } #comment",         lpf_test_entry_error(LPF_KIND_SCOPE_END, LPF_ERROR_SCOPE_END_HAS_LABEL));
    lpf_test_lowlevel_read(" some_label a } #comment",       lpf_test_entry_error(LPF_KIND_SCOPE_END, LPF_ERROR_SCOPE_END_HAS_LABEL));
    lpf_test_lowlevel_read(" some_label a b c}",             lpf_test_entry_error(LPF_KIND_SCOPE_END, LPF_ERROR_SCOPE_END_HAS_LABEL));
}

void lpf_test_write_entry()
{
    #if 1
    Lpf_Format_Options def_options = {0};
    def_options.hash_escape = STRING(":hash:");

    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val", "comment"), 
        "label type:val#comment\n", 0);
    
    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val", ""), 
        "label type:val#\n", LPF_FLAG_WHITESPACE_SENSITIVE);

    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_ENTRY, "", "type", "val", "comment"), 
        "_ type:val#comment\n", 0);

    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_CONTINUATION, "label", "type", "valval", "comment with #"), 
        ",valval#comment with :hash:\n", 0);
    
    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_ESCAPED_CONTINUATION, "label", "type", "valval", "comment with # and \n   newline "), 
        ";valval#comment with :hash: and newline \n", 0);

    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_COMMENT, "label", "type", "val", "comment##"), 
        "#comment##\n", 0);
        
    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_SCOPE_START, "label", "type", "val", "comment"), 
        "label type{ #comment\n", 0);
        

    lpf_test_write(def_options, 
        lpf_test_entry(LPF_KIND_SCOPE_END, "label", "type", "val", "comment"), 
        "} #comment\n", 0);
        
    //Dont write should not write anything
    lpf_test_write(def_options, lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val", "comment"),                  "", LPF_FLAG_DONT_WRITE);
    lpf_test_write(def_options, lpf_test_entry(LPF_KIND_CONTINUATION, "label", "type", "val", "comment"),           "", LPF_FLAG_DONT_WRITE | LPF_FLAG_WHITESPACE_SENSITIVE);
    lpf_test_write(def_options, lpf_test_entry(LPF_KIND_ESCAPED_CONTINUATION, "label", "type", "val", "comment"),   "", LPF_FLAG_DONT_WRITE);
    lpf_test_write(def_options, lpf_test_entry(LPF_KIND_COMMENT, "label", "type", "val", "comment"),                "", LPF_FLAG_DONT_WRITE);
    lpf_test_write(def_options, lpf_test_entry(LPF_KIND_SCOPE_START, "label", "type", "val", "comment"),            "", LPF_FLAG_DONT_WRITE | LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC);
    lpf_test_write(def_options, lpf_test_entry(LPF_KIND_SCOPE_END, "label", "type", "val", "comment"),              "", LPF_FLAG_DONT_WRITE);

    {
        
        Lpf_Format_Options options = {0};
        options.line_indentation_offset = 3;
        options.pad_prefix_to = 5;
        lpf_test_write(options, 
            lpf_test_entry(LPF_KIND_CONTINUATION, "label", "type", "val", "comment"), 
            "        ,val #comment\n", LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC);
    }
    
    {
        
        lpf_test_write(def_options, 
            lpf_test_entry(LPF_KIND_ENTRY, "lab#:", "t pe", "  val  ", "comment"), 
            "lab tpe:val #comment\n", LPF_FLAG_WHITESPACE_AGNOSTIC);
    }

    {
        lpf_test_write(def_options, 
            lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val1\nval2\nval3", "comment"), 
            "label type:val1\n"
            ",val2\n"
            ",val3#comment\n", LPF_FLAG_NEWLINE_AGNOSTIC);
    }
    
    {
        lpf_test_write(def_options, 
            lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val1\nval2\nval3", "comment"), 
            "", LPF_FLAG_DONT_WRITE);
    }

    {
        
        Lpf_Format_Options options = {0};
        options.pad_continuations = true;
        options.line_indentation_offset = 3;
        options.hash_escape = STRING(":hash:");
        lpf_test_write(options, 
            lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val1\nval2\nval3", "comment#"), 
            "   label type:val1#\n"
            "             ,val2#\n"
            "             ,val3#comment:hash:\n", LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC);
    }
    {
        Lpf_Format_Options options = {0};
        options.pad_continuations = true;
        options.line_indentation_offset = 3;
        options.hash_escape = STRING(":###:");
        lpf_test_write(options, 
            lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val1\nval2\nval3", "comment#"), 
            "   label type:val1\n"
            "             ,val2\n"
            "             ,val3 #comment\n", LPF_FLAG_WHITESPACE_AGNOSTIC);
    }
    {
        
        Lpf_Format_Options options = {0};
        options.pad_continuations = true;
        options.line_indentation_offset = 3;
        options.max_value_size = 4;
        options.hash_escape = STRING(":hashtag:");
        lpf_test_write(options, 
            lpf_test_entry(LPF_KIND_ENTRY, "label", "type", "val1long\nval2\nval3long", "comment#"), 
            "   label type:val1#\n"
            "             ;long#\n"
            "             ,val2#\n"
            "             ,val3#\n"
            "             ;long#comment:hashtag:\n", LPF_FLAG_WHITESPACE_AGNOSTIC);
    }
   
    {
        
        Lpf_Format_Options options = {0};
        options.max_comment_size = 8;
        lpf_test_write(options, 
            lpf_test_entry(LPF_KIND_COMMENT, "label", "type", "val", "comment## with\nnewlines\nand long lines"), 
            "#comment#\n"
            "## with\n"
            "#newlines\n"
            "#and long\n"
            "# lines\n", 0);
    }
    
    {
        Lpf_Format_Options options = {0};
        options.put_space_before_marker = true;
        lpf_test_write(options, 
            lpf_test_entry(LPF_KIND_SCOPE_START, "", "", "val", ""), 
            "{#\n", LPF_FLAG_WHITESPACE_SENSITIVE);
    }

    #endif
}


void lpf_test_read_write()
{
    Lpf_Dyn_Entry read = {0};
    Lpf_Dyn_Entry read_meaningful = {0};

    String_Builder written = {0};

    String original = STRING(
        "#this is a texture!\n"
        "\n"
        "\n"
        "before i:256#\n"
        "texture TEX { #inline\n"
        "   offset 6f:0 0 0\n"
        "            ,1 1 1\n"
        "   \n"
        "   offset 6f:0 0 0\n"
        "   \n"
        "            ,1 1 1\n"
        "   inside i :256\n"
        "   scale :0 0 0#\n"
        "} #end comment \n"
        ",error continuation \n"
        "#hello after"
        );
    
    String expected_full = STRING(
        "#this is a texture!\n"
        "\n"
        "\n"
        "before i :256#\n"
        "texture TEX { #inline\n"
        "    offset 6f :0 0 0\n"
        "              ,1 1 1\n"
        "    \n"
        "    offset 6f :0 0 0\n"
        "    \n"
        "    ,1 1 1\n"
        "    inside i :256\n"
        "    scale :0 0 0#\n"
        "} #end comment \n"
        ",error continuation \n"
        "#hello after\n"
        );
        
    String expected_read_meaningful = STRING(
        "before i :256#\n"
        "texture TEX {\n"
        "    offset 6f :0 0 0\n"
        "              ,1 1 1\n"
        "    offset 6f :0 0 0\n"
        "    :1 1 1\n"
        "    inside i :256\n"
        "    scale :0 0 0#\n"
        "}\n"
        ":error continuation \n"
    );
    
    String expected_written_meaningful = STRING(
        "before i :256#\n"
        "texture TEX {\n"
        "    offset 6f :0 0 0\n"
        "              ,1 1 1\n"
        "    offset 6f :0 0 0\n"
        "    inside i :256\n"
        "    scale :0 0 0#\n"
        "}\n"
    );

    Lpf_Error read_error = lpf_read(original, &read);
    TEST(read_error == LPF_ERROR_ENTRY_CONTINUNATION_WITHOUT_START);

    Lpf_Error meaningful_error = lpf_read_meaningful(original, &read_meaningful);
    TEST(meaningful_error == LPF_ERROR_ENTRY_CONTINUNATION_WITHOUT_START);
    
    array_clear(&written);
    lpf_write(&written, read);
    lpf_test_string_eq(expected_full, string_from_builder(written));
    
    array_clear(&written);
    lpf_write(&written, read_meaningful);
    lpf_test_string_eq(expected_read_meaningful, string_from_builder(written));
    
    array_clear(&written);
    lpf_write_meaningful(&written, read);
    lpf_test_string_eq(expected_written_meaningful, string_from_builder(written));

    lpf_dyn_entry_deinit(&read);
    lpf_dyn_entry_deinit(&read_meaningful);
    array_deinit(&written);
}

#if 0
//@TODO: move into its own file and make less tied to engine
void test_serialize()
{
    Lpf_Dyn_Entry read = {0};
    Lpf_Dyn_Entry written = {0};

    Resource_Info info = {0};
    String deserialized = STRING(
        "my_info {\n"
            "id id :5413135443\n"
            "name s:  image_2 \n"
            "path s: image.png \n"
            "type_enum u : 1\n"
            "creation_etime i :0\n"
            "death_etime i :    0\n"
            "modified_etime i :0\n"
            "load_etime i :0\n"
            "file_modified_etime i :0\n"
            "lifetime :  RESOURCE_LIFETIME_REFERENCED   \n"
            "reload :  RESOURCE_RELOAD_ON_FILE_CHANGE   \n"
        "}\n"
    );
    lpf_read(deserialized, &read);
        
    bool okay = serialize_resource_info(serialize_locate(&read, "my_info", SERIALIZE_READ), &info, SERIALIZE_READ);
    TEST(okay);
        
    okay = serialize_resource_info(serialize_locate(&written, "texture", SERIALIZE_WRITE), &info, SERIALIZE_WRITE);
    TEST(okay);

    String_Builder serialized = {0};
    lpf_write(&serialized, written);
    LOG_INFO("LPF", STRING_FMT, STRING_PRINT(serialized));

    lpf_dyn_entry_deinit(&read);
    lpf_dyn_entry_deinit(&written);
    array_deinit(&serialized);
}
#endif

void test_format_lpf()
{
    lpf_test_write_entry();
    lpf_test_read_lowlevel_entry();
    lpf_test_read_write();
    //test_serialize();
}