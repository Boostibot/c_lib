#ifndef JOT_FORMAT_LPF
#define JOT_FORMAT_LPF

// This is a custom JSON-like format spec and implementation.
// 
// The main idea is to have each line start with a 'prefix' containing some 
// meta data used for parsing (labels, type, structure) hance the name
// LPF - Line Prefix Format. 
// 
// The benefit is that values require minimal escaping since the only* value 
// we need to escape is newline. This in turn allows for tremendous variety 
// for formats and thus types the user can fill in. We use this to give the 
// user ability to define custom types. We also outline some default types
// however these play no role in the parisn.
// 
// The LPF structure also simplifies parsing because each line is lexically 
// (and almost semantically) unqiue. This is also allows for trivial paralel 
// implementation where we could simply start parsing the file from N different 
// points in paralel and then simply join the results together to obtain a 
// valid parsed file.
// 
// The LPF idea can be implemented in variety of ways, this being just one of them.
// 
// * Partially. Read below above inline comments.
// 
// The final format looks like the following:
//
// #A sample material declarion in the LPF format
// material {
//     name       :Wood   
//     reosultion :1024
//     albedo     :1 1 1
//     roughness  :0.59 #reduced roughness
//     metallic   :0
//     ao         :0
//     emissive   :0
//     mra        :0 0 0
// 
//     #this is a long comment
//     #with multiple lines
//     albedo_map TEX {
//         path s      :images/wood_albedo.bmp
//         tile b      :false
//         gamma f     :2.2
//         gain f      :1
//         bias f      :0
//         offset 3f   :0 0 0 
//         scale 3f    :1 1 1 
//     }
//     
//     roughness_map TEX { 
//         path s  :images/wood_roughness.bmp
//     }
// }
//
// formally there are 7 kinds of 'entries' in the LPF format.
// Each entry is terminated in newline. The structure of each 
// is indicated below:
// 
//      LPF_KIND_BLANK:                  ( )\n
//      
//      LPF_KIND_COMMENT:                ( )#(comment)\n
//      
//      LPF_KIND_ENTRY:                  ( ):(value)\n
//      LPF_KIND_ENTRY:                  ( )[label]( ):(value)\n
//      LPF_KIND_ENTRY:                  ( )[label][ ][type]( ):(value)\n
//      
//      LPF_KIND_CONTINUATION:           ( ),(value)\n    
//      
//      LPF_KIND_ESCAPED_CONTINUATION:   ( );(value)\n     
//      
//      LPF_KIND_SCOPE_START:            ( ){( )\n    
//      LPF_KIND_SCOPE_START:            ( )[label]( ){( )\n     
//      LPF_KIND_SCOPE_START:            ( )[label][ ][type]{( )\n    
//      
//      LPF_KIND_SCOPE_END:              ( )}( )\n
//
// where () means optional and [] means obligatory
// specifically ( ), [ ] means whitespace.
// (label) and (type) may contain any character except '#', ':', ',', ';', '{', '}' and whitespace.
// 
// LPF_KIND_ENTRY is the default value type inside LPF file. It represent arbitrary data value.
// 
// Since the value contained within does not contain newlines (by nature of the format) we need some
// way of encoding multiline values. This is what LPF_KIND_CONTINUATION does. It simply continues
// the value from the LPF_KIND_ENTRY, LPF_KIND_CONTINUATION or LPF_KIND_ESCAPED_CONTINUATION 
// directly preceeding it (else error) with added newline. Thus:
// 
//      label type:Value
//                ,Continuation
// 
//      -- value gets parsed as --> 
// 
//      "Value\nContinuation"
// 
// LPF_KIND_ESCAPED_CONTINUATION works just like LPF_KIND_CONTINUATION except does not append the newline
// character at the end of the value. This is mainly used to keep lines short and readable. Thus:
// 
//      label type:Value
//                ;Escaped continuation
// 
//      -- value gets parsed as --> 
// 
//      "ValueEscaped continuation"
// 
// Every entry can be followed by arbitrary long string of LPF_KIND_CONTINUATION and LPF_KIND_ESCAPED_CONTINUATION.
// 
// LPF_KIND_ENTRY, LPF_KIND_CONTINUATION, LPF_KIND_ESCAPED_CONTINUATION and LPF_KIND_SCOPE_START
// can be terminated with "inline comment". It works by treating everything past the FURTHEST 
// # symbol as comment. This means we can still have # inside values, we just need to place additional
// # before the line end. Contrary to their name these comments shouldnt most of the time be used for
// commenting as they require a lot of escaping (newlines need not be present).
// These comments mainly serves humans while reading the file in visualising line endings.
// Newline is normally rendered as whitespace and thus if we want to store for example "hi!  " string, its
// very difficult for humans to edit. Thus we would output this as:
// greetings :hi!  #
// 
// All values between a pair of LPF_KIND_SCOPE_START and LPF_KIND_SCOPE_END are treated as children of the 
// LPF_KIND_SCOPE_START entry. When there is no LPF_KIND_SCOPE_START parent the entries are children of 
// global root.
// 
// The following types are the default:
//      
//      #Arbitrary string. This signals that we dont know the type of the value. 
//      #This is the type of all untyped entries
//      _ any :value#
// 
//      #Arbitrary string. This value should be read as is and most of the time be 
//      #comment terminated. The value might be direct binary representation and thus
//      #contain null and other special characters.
//      _ raw :value#
// 
//      #Arbitrary string. Prefix and Postfix whitespace is ignored.
//      _ s: String entry  
// 
//      # ======= All types below may have leading whitespace which is ignored ========
// 
//      #Base 64 binary data in the base64url format (url and filename safe format).
//      _ base64: SGVsbG8=
// 
//      #single C langauge (and most other langauges) id containing no whitespace.
//      _ n: my_name 
// 
//      #Unsigned number. Format is (leading zeroes)[digits]
//      _ u: 512    
// 
//      #Signed number. Format is (-)(leading zeroes)[digits]
//      _ i: -1024      
// 
//      #Floating point number. Format is (-)(leading zeroes)[digits](.digits). 
//      #  Note that: only . is permitted as fraction separator, scientific notation is dissallowed, and leading + must not be present
//      _ f: 13.2
// 
//      #Bool value. Values can be true, false, 1, 0
//      _ b: true
//      _ b: false
//      _ b: 1
//      _ b: 0
// 
//      #Utf8 representation of single unicode codepoint
//      _ c: g
//
//      #Null entry. Its value is ignored.
//      _ null:ignored
//
// All of the default types expcept 'any', 'raw' and 's' may be prefix with a decimal number to indicate a whitespace separated array.
// So for example we could use 3f to indicate a vector:
//      offset 3f   :1.4 0 0
// 
//      matrix 16f  :1 0 0 0 
//                  ,0 1 0 0
//                  ,0 0 1 0
//                  ,0 0 0 1
// 
// These types should be thought of more as static arrays in C than as a dynamic resizable array substitute since they really are types
// and we could want to for example search for all vctors (3f) in a file. We woudlnt want our by hazard 3 element float array to show up.

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>

#include "string.h"
#include "parse.h"
#include "assert.h"
#include "vformat.h"

#ifdef PERF_COUNTER_START
    #define LPF_PERF_COUNTER_START(x) PERF_COUNTER_START(x)
    #define LPF_PERF_COUNTER_END(x) PERF_COUNTER_END(x)
#else
    #define LPF_PERF_COUNTER_START(x) int x = 0
    #define LPF_PERF_COUNTER_END(x) (void) x
#endif

typedef enum Lpf_Kind {
    LPF_KIND_BLANK = 0,
    LPF_KIND_ENTRY = ':',
    LPF_KIND_CONTINUATION = ',',
    LPF_KIND_ESCAPED_CONTINUATION = ';',
    LPF_KIND_COMMENT = '#',
    LPF_KIND_SCOPE_START = '{',
    LPF_KIND_SCOPE_END = '}',
} Lpf_Kind;

