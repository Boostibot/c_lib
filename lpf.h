#ifndef JOT_LPF
#define JOT_LPF

// This is a custom YAML-like format spec and implementation.
// 
// The main idea is to have each line start with a 'prefix' containing some 
// meta data used for parsing (labels, type, structure) hance the name
// LPF - Line Prefix Format. 
// 
// The benefit is that values require minimal escaping since the only value 
// we need to escape is newline. This in turn allows for tremendous variety 
// for formats and thus types the user can fill in. 
// 
// The LPF structure also simplifies parsing because each line is lexically 
// (and almost semantically) unqiue. This is also allows for trivial paralel 
// implementation where we could simply start parsing the file from N different 
// points in paralel and then simply join the results together to obtain a 
// valid parsed file.
// 
// The LPF idea can be implemented in variety of ways, this being just one of them.
// 
// The final format looks like the following:
// =========================================================
// 
// #The basic building block is a key value pair
// a_first_entry: its value which is a string
//              , which can span multiple lines
//              , or be escaped with ; if the new
//              ; line is just for readability
// 
// #A sample material declarion in the LPF format
// material {
//     name      : Wood   
//     reosultion: 1024
//     albedo    : 1 1 1
//     
//     #reduced roughness
//     roughness : 0.59
//     metallic  : 0
//     ao        : 0
//     emissive  : 0
//     mra       : 0 0 0
// 
//     #this is a long comment
//     #with multiple lines
//     albedo_map {
//         path  : images/wood_albedo.bmp
//         tile  : false
//         gamma : 2.2
//         gain  : 1
//         bias  : 0
//         offset: 0 0 0 
//         scale : 1 1 1 
//     }
//     
//     roughness_map { 
//         path: images/wood_roughness.bmp
//     }
// }
// =========================================================
//
// formally there are 7 lexical constructs in the LPF format.
// Each constructs is terminated in newline. The structure of each 
// is indicated below:
// 
//      BLANK:                  ( )\n
//      
//      COMMENT:                ( )# (comment)\n
//      
//      ENTRY:                  ( )(label)( ): (value)\n
//      CONTINUATION:           ( ), (value)\n    
//      ESCAPED_CONTINUATION:   ( ); (value)\n     
//      
//      COLLECTION_START:       ( )(label)( ){( )\n     
//      COLLECTION_END:         ( )}( )\n
//      COLLECTION_EMPTY:       ( )(label)( ){}\n
// 
// where () means optional and [] means obligatory
// specifically ( ), [ ] means whitespace.
// (label) and may contain any character except '#', ':', ',', ';', '{', '}' and whitespace.
// 
// in particular notice that all these have the same structure 
// (only have some fields mandatory and other prohibited). Thus 
// we can lex only this and figure out the rest in later stages of
// parsing.
// 
//     ( )[label]( )[marker] [value]

#include "parse.h"
#include "vformat.h"

typedef enum {
    LPF_ENTRY,
    LPF_COMMENT,
    LPF_COLLECTION,
} Lpf_Kind;

typedef struct Lpf_Entry {
    Lpf_Kind kind;
    i32 indentation;
    i32 blanks_before;

    i32 line;
    i32 children_count;
    i32 children_capacity;
    struct Lpf_Entry* children;

    String label;
    String value;
} Lpf_Entry;

typedef struct Lpf_Write_Options {
    bool discard_comments;
    bool discard_blanks;
    bool keep_original_indentation;
    bool align_entry_labels;
    bool align_collection_labels;
    bool align_continuations;
    bool indent_using_tabs;
    bool compact_empty_collections;

    i32 indentations_per_level;
    u32 _padding;
    isize max_line_width;
} Lpf_Write_Options;

typedef struct Lpf_Read_Options {
    bool discard_comments;
} Lpf_Read_Options;

typedef Array(Lpf_Entry) Lpf_Entry_Array;