typedef enum Lpf_Error {
    LPF_ERROR_NONE = 0,
    LPF_ERROR_ENTRY_INVALID_CHAR_BEFORE_START,

    LPF_ERROR_ENTRY_MISSING_START,
    LPF_ERROR_ENTRY_MULTIPLE_TYPES,
    LPF_ERROR_ENTRY_CONTINUNATION_WITHOUT_START,
    LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL,
    
    LPF_ERROR_SCOPE_END_HAS_LABEL,
    LPF_ERROR_SCOPE_MULTIPLE_TYPES,
    LPF_ERROR_SCOPE_CONTENT_AFTER_START,
    LPF_ERROR_SCOPE_CONTENT_AFTER_END,
    LPF_ERROR_SCOPE_TOO_MANY_ENDS,
} Lpf_Error;

enum {
    LPF_FLAG_WHITESPACE_SENSITIVE = 1,  //All whitespace matters
    LPF_FLAG_WHITESPACE_PREFIX_AGNOSTIC = 2, //prefix whitespace including newlines does not matter
    LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC = 4, //postfix whitespace including newlines does not matter. Allows space betweem value and comment
    LPF_FLAG_NEWLINE_AGNOSTIC = 8, //newlines are treated as whitespace (dont need escaping)
    LPF_FLAG_WHITESPACE_AGNOSTIC = LPF_FLAG_NEWLINE_AGNOSTIC | LPF_FLAG_WHITESPACE_PREFIX_AGNOSTIC | LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC, //whitespace and newlines dont matter (as long as there is at least one)
    LPF_FLAG_ALIGN_MEMBERS = 32, //only apllicable to scopes. Sets pad_prefix_to to the longest prefix in direct children of the scope.
    LPF_FLAG_DONT_WRITE = 16, //Entries with this flag are not written at all.
};

typedef struct Lpf_Dyn_Entry {
    char  kind;
    i8  error;
    u16 format_flags;
    u32 depth;
    i64 line_number;

    Allocator* allocator;
    char* text_parts;

    isize comment_size;
    isize label_size;
    isize type_size;
    isize value_size;

    struct Lpf_Dyn_Entry* children;
    u32 children_size;
    u32 children_capacity;
} Lpf_Dyn_Entry;

typedef struct Lpf_Entry {
    Lpf_Kind kind;
    Lpf_Error error;
    u16 format_flags;
    isize line_number;
    isize depth;

    String label;
    String type;
    String value;
    String comment;

    Lpf_Dyn_Entry* children;
    isize children_size;
    isize children_capacity;
} Lpf_Entry;

typedef struct Lpf_Format_Options {
    isize max_value_size;
    isize max_comment_size;
    i32 pad_prefix_to;

    i32 line_identation_per_level;
    i32 comment_identation_per_level;

    i32 line_indentation_offset;
    i32 comment_indentation_offset;

    String hash_escape;
    bool pad_continuations;
    bool put_space_before_marker;

    bool skip_comments;
    bool skip_inline_comments;
    bool skip_blanks;
    bool skip_connecting_blanks;
    bool skip_scopes;
    bool skip_scope_ends;
    bool skip_types;
    bool skip_errors;
    bool log_errors;

    bool correct_errors;
    bool stop_on_error;
} Lpf_Format_Options;

typedef struct Lpf_Writer {
    isize depth;
    isize line_number;
} Lpf_Writer;

typedef struct Lpf_Reader {
    bool            had_continuation;
    bool            has_last_entry;
    Lpf_Entry       last_entry;
    String_Builder  last_value;
    String_Builder  last_comment;

    ptr_Array scopes;
    isize depth;
    isize line_number;
} Lpf_Reader;

typedef struct Lpf_Write_Options {
    isize line_indentation;
    isize comment_indentation;
    
    isize pad_prefix_to;

    bool put_space_before_marker;
    bool put_space_before_value;
    bool put_space_before_comment;
    bool comment_terminate_value;
} Lpf_Write_Options;

EXPORT Lpf_Error           lpf_read(String source, Lpf_Dyn_Entry* root);               //reads the complete structure of the file including errors, comments, blanks and inline comments
EXPORT Lpf_Error           lpf_read_meaningful(String source, Lpf_Dyn_Entry* root);    //reads just the meaningful information from the file. Attempts to correct errors. If the errors cannot be corrected discards the ernous entries

EXPORT void                lpf_write(String_Builder* builder, Lpf_Dyn_Entry root);     //writes the complete structure of the file including errors, comments, blanks and inline comments
EXPORT void                lpf_write_meaningful(String_Builder* builder, Lpf_Dyn_Entry root); //writes just entries and scopes withut inline comments

EXPORT void                lpf_write_entry(Lpf_Writer* writer, String_Builder* into, Lpf_Entry entry, const Lpf_Format_Options* format);
EXPORT Lpf_Error           lpf_read_entry(Lpf_Reader* reader, Lpf_Dyn_Entry* into, Lpf_Entry entry, const Lpf_Format_Options* options);

EXPORT isize               lpf_lowlevel_read_entry(String source, isize from, Lpf_Entry* parsed);
EXPORT isize               lpf_lowlevel_write_entry_unescaped(String_Builder* source, Lpf_Entry entry, Lpf_Write_Options options);

EXPORT const char*         lpf_error_to_string(Lpf_Error error);
EXPORT Lpf_Format_Options  lpf_make_default_format_options();

EXPORT void                lpf_dyn_entry_from_entry(Lpf_Dyn_Entry* dyn, Lpf_Entry entry);
EXPORT Lpf_Entry           lpf_entry_from_dyn_entry(Lpf_Dyn_Entry dyn);

EXPORT void                lpf_reader_deinit(Lpf_Reader* reader);
EXPORT void                lpf_reader_reset(Lpf_Reader* reader);
EXPORT void                lpf_reader_commit_entries(Lpf_Reader* reader);
EXPORT void                lpf_reader_queue_entry(Lpf_Reader* reader, Lpf_Entry entry, const Lpf_Format_Options* options);

EXPORT void                lpf_dyn_entry_set_text_capacity_and_data(Lpf_Dyn_Entry* dyn, isize label_size, const char* label_or_null, isize type_size, const char* type_or_null, isize comment_size, const char* comment_or_null, isize value_size, const char* value_or_null);
EXPORT void                lpf_dyn_entry_set_text_capacity(Lpf_Dyn_Entry* dyn, isize label_size, isize type_size, isize comment_size, isize value_size);
EXPORT void                lpf_dyn_entry_deinit(Lpf_Dyn_Entry* dyn);
EXPORT void                lpf_dyn_entry_push_dyn(Lpf_Dyn_Entry* dyn, Lpf_Dyn_Entry pushed);
EXPORT void                lpf_dyn_entry_push(Lpf_Dyn_Entry* dyn, Lpf_Entry pushed);
EXPORT void                lpf_dyn_entry_map(Lpf_Dyn_Entry* dyn, void(*preorder_func)(Lpf_Dyn_Entry* dyn, void* context), void(*postorder_func)(Lpf_Dyn_Entry* dyn, void* context), void* context);

EXPORT Lpf_Error           lpf_read_custom(String source, Lpf_Dyn_Entry* root, const Lpf_Format_Options* options);
EXPORT void                lpf_write_custom(String_Builder* source, Lpf_Dyn_Entry root, const Lpf_Format_Options* options);

EXPORT Lpf_Dyn_Entry*      lpf_find(Lpf_Dyn_Entry in_children_of, Lpf_Kind kind, const char* label, const char* type);
EXPORT isize               lpf_find_index(Lpf_Dyn_Entry in_children_of, Lpf_Kind kind, String label, String type, isize from);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FORMAT_LPF_IMPL)) && !defined(JOT_FORMAT_LPF_HAS_IMPL)
#define JOT_FORMAT_LPF_HAS_IMPL

EXPORT Lpf_Format_Options lpf_make_default_format_options()
{
    Lpf_Format_Options options = {0};
    options.max_value_size = 200;
    options.max_comment_size = 200;
    options.line_identation_per_level = 4;
    options.comment_identation_per_level = 2;
    options.pad_continuations = true;
    options.put_space_before_marker = true;
    options.hash_escape = STRING(":hashtag:");

    return options;
}

EXPORT const char* lpf_error_to_string(Lpf_Error error)
{
    switch(error)
    {
        default:
        case LPF_ERROR_NONE: return "LPF_ERROR_NONE";
        case LPF_ERROR_ENTRY_INVALID_CHAR_BEFORE_START: return "LPF_ERROR_ENTRY_INVALID_CHAR_BEFORE_START";

        case LPF_ERROR_ENTRY_MISSING_START: return "LPF_ERROR_ENTRY_MISSING_START";
        case LPF_ERROR_ENTRY_MULTIPLE_TYPES: return "LPF_ERROR_ENTRY_MULTIPLE_TYPES";
        case LPF_ERROR_ENTRY_CONTINUNATION_WITHOUT_START: return "LPF_ERROR_ENTRY_CONTINUNATION_WITHOUT_START";
        case LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL: return "LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL";
    
        case LPF_ERROR_SCOPE_END_HAS_LABEL: return "LPF_ERROR_SCOPE_END_HAS_LABEL";
        case LPF_ERROR_SCOPE_CONTENT_AFTER_START: return "LPF_ERROR_SCOPE_CONTENT_AFTER_START";
        case LPF_ERROR_SCOPE_CONTENT_AFTER_END: return "LPF_ERROR_SCOPE_CONTENT_AFTER_END";
        case LPF_ERROR_SCOPE_TOO_MANY_ENDS: return "LPF_ERROR_SCOPE_TOO_MANY_ENDS";
        case LPF_ERROR_SCOPE_MULTIPLE_TYPES: return "LPF_ERROR_SCOPE_MULTIPLE_TYPES";
    }
}

INTERNAL isize _lpf_parse_inline_comment(String source, isize line_size, String* comment)
{
    isize value_to = line_size;
    isize tag_pos = string_find_last_char_from(source, '#', line_size - 1);
    if(tag_pos != -1)
    {
        value_to = tag_pos;
        *comment = string_range(source, tag_pos + 1, line_size);
    }
    else
    {
        String null = {0};
        *comment = null;
    }

    return value_to;
}

EXPORT isize lpf_lowlevel_read_entry(String source_, isize from, Lpf_Entry* parsed)
{
    String source = string_tail(source_, from);

    isize source_i = 0;
    Lpf_Entry entry = {LPF_KIND_BLANK};
    struct {
        isize from; 
        isize to;
    } word_range = {0};

    //parse line start
    bool had_non_space = false;
    bool is_within_type = false;

    //@TODO: do the comment parsing marker parsing and comment parsing all here within a single loop
    // then only do the prefix words if it is desired and it exists
    isize line_size = string_find_first_char(source, '\n', 0);
    if(line_size == -1)
        line_size = source.size;
    
    isize line_end = MIN(line_size + 1, source.size);

    enum {MAX_TYPES = 2};

    isize had_types = 0;
    String types[MAX_TYPES] = {0};

    for(; source_i < line_size; source_i++)
    {
        char c = source.data[source_i];

        switch(c)
        {
            case ':': entry.kind = LPF_KIND_ENTRY; break;
            case ';': entry.kind = LPF_KIND_ESCAPED_CONTINUATION;  break;
            case ',': entry.kind = LPF_KIND_CONTINUATION;  break;
            case '#': entry.kind = LPF_KIND_COMMENT;  break;
            case '{': entry.kind = LPF_KIND_SCOPE_START; break;
            case '}': entry.kind = LPF_KIND_SCOPE_END; break;
            default: {
                if(char_is_space(c) == false)
                {
                    had_non_space = true;
                    if(is_within_type == false)
                    {
                        is_within_type = true;
                        word_range.from = source_i;
                    }
                }
                else
                {
                    if(is_within_type)
                    {
                        is_within_type = false;
                        word_range.to = source_i;
                    }
                }
            } break;
        }

        if((entry.kind != LPF_KIND_BLANK && is_within_type) || word_range.to != 0)
        {
            word_range.to = source_i;

            if(had_types < 2)
                types[had_types] = string_range(source, word_range.from, word_range.to);
            
            had_types += 1;
            word_range.to = 0;
        }

        if(entry.kind != LPF_KIND_BLANK)
            break;
    }
        
    //if was a blank line skip
    if(entry.kind == LPF_KIND_BLANK && had_non_space == false)
    {
        entry.kind = LPF_KIND_BLANK;
        goto line_end;
    }

    source_i += 1;
    switch(entry.kind)
    {
        case LPF_KIND_BLANK: {
            //Missing start
            entry.error = LPF_ERROR_ENTRY_MISSING_START;
            goto line_end;
        }
        case LPF_KIND_ENTRY:
        case LPF_KIND_CONTINUATION:
        case LPF_KIND_ESCAPED_CONTINUATION: {
            if(entry.kind == LPF_KIND_CONTINUATION || entry.kind == LPF_KIND_ESCAPED_CONTINUATION)
            {
                if(had_types > 0)
                {
                    entry.error = LPF_ERROR_ENTRY_CONTINUNATION_HAS_LABEL;
                    goto line_end;
                }
            }
            else
            {
                if(had_types > 2)
                {
                    entry.error = LPF_ERROR_ENTRY_MULTIPLE_TYPES;
                    goto line_end;
                }

                entry.label = types[0];
                entry.type = types[1];
            }

            isize value_from = source_i;
            isize value_to = _lpf_parse_inline_comment(source, line_size, &entry.comment);
            entry.value = string_range(source, value_from, value_to);

            if(entry.comment.data != NULL)
                entry.format_flags |= LPF_FLAG_WHITESPACE_SENSITIVE;
        }
        break;

        case LPF_KIND_COMMENT: {
            if(had_types > 0)
            {
                entry.error = LPF_ERROR_SCOPE_END_HAS_LABEL;
                goto line_end;
            }
                
            entry.comment = string_range(source, source_i, line_size);
        }
        break;

        case LPF_KIND_SCOPE_START: 
        case LPF_KIND_SCOPE_END: {
            if(entry.kind == LPF_KIND_SCOPE_END)
            {
                if(had_types > 0)
                {
                    entry.error = LPF_ERROR_SCOPE_END_HAS_LABEL;
                    goto line_end;
                }
            }
            else
            {
                if(had_types > 2)
                {
                    entry.error = LPF_ERROR_SCOPE_MULTIPLE_TYPES;
                    goto line_end;
                }

                entry.label = types[0];
                entry.type = types[1];
            }
            
            isize value_from = source_i;
            isize value_to = _lpf_parse_inline_comment(source, line_size, &entry.comment);
            String value = string_range(source, value_from, value_to);

            //Value needs to be whitespace only.
            String non_white = string_trim_prefix_whitespace(value);
            if(non_white.size > 0)
            {
                if(entry.kind == LPF_KIND_SCOPE_START)
                    entry.error = LPF_ERROR_SCOPE_CONTENT_AFTER_START;
                else
                    entry.error = LPF_ERROR_SCOPE_CONTENT_AFTER_END;
                goto line_end;
            }
        }
        break;
            
        default: ASSERT_MSG(false, "unreachable!");
    }

    
    line_end:

    *parsed = entry;
    return from + line_end;
}