EXTERNAL Lpf_Write_Options lpf_default_write_options();
EXTERNAL Lpf_Read_Options lpf_default_read_options();
EXTERNAL String lpf_write(Arena_Frame* arena, const Lpf_Entry* top_level, isize top_level_count, const Lpf_Write_Options* options_or_null);
EXTERNAL String lpf_write_from_root(Arena_Frame* arena, Lpf_Entry root, const Lpf_Write_Options* options_or_null);
EXTERNAL Lpf_Entry lpf_read(Arena_Frame* arena, String source, const Lpf_Read_Options* read_options_or_null);

//@TODO: rework strings
EXTERNAL String lpf_string_duplicate(Arena_Frame* arena, String string);
EXTERNAL Lpf_Entry* lpf_entry_push_child(Arena_Frame* arena, Lpf_Entry* parent, Lpf_Entry child);
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LPF_IMPL)) && !defined(JOT_LPF_HAS_IMPL)
#define JOT_LPF_HAS_IMPL

EXTERNAL Lpf_Entry* lpf_entry_push_child(Arena_Frame* arena, Lpf_Entry* parent, Lpf_Entry child)
{
    if(parent->children_count >= parent->children_capacity)
    {
        i32 new_capacity = parent->children_capacity * 3/2 + 2;
        Lpf_Entry* new_children = (Lpf_Entry*) arena_frame_push(arena, new_capacity*isizeof(Lpf_Entry), 8);
        memcpy(new_children, parent->children, (size_t) parent->children_count * sizeof(Lpf_Entry));

        parent->children_capacity = new_capacity;
        parent->children = new_children;
    }

    parent->children[parent->children_count++] = child;
    return parent->children + parent->children_count - 1;
}

EXTERNAL String lpf_string_duplicate(Arena_Frame* arena, String string)
{
    char* str = (char*) arena_frame_push_nonzero(arena, string.len + 1, 1);
    memcpy(str, string.data, (size_t) string.len);
    str[string.len] = '\0';
    return string_make(str, string.len);
}

INTERNAL void _lpf_commit_entry(Lpf_Entry_Array* entries_stack, Lpf_Entry* queued, String_Builder* queued_value, Arena_Frame* arena)
{
    Lpf_Entry new_entry = *queued;
    new_entry.value = lpf_string_duplicate(arena, queued_value->string);
    new_entry.label = lpf_string_duplicate(arena, queued->label);
    
    array_push(entries_stack, new_entry);
    builder_clear(queued_value);
    memset(queued, 0, sizeof *queued);
}

INTERNAL void _lpf_commit_collection(Lpf_Entry_Array* entries_stack, i32_Array* collections_from, Arena_Frame* arena)
{
    
    isize collection_from = *array_last(*collections_from);
    ASSERT(collection_from > 0);

    Lpf_Entry* parent = &entries_stack->data[collection_from - 1];
    parent->children_count = (i32) (entries_stack->len - collection_from);
    parent->children_capacity = parent->children_count;
    parent->children = (Lpf_Entry*) arena_frame_push_nonzero(arena, parent->children_count*isizeof(Lpf_Entry), 8);
    memcpy(parent->children, entries_stack->data + collection_from, (size_t) parent->children_count*sizeof(Lpf_Entry));

    array_resize(entries_stack, collection_from);
    array_pop(collections_from);
}

INTERNAL bool _lpf_is_label_invalid_char(char c)
{
    return c == ':' || c == ';' || c == ',' || c == '#' || c == '{' || c == '}' || c == ' ' || c == '\t';
}

EXTERNAL Lpf_Read_Options lpf_default_read_options()
{
    Lpf_Read_Options out = {false};
    return out;
}