EXPORT void lpf_builder_pad_to(String_Builder* builder, isize to_size, char with)
{
    if(builder->size >= to_size)
        return;

    isize size_before = array_resize(builder, to_size);
    memset(builder->data + size_before, with, builder->size - size_before);
}

INTERNAL bool _lpf_is_prefix_allowed_char(char c)
{
    if(char_is_space(c))
        return false;

    if(c == ':' || c == ',' || c == '#' ||c == ';' || c == '{' || c == '}')
        return false;

    return true;
}

EXPORT isize lpf_lowlevel_write_entry_unescaped(String_Builder* builder, Lpf_Entry entry, Lpf_Write_Options options)
{
    LPF_PERF_COUNTER_START(c);
    
    LPF_PERF_COUNTER_START(realloc_and_logic_c);
    
    #if defined(DO_ASSERTS_SLOW)
        for(isize i = 0; i < entry.label.size; i++)
            ASSERT_SLOW_MSG(_lpf_is_prefix_allowed_char(entry.label.data[i]), 
                "label must contain only valid data! Label: " STRING_FMT, STRING_PRINT(entry.label));

        for(isize i = 0; i < entry.type.size; i++)
            ASSERT_SLOW_MSG(_lpf_is_prefix_allowed_char(entry.type.data[i]),
                "type must contain only valid data! Label: " STRING_FMT, STRING_PRINT(entry.type));

        isize newline_pos = string_find_first_char(entry.value, '\n', 0);
        isize tag_pos = string_find_first_char(entry.value, '#', 0);
        isize comment_newline_pos = string_find_first_char(entry.comment, '\n', 0);
        isize comment_tag_pos = string_find_first_char(entry.comment, '#', 0);

        ASSERT_SLOW_MSG(newline_pos == -1, "value must not contain newlines. Value: \"" STRING_FMT "\"", STRING_PRINT(entry.value));
        ASSERT_SLOW_MSG(comment_newline_pos == -1, "comment must not contain newlines. Value: \"" STRING_FMT "\"", STRING_PRINT(entry.comment));
        if(tag_pos != -1)
            ASSERT_SLOW_MSG(options.comment_terminate_value, "If the value contains # it must be comment terminated!");

        if(entry.kind != LPF_KIND_COMMENT)
            ASSERT_SLOW_MSG(comment_tag_pos == -1, "comment must not contain #. Value: \"" STRING_FMT "\"", STRING_PRINT(entry.comment));
    #endif

    char marker_char = '?';
    isize prefix_size = 0;
    switch(entry.kind)
    {
        default:
        case LPF_KIND_BLANK: {
            lpf_builder_pad_to(builder, builder->size + options.line_indentation, ' ');
            array_push(builder, '\n');

        } break;
        case LPF_KIND_COMMENT: {
            lpf_builder_pad_to(builder, builder->size + options.line_indentation, ' ');
            array_push(builder, '#');
            lpf_builder_pad_to(builder, builder->size + options.comment_indentation, ' ');
            builder_append(builder, entry.comment);
            array_push(builder, '\n');
        } break;

        case LPF_KIND_ENTRY:                marker_char = ':'; break;
        case LPF_KIND_CONTINUATION:         marker_char = ','; break;
        case LPF_KIND_ESCAPED_CONTINUATION: marker_char = ';'; break;
        case LPF_KIND_SCOPE_START:     marker_char = '{'; break;
        case LPF_KIND_SCOPE_END:       marker_char = '}'; break;
    }
    LPF_PERF_COUNTER_END(realloc_and_logic_c);

    if(marker_char != '?')
    {
        LPF_PERF_COUNTER_START(prefix_c);
        lpf_builder_pad_to(builder, builder->size + options.line_indentation, ' ');

        isize size_before = builder->size;
        builder_append(builder, entry.label);
        if(entry.type.size > 0)
        {
            array_push(builder, ' ');
            builder_append(builder, entry.type);
        }

        lpf_builder_pad_to(builder, size_before + options.pad_prefix_to, ' ');
        prefix_size = builder->size - size_before;

        if(prefix_size != 0 && options.put_space_before_marker)
            array_push(builder, ' ');

        array_push(builder, marker_char);
    
        LPF_PERF_COUNTER_END(prefix_c);
        
        LPF_PERF_COUNTER_START(value_and_comment_c);
        builder_append(builder, entry.value);
        if(entry.comment.size > 0)
        {
            if(options.put_space_before_comment && options.comment_terminate_value == false)
                array_push(builder, ' ');

            array_push(builder, '#');
            lpf_builder_pad_to(builder, builder->size + options.comment_indentation, ' ');
            builder_append(builder, entry.comment);
        }
        else if(options.comment_terminate_value)
            array_push(builder, '#');
    
        array_push(builder, '\n');
        LPF_PERF_COUNTER_END(value_and_comment_c);
    }
    
    LPF_PERF_COUNTER_END(c);
    return prefix_size;
}

INTERNAL String _lpf_escape_label_or_type(String_Builder* into, String label_or_line)
{
    LPF_PERF_COUNTER_START(counter);
    for(isize i = 0; i < label_or_line.size; i++)
    {
        char c = label_or_line.data[i];
        if(_lpf_is_prefix_allowed_char(c))
            array_push(into, c);
    }

    String out = string_from_builder(*into);
    LPF_PERF_COUNTER_END(counter);

    return out;
}

typedef struct _Lpf_Segment {
    Lpf_Kind kind;
    String string;
} _Lpf_Segment;

DEFINE_ARRAY_TYPE(_Lpf_Segment, _Lpf_Segment_Array);

INTERNAL bool _lpf_split_into_segments(_Lpf_Segment_Array* segemnts, String value, isize max_size)
{
    LPF_PERF_COUNTER_START(c);

    if(max_size <= 0)
        max_size = INT64_MAX;

    bool had_too_log = false;
    Line_Iterator it = {0};
    for(; line_iterator_get_line(&it, value); )
    {
        String line = it.line;
        Lpf_Kind kind = LPF_KIND_CONTINUATION;
        while(line.size > max_size)
        {
            had_too_log = true;
            _Lpf_Segment segment = {LPF_KIND_BLANK};
            segment.kind = kind;
            segment.string = string_head(line, max_size);
            
            array_push(segemnts, segment);
            line = string_tail(line, max_size);
            kind = LPF_KIND_ESCAPED_CONTINUATION;
        }
        
        _Lpf_Segment last_segemnt = {LPF_KIND_BLANK};
        last_segemnt.kind = kind;
        last_segemnt.string = line;
        array_push(segemnts, last_segemnt);
    }

    LPF_PERF_COUNTER_END(c);
    return had_too_log;
}

INTERNAL isize _lpf_calculate_prefix_size(isize label_size, isize type_size)
{
    isize prefix = label_size;
    if(type_size > 0)
    {
        //If has type the label must be present
        //thus if isnt is escaped to _
        prefix = MAX(label_size, 1) + 1 + type_size;
    }

    return prefix;
}

EXPORT void lpf_write_entry(Lpf_Writer* writer, String_Builder* builder, Lpf_Entry entry, const Lpf_Format_Options* format)
{
    Lpf_Kind kind = entry.kind;
    String label = {0};
    String type = {0};
    String value = {0};
    String comment = {0};
    String null_string = {0};
    
    Lpf_Write_Options options = {0};
    options.line_indentation = format->line_identation_per_level*writer->depth + format->line_indentation_offset;
    options.pad_prefix_to = format->pad_prefix_to;
    options.put_space_before_marker = format->put_space_before_marker;
    options.put_space_before_comment = !!(entry.format_flags & LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC);
    options.comment_terminate_value = !!(entry.format_flags & LPF_FLAG_WHITESPACE_SENSITIVE);

    if(format->skip_errors && entry.error != LPF_ERROR_NONE)
        return;
    if(entry.format_flags & LPF_FLAG_DONT_WRITE)
        return;
    
    switch(kind)
    {
        default:
        case LPF_KIND_BLANK: {
            if(format->skip_blanks)
                return;
            lpf_builder_pad_to(builder, builder->size + options.line_indentation, ' ');
            array_push(builder, '\n');

            return;
        } break;

        case LPF_KIND_COMMENT: {
            if(format->skip_comments)
                return;

            comment = entry.comment;
        } break;

        case LPF_KIND_ENTRY: {
            label = entry.label;
            type = entry.type;
            value = entry.value;
            comment = entry.comment;
        }break;
        case LPF_KIND_CONTINUATION:        
        case LPF_KIND_ESCAPED_CONTINUATION: {
            value = entry.value;
            comment = entry.comment;
        } break;

        case LPF_KIND_SCOPE_START: {
            if(format->skip_scopes)
                return;

            label = entry.label;
            type = entry.type;
            comment = entry.comment;
        } break;

        case LPF_KIND_SCOPE_END: {
            if(format->skip_scopes)
                return;

            comment = entry.comment;
        } break;
    }
    
    LPF_PERF_COUNTER_START(c);
    
    LPF_PERF_COUNTER_START(preparation_c);
    if(format->skip_types)
        type = null_string;

    if(format->skip_inline_comments && kind != LPF_KIND_COMMENT)
        comment = null_string;

    enum {LOCAL_BUFF = 256, SEGMENTS = 32, LINE_EXTRA = 5};
    _Lpf_Segment_Array value_segments = {0};
    _Lpf_Segment_Array comment_segments = {0};
    String_Builder escaped_inline_comment = {0};
    String_Builder escaped_label_builder = {0};
    String_Builder escaped_type_builder = {0};

    Allocator* scratch = allocator_get_scratch();

    array_init_backed(&value_segments, scratch, SEGMENTS);
    array_init_backed(&comment_segments, scratch, SEGMENTS);
    array_init_backed(&escaped_inline_comment, scratch, LOCAL_BUFF);
    array_init_backed(&escaped_label_builder, scratch, LOCAL_BUFF);
    array_init_backed(&escaped_type_builder, scratch, LOCAL_BUFF);

    //Escape label
    if(label.size > 0)
        label = _lpf_escape_label_or_type(&escaped_label_builder, label);
    
    //Escape type
    if(type.size > 0)
    {
        if(label.size == 0)
            label = STRING("_");

        type = _lpf_escape_label_or_type(&escaped_type_builder, type);
    }
    
    if(value.size > 0)
    {
        if(entry.format_flags & LPF_FLAG_WHITESPACE_PREFIX_AGNOSTIC)
            value = string_trim_prefix_whitespace(value); 
                    
        if(entry.format_flags & LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC)
            value = string_trim_postfix_whitespace(value);
    }
    LPF_PERF_COUNTER_END(preparation_c);

    //Escape the inline comment:
    // "  an inline comment \n"
    // "  with # and lots of space  "
    // ->
    // "  an inline comment with :hashtag: and lots of space  "
    if(comment.size > 0 && kind != LPF_KIND_COMMENT)
    {
        String escape = {0};
        if(string_find_first_char(format->hash_escape, '#', 0) == -1)
            escape = format->hash_escape;

        isize last_size = escaped_inline_comment.size;
        for(Line_Iterator line_it = {0}; line_iterator_get_line(&line_it, comment); )
        {
            String line = line_it.line;
            if(line_it.line_number != 1)
                line = string_trim_prefix_whitespace(line);

            String escaped_so_far = string_from_builder(escaped_inline_comment);
            escaped_so_far = string_trim_postfix_whitespace(escaped_so_far);
            if(escaped_so_far.size != escaped_inline_comment.size)
                array_resize(&escaped_inline_comment, escaped_so_far.size);

            if(last_size != escaped_inline_comment.size)
                array_push(&escaped_inline_comment, ' ');
            
            last_size = escaped_inline_comment.size;
            for(isize i = 0; i <= line.size; i++)
            {
                isize next_i = string_find_first_char(line, '#', i);
                if(next_i == -1)
                    next_i = line.size;
                
                if(i != 0)
                    builder_append(&escaped_inline_comment, escape);

                String current_range = string_range(line, i, next_i);
                builder_append(&escaped_inline_comment, current_range);
                i = next_i;
            }
        }

        comment = string_from_builder(escaped_inline_comment);
    }

    //Writers scopes normally:
    // label type { #comment
    //      #increased indentation!   
    // } #comment
    if(kind == LPF_KIND_SCOPE_END || kind == LPF_KIND_SCOPE_START)
    {

        if(kind == LPF_KIND_SCOPE_END)
        {
            options.line_indentation -= format->line_identation_per_level;
            writer->depth = MAX(writer->depth - 1, 0);
        }
        
        if(kind == LPF_KIND_SCOPE_START)
        {
            writer->depth += 1;
        }

        Lpf_Entry continuation = {kind};
        continuation.type = type;
        continuation.label = label;
        continuation.comment = comment;

        options.put_space_before_comment = true;
        lpf_lowlevel_write_entry_unescaped(builder, continuation, options);
    }

    //Writes comment:
    // "this is a comment thats too long \n"
    // "with newlines \n"
    // ->
    // # this is comment
    // #  thats too long
    // # with newlines
    //  <-------------->
    // writer->max_comment_size
    else if(kind == LPF_KIND_COMMENT)
    {
        //Split comment into segments (by lines and so that its shorter then max_value_size)
        _lpf_split_into_segments(&comment_segments, comment, format->max_comment_size);
        
        //estimate the needed size
        isize real_segment_count = MAX(comment_segments.size, 1);
        isize expected_size = comment.size + real_segment_count * (LINE_EXTRA + options.line_indentation);
        array_reserve(builder, builder->size + expected_size);

        for(isize i = 0; i < comment_segments.size; i++)
        {
            _Lpf_Segment seg = comment_segments.data[i];
            Lpf_Entry continuation = {LPF_KIND_COMMENT};
            continuation.comment = seg.string;
            lpf_lowlevel_write_entry_unescaped(builder, continuation, options);
        }
        
        if(comment_segments.size == 0)
        {
            Lpf_Entry continuation = {LPF_KIND_COMMENT};
            continuation.comment = comment;
            lpf_lowlevel_write_entry_unescaped(builder, continuation, options);
        }
    }
    
    //Writes entry:
    // "this is a value thats too long \n"
    // "with newlines \n"
    // ->
    // :this is a value thats #
    // ;too long #
    // ,with newlines #
    // ,#
    //  <-------------------->
    //  writer->max_value_size
    //  (with no flag)
    //
    // " 123\n123", "comment"
    // ->
    // : 123 
    // ,123 #comment
    // (with LPF_FLAG_COMMENT_TERMINATE_NEVER)
    else
    {
        //Split value into segments (by lines and so that its shorter then max_value_size)
        LPF_PERF_COUNTER_START(write_value_c);
        bool had_too_long = false;
        if(value.size > 0)
            had_too_long = _lpf_split_into_segments(&value_segments, value, format->max_value_size);
            
        if(value_segments.size > 1)
            if((entry.format_flags & LPF_FLAG_NEWLINE_AGNOSTIC) == 0)
                options.comment_terminate_value = true;

        if(had_too_long)
            options.comment_terminate_value = true;
            
        isize pad_prefix_to = MAX(_lpf_calculate_prefix_size(label.size, type.size), options.pad_prefix_to);
        if(format->pad_continuations)
            options.pad_prefix_to = pad_prefix_to;

        //estimate the needed size for large values
        if(value.size > 1000)
        {
            isize real_segment_count = MAX(value_segments.size, 1);
            isize inline_comment_size = LINE_EXTRA + comment.size;
            isize expected_size = value.size + real_segment_count*(LINE_EXTRA+options.line_indentation+pad_prefix_to) + inline_comment_size;
            array_reserve(builder, builder->size + expected_size);
        }
        
        LPF_PERF_COUNTER_START(value_segments_c);
        for(isize i = 0; i < value_segments.size; i++)
        {
            _Lpf_Segment segment = value_segments.data[i];
            Lpf_Entry continuation = {LPF_KIND_BLANK};
            continuation.kind = segment.kind;
            continuation.value = segment.string;

            //if is first segment add the prefix as well
            if(i == 0)
            {
                continuation.kind = kind;
                continuation.type = type;
                continuation.label = label;
            }
            
            //Only add the comments to the last entry
            if(i == value_segments.size - 1)
            {
                continuation.comment = comment;
            }
        
            lpf_lowlevel_write_entry_unescaped(builder, continuation, options);
        }
        LPF_PERF_COUNTER_END(value_segments_c);

        //@TODO: make better
        if(value_segments.size == 0)
        {
            Lpf_Entry continuation = {kind};
            continuation.type = type;
            continuation.label = label;
            continuation.value = value;
            continuation.comment = comment;
            lpf_lowlevel_write_entry_unescaped(builder, continuation, options);
        }
        LPF_PERF_COUNTER_END(write_value_c);
    }
    
    LPF_PERF_COUNTER_START(fixup_c);
    array_deinit(&escaped_inline_comment);
    array_deinit(&value_segments);
    array_deinit(&comment_segments);
    array_deinit(&escaped_label_builder);
    array_deinit(&escaped_type_builder);
    LPF_PERF_COUNTER_END(fixup_c);

    LPF_PERF_COUNTER_END(c);
}