EXTERNAL Lpf_Entry lpf_read(Arena_Frame* arena, String source, const Lpf_Read_Options* read_options_or_null)
{
    typedef enum {
        BLANK,
        ENTRY,
        ENTRY_CONTINUATION,
        ENTRY_CONTINUATION_ESCAPED, 
        COMMENT, 
        COLLECTION_START, 
        COLLECTION_END, 
        COLLECTION_EMPTY, 
        SYNTAX_ERROR
    } Type;
    
    typedef struct Lpf_Token {
        Type type;
        i32 indentation;

        isize label_from;
        isize label_to;
        isize value_from;
        isize value_to;
    } Lpf_Token;
    
    Lpf_Read_Options options = lpf_default_read_options();
    if(read_options_or_null)
        options = *read_options_or_null;

    Lpf_Entry root = {0};
    root.kind = LPF_COLLECTION;
    
    SCRATCH_ARENA(scratch)
    {
        typedef Array(Lpf_Token) Lpf_Token_Array;

        Lpf_Token_Array token_array = {0};
        array_init_with_capacity(&token_array, scratch.alloc, 1024);

        for(Line_Iterator it = {0}; line_iterator_get_line(&it, source); )
        {
            isize i = 0;
            Lpf_Token token = {BLANK};
            
            //Skip Whitespace before label and count indentation
            for(; i < it.line.len; i++)
            {
                char c = it.line.data[i];
                ASSERT(c != '\n');
                if(c == '\t')
                    token.indentation += 4;
                else if(c == ' ')
                    token.indentation += 1;
                else
                    break;
            }
        
            //match label (any not of ":;,#{} \t")
            token.label_from = i;
            for(; i < it.line.len; i++)
            {
                if(_lpf_is_label_invalid_char(it.line.data[i]))
                    break;
            }
            token.label_to = i;
        
            //Skip whitespace after label
            for(; i < it.line.len; i++)
            {
                char c = it.line.data[i];
                if(c != ' ' && c != '\t')
                    break;
            }
            token.value_from = i;
            token.value_to = it.line.len;

            //Match marker character (determine entry type)
            if(i < it.line.len)
            {
                char c = it.line.data[i];
                token.value_from = i + 1;
                switch(c)
                {
                    case ':': token.type = ENTRY; break;
                    case ';': token.type = ENTRY_CONTINUATION_ESCAPED;break;
                    case ',': token.type = ENTRY_CONTINUATION; break;
                    case '#': token.type = COMMENT; break;
                    case '}': token.type = COLLECTION_END; break;

                    //{ can be just { or {}
                    case '{': {
                        if(i + 1 < it.line.len && it.line.data[i + 1] == '}')
                        {
                            token.type = COLLECTION_EMPTY;
                            token.value_from = i + 2;
                        }
                        else
                            token.type = COLLECTION_START;
                    } break;

                    default: UNREACHABLE();
                }

                //If value starts with ' ' remove it
                // so: "key: value" has value just "value"
                if(token.value_from < it.line.len)
                {
                    if(it.line.data[token.value_from] == ' ')
                        token.value_from += 1;
                }
            }

            token.label_from += it.line_from;
            token.label_to += it.line_from;
            token.value_from += it.line_from;
            token.value_to += it.line_from;

            array_push(&token_array, token);
        }

        Lpf_Entry queued = {0};
        String_Builder queued_value = {0};
        i32_Array collections_from = {0};
        Lpf_Entry_Array entries_stack = {0};

        array_init_with_capacity(&collections_from, scratch.alloc, 32);
        array_init_with_capacity(&entries_stack, scratch.alloc, 1024);
        builder_init_with_capacity(&queued_value, scratch.alloc, 512);
        
        //add the root and its collection
        array_push(&entries_stack, root);
        array_push(&collections_from, (i32) entries_stack.len);

        i32 blanks_before = 0;
        for(isize i = 0; i < token_array.len; i++)
        {
            Lpf_Token token = token_array.data[i];
            String label = string_range(source, token.label_from, token.label_to); 
            String value = string_range(source, token.value_from, token.value_to); 
            i32 line = (i32) (i + 1);
            //String line = string_range(source, token.label_from, token.value_to); 

            //@TODO: ERROR reporting!!!
            bool had_error = false; (void) had_error;
            switch(token.type)
            {
                case BLANK: {
                    if(queued.line)
                        _lpf_commit_entry(&entries_stack, &queued, &queued_value, arena);

                    if(label.len > 0)
                    {
                        LOG_ERROR("lpf", "Parsing error at line %i: Missing format specifier (':', '[', '#', ...) after '%.*s'. Dicarding.", line, STRING_PRINT(label));
                        had_error = true; break;
                    }

                    blanks_before += 1;
                    ASSERT(value.len == 0, "shouldnt be possible to have a value while staying blank!");
                } break;

                case ENTRY: {
                    if(queued.line)
                        _lpf_commit_entry(&entries_stack, &queued, &queued_value, arena);

                    queued.kind = LPF_ENTRY;
                    queued.line = line;
                    queued.label = label;
                    queued.indentation = token.indentation;
                    queued.blanks_before = blanks_before; blanks_before = 0;
                    builder_append(&queued_value, value);
                } break;

                case ENTRY_CONTINUATION_ESCAPED:
                case ENTRY_CONTINUATION: {
                    if(label.len > 0)
                    {
                        LOG_ERROR("lpf", "Parsing error at line %i: Continuations cannot have labels. Label found '%.*s'. Ignoring.", line, STRING_PRINT(label));
                    }

                    if(queued.line == 0)
                    {
                        LOG_ERROR("lpf", "Parsing error at line %i: Stray continuation '%.*s'. All continautions need to be after entries (:). Discarding", line, STRING_PRINT(value));
                        had_error = true; break;
                    }

                    if(token.type != ENTRY_CONTINUATION_ESCAPED)
                        builder_push(&queued_value, '\n');
                    builder_append(&queued_value, value);
                } break;

                case COMMENT: {
                    if(queued.line && queued.kind != LPF_COMMENT)
                        _lpf_commit_entry(&entries_stack, &queued, &queued_value, arena);

                    if(options.discard_comments == false)
                    {
                        if(label.len > 0)
                            LOG_ERROR("lpf", "Parsing error at line %i: Comments cannot have labels. Label found '%.*s'. Ignoring.", line, STRING_PRINT(label));
                    
                        if(queued.line == 0)
                        {
                            queued.kind = LPF_COMMENT;
                            queued.line = line;
                            queued.label = label;
                            queued.indentation = (u16) token.indentation;
                            queued.blanks_before = (u16) blanks_before; 
                        }
                        else
                            builder_push(&queued_value, '\n');
                        builder_append(&queued_value, value);
                    }
                    blanks_before = 0;
                } break;

                case COLLECTION_START:
                case COLLECTION_EMPTY:
                case COLLECTION_END: {
                    if(queued.line)
                        _lpf_commit_entry(&entries_stack, &queued, &queued_value, arena);

                    if(token.type == COLLECTION_END && label.len > 0)
                        LOG_ERROR("lpf", "Parsing error at line %i: Collection ends cannot have labels. Label found '%.*s'. Ignoring.", line, STRING_PRINT(label));

                    String trimmed_whitespace_value = string_trim_whitespace(value);
                    if(trimmed_whitespace_value.len > 0)
                        LOG_ERROR("lpf", "Parsing error at line %i: Collections cannot have values. Value found '%.*s'. Ignoring.", line, STRING_PRINT(trimmed_whitespace_value));
                    
                    if(token.type == COLLECTION_END)
                    {
                        blanks_before = 0;
                        if(collections_from.len <= 1)
                            LOG_ERROR("lpf", "Parsing error at line %i: Extra collection end. Ignoring.", line);
                        else
                            _lpf_commit_collection(&entries_stack, &collections_from, arena);
                    }
                    else
                    {
                        queued.kind = LPF_COLLECTION;
                        queued.line = line;
                        queued.label = label;
                        queued.indentation = (u16) token.indentation;
                        queued.blanks_before = (u16) blanks_before; blanks_before = 0;
                        _lpf_commit_entry(&entries_stack, &queued, &queued_value, arena);

                        if(token.type == COLLECTION_START)
                            array_push(&collections_from, (i32) entries_stack.len);
                    }
                } break;

                case SYNTAX_ERROR: {
                    UNREACHABLE();
                } break;
            }
        }
        
        //Commit remianing entry
        if(queued.line)
            _lpf_commit_entry(&entries_stack, &queued, &queued_value, arena);

        ASSERT(collections_from.len >= 1);
        if(collections_from.len != 1)
            LOG_ERROR("lpf", "Parsing error at line %i: Missing %i collection end(s). Ignoring.", (i32) token_array.len, (i32) collections_from.len - 1);

        //Commit the remaining open collections (including root)
        while(collections_from.len > 0)
            _lpf_commit_collection(&entries_stack, &collections_from, arena);

        ASSERT(entries_stack.len == 1);
        root = entries_stack.data[0];
    }
    return root;
}