EXPORT void lpf_reader_deinit(Lpf_Reader* reader)
{
    lpf_reader_commit_entries(reader);
    array_deinit(&reader->scopes);
    array_deinit(&reader->last_value);
    array_deinit(&reader->last_comment);
    memset(reader, 0, sizeof *reader);
}

EXPORT void lpf_reader_reset(Lpf_Reader* reader)
{
    lpf_reader_commit_entries(reader);
    array_clear(&reader->scopes);
    array_clear(&reader->last_value);
    array_clear(&reader->last_comment);
}


EXPORT void lpf_dyn_entry_deinit_text_parts(Lpf_Dyn_Entry* dyn)
{
    if(dyn->text_parts != NULL)
    {
        isize combined_size = dyn->label_size + dyn->type_size + dyn->comment_size + dyn->value_size;
        isize allocated_size = combined_size + 4;

        allocator_deallocate(dyn->allocator, dyn->text_parts, allocated_size, DEF_ALIGN, SOURCE_INFO());
        dyn->label_size = 0;
        dyn->type_size = 0;
        dyn->comment_size = 0;
        dyn->value_size = 0;
        dyn->text_parts = NULL;
    }
}

EXPORT void lpf_dyn_entry_set_text_capacity_and_data(Lpf_Dyn_Entry* dyn, isize label_size, const char* label_or_null, isize type_size, const char* type_or_null, isize comment_size, const char* comment_or_null, isize value_size, const char* value_or_null)
{
    if(dyn->allocator == NULL)
        dyn->allocator = allocator_get_default();

    isize combined_size = label_size + type_size + comment_size + value_size;
    u8* data = NULL;
    if(combined_size > 0)
    {
        isize allocated_size = combined_size + 4;
        data = (u8*) allocator_allocate(dyn->allocator, allocated_size, DEF_ALIGN, SOURCE_INFO());
        memset(data, 0, allocated_size);

        isize curr_pos = 0;
        if(label_or_null)
            memcpy(data + curr_pos, label_or_null, label_size);
        curr_pos += label_size + 1;
           
        if(type_or_null)
            memcpy(data + curr_pos, type_or_null, type_size);
        curr_pos += type_size + 1;
                
        if(comment_or_null)
            memcpy(data + curr_pos, comment_or_null, comment_size);
        curr_pos += comment_size + 1;
                
        if(value_or_null)
            memcpy(data + curr_pos, value_or_null, value_size);
        curr_pos += value_size + 1;
        
        ASSERT_MSG(curr_pos == allocated_size, "all data should be properly escaped");
    }
    
    lpf_dyn_entry_deinit_text_parts(dyn);
    dyn->label_size = label_size;
    dyn->type_size = type_size;
    dyn->comment_size = comment_size;
    dyn->value_size = value_size;
    dyn->text_parts = (char*) data;
}

EXPORT void lpf_dyn_entry_set_text_capacity(Lpf_Dyn_Entry* dyn, isize label_size, isize type_size, isize comment_size, isize value_size)
{
    lpf_dyn_entry_set_text_capacity_and_data(dyn, label_size, NULL, type_size, NULL, comment_size, NULL, value_size, NULL);
}

INTERNAL void _lpf_dyn_entry_deinit_children_self_only(Lpf_Dyn_Entry* dyn)
{
    if(dyn->children != NULL)
    {
        allocator_deallocate(dyn->allocator, dyn->children, dyn->children_capacity*sizeof(Lpf_Dyn_Entry), DEF_ALIGN, SOURCE_INFO());
        dyn->children = NULL;
        dyn->children_capacity = 0;
        dyn->children_size = 0;
    }
}

INTERNAL void _lpf_dyn_entry_deinit_self_only(Lpf_Dyn_Entry* dyn, void* context)
{
    (void) context;
    lpf_dyn_entry_deinit_text_parts(dyn);
    _lpf_dyn_entry_deinit_children_self_only(dyn);
}

EXPORT void lpf_dyn_entry_map(Lpf_Dyn_Entry* dyn, void(*preorder_func)(Lpf_Dyn_Entry* dyn, void* context), void(*postorder_func)(Lpf_Dyn_Entry* dyn, void* context), void* context)
{
    if(dyn->children_size > 0)
    {
        typedef struct Iterator {
            Lpf_Dyn_Entry* scope;
            isize i;
        } Iterator;

        DEFINE_ARRAY_TYPE(Iterator, Iterator_Array);
        
        Iterator_Array iterators = {0};
        array_init_backed(&iterators, allocator_get_scratch(), 32);
        
        Iterator top_level_it = {dyn, 0};
        array_push(&iterators, top_level_it);

        while(iterators.size > 0)
        {
            Iterator* it = array_last(iterators);
            bool going_deeper = false;

            for(; it->i < it->scope->children_size; it->i++)
            {
                Lpf_Dyn_Entry* child = &it->scope->children[it->i];
                if(preorder_func)
                    preorder_func(child, context);

                if(child->children != NULL)
                {
                    Iterator child_level_it = {child, 0};
                    array_push(&iterators, child_level_it);
                    going_deeper = true;
                }
                else
                {
                    if(postorder_func)
                        postorder_func(child, context);
                }
            }

            if(going_deeper == false)
            {
                if(postorder_func)
                    postorder_func(it->scope, context);

                array_pop(&iterators);
            }
        }
    }
    else
    {
        if(preorder_func)
            preorder_func(dyn, context);
            
        if(postorder_func)
            postorder_func(dyn, context);
    }
}

EXPORT void lpf_dyn_entry_deinit(Lpf_Dyn_Entry* dyn)
{
    if(dyn->children != NULL)
        lpf_dyn_entry_map(dyn, NULL, _lpf_dyn_entry_deinit_self_only, NULL);
    else
        _lpf_dyn_entry_deinit_self_only(dyn, NULL);
    memset(dyn, 0, sizeof *dyn);
}


EXPORT isize lpf_find_index(Lpf_Dyn_Entry in_children_of, Lpf_Kind kind, String label, String type, isize from)
{
    for(isize i = from; i < in_children_of.children_size; i++)
    {
        Lpf_Dyn_Entry* child = &in_children_of.children[i];
        if(kind != (Lpf_Kind) -1 && child->kind != kind)
            continue;

        Lpf_Entry child_e = lpf_entry_from_dyn_entry(*child);
        if(label.size > 0 && string_is_equal(child_e.label, label) == false)
            continue;
            
        if(type.size > 0 && string_is_equal(child_e.type, type) == false)
            continue;

        return i;
    }

    return -1;
}

EXPORT Lpf_Dyn_Entry* lpf_find(Lpf_Dyn_Entry in_children_of, Lpf_Kind kind, const char* label, const char* type)
{
    String label_str = string_make(label);
    String type_str = string_make(type);
    isize found = lpf_find_index(in_children_of, kind, label_str, type_str, 0);
    if(found == -1)
        return NULL;
    else
        return &in_children_of.children[found];
}

EXPORT void lpf_dyn_entry_push_dyn(Lpf_Dyn_Entry* dyn, Lpf_Dyn_Entry push)
{
    if(dyn->children_size + 1 > dyn->children_capacity)
    {
        isize new_capacity = 2;
        while(new_capacity <= dyn->children_capacity)
            new_capacity *= 2;

        if(dyn->allocator == NULL)
            dyn->allocator = allocator_get_default();
        dyn->children = (Lpf_Dyn_Entry*) allocator_reallocate(dyn->allocator, new_capacity*sizeof(Lpf_Dyn_Entry), dyn->children, dyn->children_capacity*sizeof(Lpf_Dyn_Entry), DEF_ALIGN, SOURCE_INFO());
        dyn->children_capacity = (u32) new_capacity;
    }
        
    dyn->children[dyn->children_size++] = push;
}

EXPORT void lpf_dyn_entry_push(Lpf_Dyn_Entry* dyn, Lpf_Entry push)
{
    Lpf_Dyn_Entry pushed = {0};
    pushed.allocator = dyn->allocator;
    lpf_dyn_entry_from_entry(&pushed, push);
    lpf_dyn_entry_push_dyn(dyn, pushed);
}

EXPORT void lpf_dyn_entry_from_entry(Lpf_Dyn_Entry* dyn, Lpf_Entry entry)
{
    if(dyn->allocator == NULL)
        dyn->allocator = allocator_get_default();

    dyn->kind = entry.kind;
    dyn->error = entry.error;
    dyn->line_number = entry.line_number;
    dyn->depth = (u32) entry.depth;
    dyn->format_flags = entry.format_flags;

    lpf_dyn_entry_set_text_capacity_and_data(dyn, entry.label.size, entry.label.data, entry.type.size, entry.type.data, entry.comment.size, entry.comment.data, entry.value.size, entry.value.data);
}

EXPORT Lpf_Entry lpf_entry_from_dyn_entry(Lpf_Dyn_Entry dyn)
{
    Lpf_Entry entry = {LPF_KIND_BLANK};

    entry.kind = (Lpf_Kind) dyn.kind;
    entry.error = (Lpf_Error) dyn.error;
    entry.line_number = dyn.line_number;
    entry.depth = dyn.depth;
    entry.format_flags = dyn.format_flags;

    entry.children = dyn.children;
    entry.children_size = dyn.children_size;
    entry.children_capacity = dyn.children_capacity;

    if(dyn.text_parts != NULL)
    {
        isize label_from = 0;
        isize label_to = dyn.label_size;

        isize type_from = label_to + 1;
        isize type_to = type_from + dyn.type_size;

        isize comment_from = type_to + 1;
        isize comment_to = comment_from + dyn.comment_size;
                
        isize value_from = comment_to + 1;
        isize value_to = value_from + dyn.value_size;

        isize comnined_size = dyn.label_size + dyn.type_size + dyn.comment_size + dyn.value_size;
        String whole_text = {dyn.text_parts, comnined_size + 4};

        entry.label = string_range(whole_text, label_from, label_to);
        entry.type = string_range(whole_text, type_from, type_to);
        entry.value = string_range(whole_text, value_from, value_to);
        entry.comment = string_range(whole_text, comment_from, comment_to);
    }

    return entry;
}

EXPORT void lpf_reader_commit_entries(Lpf_Reader* reader)
{
    if(reader->has_last_entry)
    {
        Lpf_Entry last = reader->last_entry;
        last.value = string_from_builder(reader->last_value);
        last.comment = string_from_builder(reader->last_comment);
        
        //if this is a value entry that had multiple lines but none of them were comment terminated
        // nor had comments then is (probably) newline agnostic
        if(reader->had_continuation && last.kind == LPF_KIND_ENTRY)
        {
            if((last.format_flags & LPF_FLAG_WHITESPACE_SENSITIVE) == 0)
                last.format_flags |= LPF_FLAG_NEWLINE_AGNOSTIC;
        }

        Lpf_Dyn_Entry* parent = (Lpf_Dyn_Entry*) *array_last(reader->scopes);
        lpf_dyn_entry_push(parent, last);

        Lpf_Entry null_entry = {LPF_KIND_BLANK};
        reader->last_entry = null_entry;
        array_clear(&reader->last_comment);
        array_clear(&reader->last_value);
    }
    reader->has_last_entry = false;
    reader->had_continuation = false;
}

EXPORT void lpf_reader_queue_entry(Lpf_Reader* reader, Lpf_Entry entry, const Lpf_Format_Options* options)
{
    lpf_reader_commit_entries(reader);

    if(entry.kind != LPF_KIND_COMMENT && options->skip_inline_comments)
        entry.comment = STRING("");

    if(options->skip_types)
        entry.type = STRING("");

    reader->has_last_entry = true;
    reader->last_entry = entry;
    builder_append(&reader->last_comment, entry.comment);
    builder_append(&reader->last_value, entry.value);
}
    