EXTERNAL Lpf_Write_Options lpf_default_write_options()
{
    Lpf_Write_Options options = {0};
    options.indentations_per_level = 4;
    options.align_entry_labels = true;
    options.align_continuations = true;
    options.align_collection_labels = false;
    options.compact_empty_collections = true;

    options.max_line_width = 80;
    options.indentations_per_level = 4;
    return options;
}

EXTERNAL String lpf_write_from_root(Arena_Frame* arena, Lpf_Entry root, const Lpf_Write_Options* options_or_null)
{
    //Writing is reverse process from reading so we first rpdouce an array of tokens and then serialize them all in a small forloop.
    typedef enum {
        BLANK,
        ENTRY,
        ENTRY_CONTINUATION,
        ENTRY_CONTINUATION_ESCAPED, 
        COMMENT, 
        COLLECTION_START, 
        COLLECTION_END, 
        COLLECTION_EMPTY, 
        SYNTAX_ERROR
    } Type;
    
    typedef struct Lpf_Token {
        Type type;
        i32 indentation;
        i32 pad_labels_to;
        i32 original_line;

        String label;
        String value;
    } Lpf_Token;

    typedef Array(Lpf_Token) Lpf_Token_Array;

    String return_string = {0};

    Lpf_Write_Options options = lpf_default_write_options();
    if(options_or_null)
        options = *options_or_null;

    if(options.max_line_width <= 0)
        options.max_line_width = INT64_MAX;

    enum {ALIGN_INDENT_EVERY = 10};
    Arena_Frame scratch = scratch_arena_frame_acquire();
    {
        typedef struct Iterator {
            Lpf_Entry* parent;
            isize i;
            i32 indentation;
            i32 pad_labels_to;
        } Iterator;

        typedef Array(Iterator) Iterator_Array;
        
        Lpf_Token_Array tokens = {0};
        Iterator_Array iterators = {0};
        array_init_with_capacity(&iterators, scratch.alloc, 32);
        array_init_with_capacity(&tokens, scratch.alloc, 256);
        
        Iterator first_it = {&root};
        //first_it.pad_labels_to = 

        array_push(&iterators, first_it);
        while(iterators.len > 0)
        {
            Iterator* it = array_last(iterators);
            for(; it->i < it->parent->children_count; )
            {
                //Every ALIGN_INDENT_EVERY calculate the max size of lables so we can align to it
                if(it->i % ALIGN_INDENT_EVERY == 0)
                {
                    isize iterate_to = MIN(it->parent->children_count - it->i, ALIGN_INDENT_EVERY);

                    isize max = 0;
                    for(isize i = it->i; i < iterate_to; i++)
                    {
                        Lpf_Entry entry = it->parent->children[i];
                        if(max < entry.label.len && entry.kind == LPF_ENTRY)
                            max = entry.label.len;
                    }

                    it->pad_labels_to = (i32) max;
                }

                it->i += 1;
                Lpf_Entry* entry = &it->parent->children[it->i - 1];
                String label = entry->label;
                String value = entry->value;
                
                i32 indentation = options.keep_original_indentation ? entry->indentation : it->indentation;
                
                if(options.discard_blanks == false)
                {
                    for(i32 i = 0; i < entry->blanks_before; i++)
                    {
                        Lpf_Token token = {BLANK};
                        token.indentation = indentation;
                        token.original_line = entry->line;
                        array_push(&tokens, token);
                    }
                }

                if(entry->kind == LPF_COMMENT)
                {
                    if(label.len > 0)
                        LOG_ERROR("lpf", "Writing error at line %i (entry from line %i): Collections may not have values. Found '%.*s'. Ignoring", (int) tokens.len, (i32) entry->line, STRING_PRINT(value));

                    label = STRING("");
                }

                //If entry or comment split the value into lines no longer than options.max_line_width
                //Push tokens from each line separately
                if(entry->kind == LPF_ENTRY || (entry->kind == LPF_COMMENT && options.discard_comments == false))
                {
                    isize token_counter = 0;
                    for(Line_Iterator line_it = {0}; line_iterator_get_line(&line_it, value);)
                    {
                        for(isize segment_from = 0; segment_from < line_it.line.len; segment_from += options.max_line_width, token_counter++)
                        {
                            String segment = string_safe_head(string_tail(line_it.line, segment_from), options.max_line_width);
                            
                            Lpf_Token token = {BLANK};
                            token.indentation = indentation;
                            token.value = segment;
                            token.pad_labels_to = it->pad_labels_to;
                            token.original_line = entry->line;
                            if(entry->kind == LPF_COMMENT)
                                token.type = COMMENT;
                            else
                            {
                                if(options.align_continuations)
                                    token.pad_labels_to = (i32) MAX(it->pad_labels_to, label.len);
                                
                                if(token_counter == 0)
                                {
                                    token.label = label;
                                    token.type = ENTRY;
                                }
                                else if(segment_from == 0)
                                    token.type = ENTRY_CONTINUATION;
                                else
                                    token.type = ENTRY_CONTINUATION_ESCAPED;
                            }
                            array_push(&tokens, token);
                        }
                    }
                }
                
                //If collection either push empty collection or push start collection and
                // setup the iterator to iterate through it
                if(entry->kind == LPF_COLLECTION)
                {
                    if(value.len > 0)
                        LOG_ERROR("lpf", "Writing error at line %i (entry from line %i): Comments may not have values. Found '%.*s'. Ignoring", (int) tokens.len, (i32) entry->line, STRING_PRINT(value));

                    value = STRING("");
                    
                    Lpf_Token token = {BLANK};
                    token.indentation = indentation;
                    token.label = label;
                    token.pad_labels_to = it->pad_labels_to;
                    token.original_line = entry->line;
                    if(entry->children_count == 0 && options.compact_empty_collections)
                    {
                        token.type = COLLECTION_EMPTY;
                        array_push(&tokens, token);
                    }
                    else
                    {
                        token.type = COLLECTION_START;
                        array_push(&tokens, token);

                        Iterator child_level_it = {entry};
                        child_level_it.indentation = it->indentation + options.indentations_per_level;
                        array_push(&iterators, child_level_it);
                        it = array_last(iterators);
                        break;
                    }
                }
            }

            //If reached end of this collection push end of collection token
            if(it->i >= it->parent->children_count)
            {
                array_pop(&iterators);
                if(iterators.len > 0)
                {
                    Iterator* deeper_it = array_last(iterators);
                    Lpf_Token token = {COLLECTION_END};
                    token.indentation = deeper_it->indentation;
                    array_push(&tokens, token);
                }
            }
        }
        
        String_Builder out = builder_make(scratch.alloc, 255);
        String_Builder indentation = builder_make(scratch.alloc, 127);
        isize indentation_level = -1;
        
        //We only pad up to 127 chars. If thats not enough too bad.
        String_Builder label_padding_buffer = builder_make(scratch.alloc, 127);
        builder_resize(&label_padding_buffer, 127);
        memset(label_padding_buffer.data, ' ', (size_t) label_padding_buffer.len);

        for(isize token_i = 0; token_i < tokens.len; token_i ++)
        {
            Lpf_Token token = tokens.data[token_i];
            if(token.type == BLANK)
            {
                builder_push(&out, '\n');
                continue;
            }
            
            String label = token.label;
            String value = token.value;

            //Recache ondentation and indent
            if(indentation_level != token.indentation)
            {
                builder_clear(&indentation);
                i32 indented_so_far = 0;
                if(options.indent_using_tabs)
                    for(; indented_so_far + 4 <= token.indentation; indented_so_far += 4)
                        builder_push(&indentation, '\t');
        
                for(; indented_so_far < token.indentation; indented_so_far ++)
                    builder_push(&indentation, ' ');

                indentation_level = token.indentation;
            }
            builder_append(&out, indentation.string);

            //Escape label
            if(label.len > 0)
            {
                isize label_from = 0;
                for(; label_from < label.len; label_from ++)
                {
                    char c = label.data[label_from];
                    if(char_is_space(c) == false)
                        break;
                }

                isize label_to = label_from;
                for(; label_to < label.len; label_to ++)
                {
                    char c = label.data[label_to];
                    if(char_is_space(c) || _lpf_is_label_invalid_char(c))
                        break;
                }

                String escaped_label = string_range(label, label_from, label_to);
                if(label_from != 0 || label_to != label.len)
                    LOG_ERROR("lpf", "Writing error at line %i (entry from line %i): Label contains invalid characters. Trimming '%.*s' to '%.*s'", (int) token_i + 1, token.original_line, STRING_PRINT(label), STRING_PRINT(escaped_label));
            }

            isize label_padding_ammount = CLAMP(token.pad_labels_to - label.len, 0, label_padding_buffer.len);
            String label_padding = string_head(label_padding_buffer.string, label_padding_ammount);
            
            //Append each token according to its own desired styling. 
            // This is the part of the code that can be tweaked a lot
            if(token.type == COMMENT)
            {
                builder_append(&out, STRING("# "));
                builder_append(&out, value);
                builder_push(&out, '\n');
            }
            else if(token.type == COLLECTION_END)
            {
                builder_append(&out, STRING("}\n"));
                continue;
            }
            else if(token.type == COLLECTION_START || token.type == COLLECTION_EMPTY)
            {
                builder_append(&out, label);
                if(options.align_collection_labels)
                    builder_append(&out, label_padding);

                if(label.len > 0)
                    builder_push(&out, ' ');

                if(token.type == COLLECTION_START)
                     builder_append(&out, STRING("{\n"));
                else
                     builder_append(&out, STRING("{}\n"));
                     
                continue;
            }
            else if(token.type == ENTRY || token.type == ENTRY_CONTINUATION || token.type == ENTRY_CONTINUATION_ESCAPED)
            {
                builder_append(&out, label);
                if((token.type == ENTRY && options.align_entry_labels)
                    || (token.type != ENTRY && options.align_continuations))
                    builder_append(&out, label_padding);
                    
                if(token.type == ENTRY)
                    builder_append(&out, STRING(": "));
                else if(token.type == ENTRY_CONTINUATION)
                    builder_append(&out, STRING(", "));
                else
                    builder_append(&out, STRING("; "));
                    
                builder_append(&out, value);
                builder_push(&out, '\n');
                continue;
            }
            else
            {
                UNREACHABLE();
            }
        }

        return_string = lpf_string_duplicate(arena, out.string);
    }
    arena_frame_release(&scratch);
    return return_string;
}

EXTERNAL String lpf_write(Arena_Frame* arena, const Lpf_Entry* top_level, isize top_level_count, const Lpf_Write_Options* options_or_null)
{
    Lpf_Entry root = {0};
    root.kind = LPF_COLLECTION;
    root.children = (Lpf_Entry*) top_level;
    root.children_count = (i32) top_level_count;

    return lpf_write_from_root(arena, root, options_or_null);
}

#endif