EXPORT Lpf_Error lpf_read_entry(Lpf_Reader* reader, Lpf_Dyn_Entry* into, Lpf_Entry entry, const Lpf_Format_Options* options)
{
    if(reader->scopes.size == 0)
        array_push(&reader->scopes, into);

    Lpf_Kind last_kind = reader->last_entry.kind;

    reader->line_number += 1;
    entry.line_number = reader->line_number;
    entry.depth = reader->depth;

    if(entry.error != LPF_ERROR_NONE)
    {
        if(options->skip_errors)
            goto function_end;
    }
    switch(entry.kind)
    {
        case LPF_KIND_BLANK: {
            if(options->skip_blanks)
                lpf_reader_commit_entries(reader);
            else
            {
                if(options->skip_connecting_blanks && reader->has_last_entry && last_kind == LPF_KIND_BLANK)
                {
                    //** nothing **
                }
                else
                {
                    lpf_reader_queue_entry(reader, entry, options);
                }
            }
        } break;

        case LPF_KIND_COMMENT: {
            if(options->skip_comments)
                lpf_reader_commit_entries(reader);
            else
            {
                if(reader->has_last_entry && last_kind == LPF_KIND_COMMENT)
                {
                    reader->had_continuation = true;
                    builder_append(&reader->last_comment, entry.value);
                    builder_append(&reader->last_comment, STRING("\n"));

                    reader->last_entry.format_flags |= entry.format_flags;
                }
                else
                {
                    lpf_reader_queue_entry(reader, entry, options);
                }
            }
        } break;
            
        case LPF_KIND_ENTRY: {
            lpf_reader_queue_entry(reader, entry, options);
        } break;

        case LPF_KIND_CONTINUATION:
        case LPF_KIND_ESCAPED_CONTINUATION: {
            bool was_last_proper_continaution = reader->has_last_entry 
                &&  (last_kind == LPF_KIND_ENTRY 
                    || last_kind == LPF_KIND_CONTINUATION 
                    || last_kind == LPF_KIND_ESCAPED_CONTINUATION);

            if(was_last_proper_continaution)
            {
                reader->had_continuation = true;
                if(entry.kind == LPF_KIND_CONTINUATION)
                    builder_append(&reader->last_value, STRING("\n"));
                builder_append(&reader->last_value, entry.value);

                if(entry.comment.size > 0)
                {
                    builder_append(&reader->last_comment, entry.value);
                    builder_append(&reader->last_comment, STRING("\n"));
                }

                reader->last_entry.format_flags |= entry.format_flags;
            }
            else
            {
                entry.error = LPF_ERROR_ENTRY_CONTINUNATION_WITHOUT_START;
                if(options->correct_errors)
                    entry.kind = LPF_KIND_ENTRY;
                else if(options->skip_errors)
                    goto function_end;
                    
                lpf_reader_queue_entry(reader, entry, options);
            }
        } break;

        case LPF_KIND_SCOPE_START: {
            reader->depth += 1;
            
            lpf_reader_commit_entries(reader);
            if(options->skip_scopes == false)
            {
                lpf_reader_queue_entry(reader, entry, options);
                lpf_reader_commit_entries(reader);
            
                Lpf_Dyn_Entry* parent = (Lpf_Dyn_Entry*) *array_last(reader->scopes); 
                Lpf_Dyn_Entry* pushed_scope = &parent->children[parent->children_size - 1];
                array_push(&reader->scopes, pushed_scope);
                ASSERT_MSG(pushed_scope->depth == entry.depth && pushed_scope->line_number == entry.line_number,
                    "The pushed scope should be the current entry");
            }
        } break;

        case LPF_KIND_SCOPE_END: {
            lpf_reader_commit_entries(reader);
            if(options->skip_scopes == false)
            {
                if(options->skip_scope_ends == false)
                {
                    lpf_reader_queue_entry(reader, entry, options);
                    lpf_reader_commit_entries(reader);
                }

                if(reader->scopes.size >= 1)
                    array_pop(&reader->scopes);
            }
            

            if(reader->depth <= 0)
                entry.error = LPF_ERROR_SCOPE_TOO_MANY_ENDS;
            reader->depth = MAX(reader->depth - 1, 0);
        } break;
    }

    function_end:
    return entry.error;
}

EXPORT Lpf_Error lpf_read_custom(String source, Lpf_Dyn_Entry* into, const Lpf_Format_Options* options)
{
    Lpf_Reader reader = {0};
    isize last_source_i = 0;
    Lpf_Error last_error = LPF_ERROR_NONE;
    into->kind = LPF_KIND_SCOPE_START;
    while(true)
    {
        Lpf_Entry entry = {LPF_KIND_BLANK};
        isize next_source_i = lpf_lowlevel_read_entry(source, last_source_i, &entry);
        if(last_source_i == next_source_i)
            break;

        Lpf_Error error = lpf_read_entry(&reader, into, entry, options);
        if(error != LPF_ERROR_NONE)
        {
            last_error = error;
            if(options->log_errors)
            {
                String line = string_range(source, last_source_i, next_source_i);
                LOG_ERROR("LPF", "Error %s reading lpf file on line %lli depth %lli", lpf_error_to_string(entry.error), (lli) entry.line_number, (lli) entry.depth);
                    LOG_ERROR(">LPF", STRING_FMT, STRING_PRINT(line));
            }

            if(options->stop_on_error)
                break;
        }

        last_source_i = next_source_i;
    }

    lpf_reader_commit_entries(&reader);
    lpf_reader_deinit(&reader);

    return last_error;
}

INTERNAL i32 lpf_max_child_prefix(const Lpf_Dyn_Entry* dyn, i32 max_before)
{
    i32 max_prefix = max_before;
    if(dyn->format_flags & LPF_FLAG_ALIGN_MEMBERS)
    {
        for(isize i = 0; i < dyn->children_size; i++)
        {
            Lpf_Dyn_Entry* child = &dyn->children[i];
            isize prefix = _lpf_calculate_prefix_size(child->label_size, child->type_size);
            max_prefix = MAX((i32) prefix, max_prefix);
        }
    }

    return max_prefix;
}

EXPORT void lpf_write_custom(String_Builder* source, Lpf_Dyn_Entry root, const Lpf_Format_Options* options)
{
    typedef struct Iterator {
        const Lpf_Dyn_Entry* scope;
        u32 i;
        i32 prefix_pad;
    } Iterator;

    DEFINE_ARRAY_TYPE(Iterator, Iterator_Array);

    Lpf_Writer writer = {0};
    Allocator_Set prev_allocs = allocator_set_default(allocator_get_scratch());
    
    Iterator_Array iterators = {0};
    array_init_backed(&iterators, NULL, 32);

    Iterator top_level_it = {&root, 0, lpf_max_child_prefix(&root, options->pad_prefix_to)};
    array_push(&iterators, top_level_it);

    Lpf_Format_Options customized_options = *options; 
    while(iterators.size > 0)
    {
        bool going_deeper = false;
        bool had_explicit_ending = false;

        Iterator* it = array_last(iterators);
        for(; it->i < it->scope->children_size; )
        {
            const Lpf_Dyn_Entry* dyn = &it->scope->children[it->i];
            Lpf_Entry entry = lpf_entry_from_dyn_entry(*dyn);
            customized_options.pad_prefix_to = it->prefix_pad;
            lpf_write_entry(&writer, source, entry, &customized_options);

            it->i += 1;
            if(entry.kind == LPF_KIND_SCOPE_START)
            {
                Iterator child_level_it = {dyn, 0, lpf_max_child_prefix(dyn, options->pad_prefix_to)};
                array_push(&iterators, child_level_it);
                going_deeper = true;
                break;
            }
            if(entry.kind == LPF_KIND_SCOPE_END)
            {
                had_explicit_ending = true;
                break;
            }
        }

        if(going_deeper == false)
        {
            array_pop(&iterators);
            if(had_explicit_ending == false && iterators.size > 0)
            {
                Lpf_Entry entry = {LPF_KIND_SCOPE_END};
                lpf_write_entry(&writer, source, entry, options);
            }
        }
    }

    array_deinit(&iterators);
    allocator_set(prev_allocs);
}


EXPORT Lpf_Error lpf_read(String source, Lpf_Dyn_Entry* root)
{
    Lpf_Format_Options options = lpf_make_default_format_options();
    return lpf_read_custom(source, root, &options);
}

EXPORT Lpf_Error lpf_read_meaningful(String source, Lpf_Dyn_Entry* root)
{
    Lpf_Format_Options options = lpf_make_default_format_options();
    options.skip_blanks = true;
    options.skip_comments = true;
    options.skip_inline_comments = true;
    options.skip_errors = true;
    options.skip_scope_ends = true;
    options.correct_errors = true;
    return lpf_read_custom(source, root, &options);
}

EXPORT void lpf_write(String_Builder* builder, Lpf_Dyn_Entry root)
{
    Lpf_Format_Options options = lpf_make_default_format_options();
    lpf_write_custom(builder, root, &options);
}

EXPORT void lpf_write_meaningful(String_Builder* builder, Lpf_Dyn_Entry root)
{
    Lpf_Format_Options options = lpf_make_default_format_options();
    options.skip_blanks = true;
    options.skip_comments = true;
    options.skip_inline_comments = true;
    options.skip_errors = true;
    lpf_write_custom(builder, root, &options);
}
#endif