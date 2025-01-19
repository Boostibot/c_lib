#ifndef JOT_PATH
#define JOT_PATH

#include "string.h"
#include "scratch.h"
#include "log.h"

// This is a not exhaustive filepath handling facility. 
// We require all strings to pass through some basic parsing and be wrapped in Path struct. 
// This makes it easy to distinguish designated paths from any other strings. 
// Further we define Path_Builder which is guaranteed to always be in normalized form.
//
// Path_Info Represents the following:
// 
// \\?\C:/Users/Program_Files/./../Dir/file.txt
// <--><-><-------------------------->|<------>
//   P   R         D                  |  F  <->
//                                    M*      E
//                               
// Where:
//  P - prefix_size - this is OS specific (win32) prefix that carries meta data
//  R - root_size
//  D - directories_size
//  F - filename_size
//  E - extension_size
//  M* - This / is explicitly not including in directory_size.
//       This is because non normalized directory paths can but dont have to end
//       on /. This makes sure that both cases have the same size.
//
// All handling in this file respects the above categories and nothing more.
// Notably prefix is ignored in almost all operations but still is properly propagated when appending.
//
// Path_Builder is in normalized form. The following algorithm is used for normalization:
// (modified version of the one use std::filesystem::path. We respect "/" trailing to denote directories. We also respect windows prefixes.)
// 1 - If the path is empty, stop (normal form of an empty path is an empty path).
// 2 - Replace each directory-separator (which may consist of multiple /) with a single /
// 3 - Replace each slash character in the root-name with / (but not in prefix which is left unchanged!)
// 4 - Remove each dot and any immediately following /.
// 5 - Remove each non-dot-dot filename immediately followed by a / and a dot-dot.
// 6 - If there is root-directory, remove all dot-dots and any / immediately following them.
// 7 - Remove trailing /.
// 8 - If the path is empty, add a dot.
// 8 - Insert back trailing / if is directory path.
//
// The canonical path has the following invariants:
// 1) Path_Info is up to date including segment_count.
// 2) Path_Info.is_directory <=> ends with /
// 3) Includes only / (and not \)
// 4) Absolute paths do not contain any "." or ".." segments
// 5) Relative paths are either only "." and nothing more or dont contain "." at all. 
//    Relative paths contain ".." segments only as the first segment. 
//
//@NOTE: We attempt to cover a few edge cases and gain as much insight into the
//       path as we can but we by no means attempt to be exhaustively correct
//       for all special windows cases. As such this should be viewed as an
//       approximation of the exact solution rather than the final product.

typedef enum Path_Root_Kind {
    PATH_ROOT_NONE = 0,
    PATH_ROOT_SLASH,
    PATH_ROOT_SLASH_SLASH,
    PATH_ROOT_SERVER,
    PATH_ROOT_WIN,
    PATH_ROOT_UNKNOWN,
} Path_Root_Kind;

typedef struct Path_Info {
    i32 prefix_size;
    i32 root_content_from;
    i32 root_content_to;
    i32 root_size;
    i32 directories_size;
    i32 filename_size;
    i32 extension_size;
    i32 segment_count; //0 unless is_normalized
    Path_Root_Kind root_kind;
    bool is_absolute;
    bool is_directory;
    bool is_normalized; 
    bool has_trailing_slash;

    //is_normalized denotes if is in the canonical representation
    // is only set for Path_Info from Path_Builder, but is still
    // useful since we use Path as the interface type and thus
    // the is_normalized propagates. This can be used to not waste time
    // renormalizing again already normalized path.
} Path_Info;

typedef union Path {
    struct {
        String string;
        Path_Info info;
    };
    struct {
        const char* data;
        isize count;
    };
} Path;

EXTERNAL bool  is_path_sep(char c);
EXTERNAL isize string_find_first_path_separator(String string, isize from);
EXTERNAL isize string_find_last_path_separator(String string, isize from);

EXTERNAL Path   path_parse(String path);
EXTERNAL Path   path_parse_cstring(const char* path);
EXTERNAL void   path_parse_root(String path, Path_Info* info);
EXTERNAL void   path_parse_rest(String path, Path_Info* info);
EXTERNAL bool   path_is_empty(Path path);
EXTERNAL bool   path_is_equal(Path a, Path b); //Compares the textual representations of a and b for equality. To get more acurate results normalize the paths first.
EXTERNAL bool   path_is_equal_except_prefix(Path a, Path b); //Compares the textual representations of a and b without prefix for equality. To get more acurate results normalize the paths first.
EXTERNAL String path_get_prefix(Path path);
EXTERNAL String path_get_root(Path path);
EXTERNAL String path_get_directory(Path path);
EXTERNAL String path_get_extension(Path path);
EXTERNAL String path_get_filename(Path path);
EXTERNAL String path_get_root_content(Path path);
EXTERNAL String path_get_filename_without_extension(Path path);
EXTERNAL String path_get_without_trailing_slash(Path path);
EXTERNAL String path_get_segments(Path path);

EXTERNAL Path path_strip_prefix(Path path);
EXTERNAL Path path_strip_root(Path path);
EXTERNAL Path path_strip_trailing_slash(Path path);
EXTERNAL Path path_strip_last_segment(Path path, String* last_segment_or_null);
EXTERNAL Path path_strip_first_segment(Path path, Path* first_segment_or_null);
EXTERNAL Path path_strip_to_containing_directory(Path path);

typedef struct Path_Segement_Iterator {
    String segment;
    isize segment_number; //one based segment index
    isize segment_from;
    isize segment_to;
} Path_Segement_Iterator;

EXTERNAL bool path_segment_iterate_string(Path_Segement_Iterator* it, String path, isize till_root_size);
EXTERNAL bool path_segment_iterate(Path_Segement_Iterator* it, Path path);

typedef union Path_Builder {
    struct {
        String_Builder builder;
        Path_Info info;
    };

    struct {
        Allocator* allocator;
        isize capacity;
        union {
            String string;
            struct {
                char* data;
                isize count;
            };
        };
    };
    
    struct {
        Allocator* _1;
        isize _2;
        Path path;
    };
} Path_Builder;

enum {
    PATH_FLAG_APPEND_EVEN_WITH_ERROR = 1,   //Allows append: C:/hello/world + C:/file.txt == C:/hello/world/file.txt but still returns false
    PATH_FLAG_NO_REMOVE_DOT = 4,            //Treats "." segement as any other segment
    PATH_FLAG_NO_REMOVE_DOT_DOT = 8,        //Treats ".." segement as any other segment
    PATH_FLAG_BACK_SLASH = 16,              //Changes to use '\' instead of '/'
    PATH_FLAG_TRANSFORM_TO_DIR = 32,        //Adds trailing /
    PATH_FLAG_TRANSFORM_TO_FILE = 64,       //Removes trailing /
    PATH_FLAG_NO_ROOT = 128,                //Does not append root (for normalize this meens the result will not have root). 
    PATH_FLAG_NO_PREFIX = 256,              //Does not append prefix (for normalize this meens the result will not have prefix)
};

EXTERNAL void         path_builder_deinit(Path_Builder* builder);
EXTERNAL void         path_builder_init(Path_Builder* builder, Allocator* alloc_or_null, isize initial_capacity_or_zero);
EXTERNAL Path_Builder path_builder_make(Allocator* alloc_or_null, isize initial_capacity_or_zero);
EXTERNAL bool         path_builder_append(Path_Builder* into, Path path, int flags);
EXTERNAL void         path_builder_assign(Path_Builder* into, Path path, int flags);
EXTERNAL Path_Builder path_builder_dup(Allocator* alloc, Path_Builder to_copy);
EXTERNAL void         path_builder_clear(Path_Builder* builder);
EXTERNAL void         path_normalize_in_place(Path_Builder* path, int flags);

//Normalizes the given path removing '..', '.', double slashes, converting slashes to '/'. 
//When given relative resp. absolute path the output is relative resp. absolute.
//When given path with prefix output path will have the same prefix.
//Accepts additional flags to tweak some of the behaviour.
EXTERNAL Path_Builder path_normalize(Allocator* alloc, Path path, int flags);
EXTERNAL Path_Builder path_concat(Allocator* alloc, Path a, Path b);
EXTERNAL Path_Builder path_concat_many(Allocator* alloc, const Path* paths, isize path_count);
EXTERNAL void         path_make_relative_into(Path_Builder* into, Path relative_to, Path path);
EXTERNAL void         path_make_absolute_into(Path_Builder* into, Path relative_to, Path path);
EXTERNAL Path_Builder path_make_relative(Allocator* alloc, Path relative_to, Path path);
EXTERNAL Path_Builder path_make_absolute(Allocator* alloc, Path relative_to, Path path);

EXTERNAL Path path_get_executable();
EXTERNAL Path path_get_executable_directory();
EXTERNAL Path path_get_startup_working_directory();
EXTERNAL Path_Builder path_get_current_working_directoryXXX(Allocator* alloc, Platform_Error* error_or_null);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_PATH_IMPL)) && !defined(JOT_PATH_HAS_IMPL)
#define JOT_PATH_HAS_IMPL

#ifndef PROFILE_START
    #define PROFILE_START(...)
    #define PROFILE_STOP(...)
#endif

EXTERNAL bool is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

EXTERNAL isize string_find_first_path_separator(String string, isize from)
{
    for(isize i = from; i < string.count; i++)
        if(string.data[i] == '/' || string.data[i] == '\\')
            return i;

    return -1;
}

EXTERNAL isize string_find_last_path_separator(String string, isize from)
{
    for(isize i = from; i-- > 0; )
        if(string.data[i] == '/' || string.data[i] == '\\')
            return i;

    return -1;
}

EXTERNAL void path_parse_root(String path, Path_Info* info)
{
    PROFILE_START();
    info->prefix_size = 0;
    info->root_content_from = 0;
    info->root_content_to = 0;
    info->root_size = 0;
    info->root_kind = PATH_ROOT_NONE;
    info->is_absolute = false;

    String prefix_path = path;

    //https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    String win32_file_namespace = STRING("\\\\?\\");    // "\\?\"
    String win32_device_namespace = STRING("\\\\.\\");  // "\\.\"

    //Attempt to parse windows prefixes 
    if(string_is_prefixed_with(prefix_path, win32_file_namespace)) 
    {
        info->prefix_size = (i32) win32_file_namespace.count;
    }
    else if(string_is_prefixed_with(prefix_path, win32_device_namespace))
    {
        info->prefix_size = (i32) win32_device_namespace.count;
    }

    i32 root_from = info->prefix_size;
    String root_path = string_tail(prefix_path, info->prefix_size);
    if(root_path.count == 0)
    {
        info->is_absolute = false;
        info->is_normalized = true;
    }
    else
    {
        //Windows UNC server path //My_Root
        if(root_path.count >= 2 && is_path_sep(root_path.data[0]) && is_path_sep(root_path.data[1]))
        {
            if(root_path.count == 2)
            {
                info->root_content_from = 0;
                info->root_content_to = 0;
                info->root_size = 2;
                info->root_kind = PATH_ROOT_SLASH_SLASH;
            }
            else
            {
                isize root_end = string_find_first_path_separator(root_path, 2);
                if(root_end == -1)
                {
                    info->root_content_from = root_from + 2;
                    info->root_content_to = (i32) root_path.count;
                    info->root_size = (i32) root_path.count;
                }
                else
                {
                    info->root_content_from = root_from + 2;
                    info->root_content_to = root_from + (i32) root_end;
                    info->root_size = (i32) root_end + 1;
                }
                
                info->root_kind = PATH_ROOT_SERVER;
            }

            info->is_absolute = true;
        }
        //unix style root
        else if(root_path.count >= 1 && is_path_sep(root_path.data[0]))
        {
            info->root_content_from = root_from;
            info->root_content_to = root_from;
            info->is_absolute = true;
            info->root_size = 1;
            
            info->root_kind = PATH_ROOT_SLASH;
        }
        //Windows style root
        else if(root_path.count >= 2 && char_is_alphabetic(root_path.data[0]) && root_path.data[1] == ':')
        {
            info->root_content_from = root_from;
            info->root_content_to = root_from + 1;

            //In windows "C:some_file" means relative path on the drive C
            // while "C:/some_file" is absolute path starting from root C
            if(root_path.count >= 3 && is_path_sep(root_path.data[2]))
            {
                info->is_absolute = true;
                info->root_size = 3;
            }
            else
            {
                info->is_absolute = false;
                info->root_size = 2;
            }
            
            info->root_kind = PATH_ROOT_WIN;
        }
    }
    PROFILE_STOP();
}

EXTERNAL void path_parse_rest(String path, Path_Info* info)
{
    PROFILE_START();
    //Clear the overriden
    info->is_directory = false;
    info->is_directory = false;
    info->directories_size = 0;
    info->filename_size = 0;
    info->extension_size = 0;

    String root_path = string_tail(path, info->prefix_size);
    String directory_path = string_tail(path, info->prefix_size + info->root_size);

    if(root_path.count <= 0)
    {
        info->is_directory = true; //empty path is sometimes current directory. Thus is a directory
        info->is_normalized = true; //Empty path is invarinat
    }
    if(directory_path.count <= 0)
    {
        info->is_directory = true; //just root is considered a directory
    }
    else
    {
        //We consider path a directory path if it ends with slash. This incldues just "/" directory
        isize last = root_path.count - 1;
        ASSERT(last >= 0);
        info->is_directory = is_path_sep(root_path.data[last]);
        if(info->is_directory)
        {
            info->directories_size = (i32) directory_path.count - 1;
            info->has_trailing_slash = true;
        }
    }

    if(info->is_directory == false)
    {
        //Find the last directory segment
        isize file_i = 0;
        isize dir_i = string_find_last_path_separator(directory_path, directory_path.count);
        if(dir_i < 0)
            dir_i = 0;
        else
            file_i = dir_i + 1;
            

        info->directories_size = (i32) dir_i;

        //Parse filename
        String filename_path = string_safe_tail(directory_path, file_i);
        if(filename_path.count > 0)
        {
            //If is . or .. then is actually a directory
            if(string_is_equal(filename_path, STRING(".")) || string_is_equal(filename_path, STRING("..")))
            {
                info->is_directory = true;
                info->directories_size = (i32) directory_path.count;
            }
            else
            {
                //find the extension if any    
                isize dot_i = string_find_last_char(filename_path, '.');
                if(dot_i == -1)
                    dot_i = filename_path.count;
                else
                    dot_i += 1;

                info->filename_size = (i32) filename_path.count;
                info->extension_size = (i32) (filename_path.count - dot_i);
            }
        }
    }
    PROFILE_STOP();
}

EXTERNAL bool path_is_empty(Path path)
{
    return path.string.count <= path.info.prefix_size;
}

EXTERNAL bool path_is_equal(Path a, Path b)
{
    return string_is_equal(a.string, b.string);
}
EXTERNAL bool path_is_equal_except_prefix(Path a, Path b)
{
    return string_is_equal(path_strip_prefix(a).string, path_strip_prefix(b).string);
}

EXTERNAL Path path_parse(String path)
{
    PROFILE_START();
    Path out_path = {path};
    path_parse_root(path, &out_path.info);
    path_parse_rest(path, &out_path.info);
    PROFILE_STOP();
    return out_path;
}

EXTERNAL Path path_parse_cstring(const char* path)
{
    return path_parse(string_of(path));
}

EXTERNAL String path_get_prefix(Path path)
{
    return string_head(path.string, path.info.prefix_size);
}

EXTERNAL String path_get_root(Path path)
{
    return string_range(path.string, path.info.prefix_size, path.info.prefix_size + path.info.root_size);
}

EXTERNAL String path_get_root_content(Path path)
{
    return string_range(path.string, path.info.root_content_from, path.info.root_content_to);
}

EXTERNAL String path_get_directory(Path path)
{
    isize from = path.info.prefix_size + path.info.root_size;
    return string_range(path.string, from, from + path.info.directories_size);
}

EXTERNAL String path_get_without_trailing_slash(Path path)
{   
    if(path.info.has_trailing_slash)
        return path.string;
    else
        return string_head(path.string, path.string.count - 1);
}

EXTERNAL String path_get_segments(Path path)
{
    isize from = path.info.prefix_size + path.info.root_size;
    isize to = path.string.count;
    if(path.info.has_trailing_slash)
        to -= 1;

    return string_range(path.string, from, to);
}

EXTERNAL String path_get_filename(Path path)
{
    return string_range(path.string, path.string.count - path.info.filename_size, path.string.count);
}

EXTERNAL String path_get_filename_without_extension(Path path)
{
    String filename = path_get_filename(path);
    if(path.info.extension_size > 0)
        filename = string_head(filename, filename.count - path.info.extension_size - 1); 

    return filename;
}

EXTERNAL String path_get_extension(Path path)
{
    return string_range(path.string, path.string.count - path.info.extension_size, path.string.count);
}

EXTERNAL Path path_strip_prefix(Path path)
{
    Path out = path;
    out.string = string_tail(path.string, path.info.prefix_size);
    out.info.prefix_size = 0;
    return out;
}

EXTERNAL Path path_strip_root(Path path)
{   
    Path out = path;
    out.string = string_tail(path.string, path.info.prefix_size + path.info.root_size);
    out.info.prefix_size = 0;
    out.info.root_size = 0;
    out.info.root_kind = PATH_ROOT_NONE;
    out.info.root_content_from = 0;
    out.info.root_content_to = 0;
    return out;
}

EXTERNAL Path path_strip_trailing_slash(Path path)
{
    Path out = path;
    if(path.info.has_trailing_slash)
    {
        out.string.count = MAX(out.string.count - 1, 0);
        path_parse_rest(out.string, &out.info);
    }
    return out;
}

//Splits "C:/path/to/dir/"  --> "C:/path/to/" + "dir"
//       "path/to/file.txt" --> "path/to/" + "file.txt"
EXTERNAL Path path_strip_last_segment(Path path, String* last_segment_or_null)
{
    Path no_trailing = path_strip_trailing_slash(path);
    isize split_i = string_find_last_path_separator(no_trailing.string, no_trailing.string.count);
    isize root_till = path.info.root_size + path.info.prefix_size;
    if(split_i < root_till)
        split_i = root_till;
    else
        split_i += 1;

    if(last_segment_or_null)
        *last_segment_or_null = string_tail(no_trailing.string, split_i);
    
    Path out = no_trailing;
    out.string = string_head(no_trailing.string, split_i);
    path_parse_rest(out.string, &out.info);
    return out;
}

//Splits "C:/path/to/dir/"   --> "C:/path/" + "to/dir/"
//       "path/to/file.txt"  --> "path/" + "to/file.txt"
EXTERNAL Path path_strip_first_segment(Path path, Path* first_segment_or_null)
{
    isize root_till = path.info.root_size + path.info.prefix_size;
    isize split_i = string_find_first_path_separator(path.string, root_till);
    if(split_i == -1)
        split_i = path.string.count;
    else
        split_i += 1;

    if(first_segment_or_null)
    {
        first_segment_or_null->string = string_head(path.string, split_i);
        first_segment_or_null->info = path.info;
        path_parse_rest(first_segment_or_null->string, &first_segment_or_null->info);
    }
    
    Path out = path;
    out.string = string_tail(path.string, split_i);
    path_parse_root(out.string, &out.info);
    return out;
}

EXTERNAL Path path_strip_to_containing_directory(Path path)
{
    if(path.info.is_directory)
        return path;
    else
        return path_strip_last_segment(path, NULL);
}

EXTERNAL bool path_segment_iterate_string(Path_Segement_Iterator* it, String path, isize till_root_size)
{
    isize segment_from = till_root_size;
    if(it->segment_number != 0)
        segment_from = it->segment_to + 1;

    if(segment_from >= path.count)
        return false;
        
    isize segment_to = string_find_first_path_separator(path, segment_from);
        
    if(segment_to == -1)
        segment_to = path.count;
        
    it->segment_number += 1;
    it->segment_from = segment_from;
    it->segment_to = segment_to;
    it->segment = string_range(path, segment_from, segment_to);

    return true;
}

EXTERNAL bool path_segment_iterate(Path_Segement_Iterator* it, Path path)
{
    return path_segment_iterate_string(it, path.string, path.info.prefix_size + path.info.root_size);
}

EXTERNAL void path_builder_deinit(Path_Builder* builder)
{
    builder_deinit(&builder->builder);
    memset(builder, 0, sizeof *builder);
}
EXTERNAL void path_builder_init(Path_Builder* builder, Allocator* alloc_or_null, isize initial_capacity_or_zero)
{
    builder_init_with_capacity(&builder->builder, alloc_or_null, initial_capacity_or_zero);
    memset(&builder->info, 0, sizeof builder->info);
}

EXTERNAL Path_Builder path_builder_make(Allocator* alloc_or_null, isize initial_capacity_or_zero)
{
    Path_Builder builder = {builder_make(alloc_or_null, initial_capacity_or_zero)};
    return builder;
}

EXTERNAL void path_builder_clear(Path_Builder* builder)
{
    memset(&builder->info, 0, sizeof builder->info);
    builder_clear(&builder->builder);
}

EXTERNAL bool path_builder_append(Path_Builder* into, Path path, int flags)
{
    PROFILE_START();
    //@NOTE: this function is the main normalization function. It expects 
    // into to be in a valid state.
    builder_reserve(&into->builder, path.string.count*9/8 + 5);

    char slash = (flags & PATH_FLAG_BACK_SLASH) ? '\\' : '/';
    bool remove_dot = (flags & PATH_FLAG_NO_REMOVE_DOT) == 0;
    bool remove_dot_dot = (flags & PATH_FLAG_NO_REMOVE_DOT_DOT) == 0;
    bool transform_dir = (flags & PATH_FLAG_TRANSFORM_TO_DIR) > 0;
    bool transform_file = (flags & PATH_FLAG_TRANSFORM_TO_FILE) > 0;
    bool add_prefix = (flags & PATH_FLAG_NO_PREFIX) == 0;
    bool add_root = (flags & PATH_FLAG_NO_ROOT) == 0;
    bool ignore_error = (flags & PATH_FLAG_APPEND_EVEN_WITH_ERROR) == 0;

    #ifdef DO_ASSERTS_SLOW
    bool has_trailing_slash = false;
    String except_root = path_strip_root(into->path).string;
    if(except_root.count > 0 && is_path_sep(except_root.data[except_root.count - 1]))
        has_trailing_slash = true;

    ASSERT(into->info.has_trailing_slash == has_trailing_slash);
    #endif

    if(into->info.has_trailing_slash)
    {
        ASSERT(into->info.segment_count > 0);
        builder_resize(&into->builder, into->builder.count - 1);        
        into->info.has_trailing_slash = false;
    }

    bool was_empty = path_is_empty(into->path);

    bool state = true;
    if(add_prefix && was_empty && into->info.prefix_size == 0)
    {
        String prefix = path_get_prefix(path);
        builder_append(&into->builder, prefix);
        into->info.prefix_size = (i32) prefix.count;
    }

    if(path_is_empty(path) == false)
    {
        if(add_root && path.info.root_kind != PATH_ROOT_NONE)
        {
            //Only set root when empty else bad bad.
            if(was_empty)
            {
                String root_content = path_get_root_content(path);
                ASSERT(into->info.root_kind == PATH_ROOT_NONE);
                ASSERT(into->info.root_size == 0);
                ASSERT(into->info.root_content_from == 0);
                ASSERT(into->info.root_content_to == 0);

                switch(path.info.root_kind)
                {
                    case PATH_ROOT_NONE: {
                        ASSERT(path.info.is_absolute == false);
                    } break;

                    case PATH_ROOT_SLASH: {
                        builder_push(&into->builder, slash);
                    } break;

                    case PATH_ROOT_SLASH_SLASH: {
                        builder_push(&into->builder, slash);
                        builder_push(&into->builder, slash);
                    } break;

                    case PATH_ROOT_SERVER: {
                        builder_push(&into->builder, slash);
                        builder_push(&into->builder, slash);
                        if(path.info.root_content_to > path.info.root_content_from)
                        {
                            builder_append(&into->builder, root_content);
                            builder_push(&into->builder, slash);
                        }
                        else
                            LOG_WARN("path", "Empty prefix '%.*s' with PATH_ROOT_SERVER", STRING_PRINT(root_content));
                    } break;

                    case PATH_ROOT_WIN: {
                        char c = 'C';
                        if(root_content.count > 0 && char_is_alphabetic(root_content.data[0]))
                            c = root_content.data[0];
                        else
                            LOG_WARN("path", "Strange prefix '%.*s' with PATH_ROOT_WIN", STRING_PRINT(root_content));

                        //to uppercase
                        if('a' <= c && c <= 'z')
                            c = c - 'a' + 'A';

                        builder_push(&into->builder, c);
                        builder_push(&into->builder, ':');
                        if(path.info.is_absolute)
                            builder_push(&into->builder, slash);
                    } break;

                    case PATH_ROOT_UNKNOWN: {
                        builder_append(&into->builder, path_get_root(path));
                    } break;
                }

                if(path.info.root_kind != PATH_ROOT_NONE)
                    path_parse_root(into->string, &into->info);
            }
            else
            {
                state = false;
            }
        
        }
        #ifdef DO_ASSERTS_SLOW
        Path_Info new_info = path_parse(into->string).info;
        ASSERT_SLOW(new_info.prefix_size == into->info.prefix_size);
        ASSERT_SLOW(new_info.root_kind == into->info.root_kind);
        ASSERT_SLOW(new_info.root_size == into->info.root_size);
        ASSERT_SLOW(new_info.root_content_from == into->info.root_content_from);
        ASSERT_SLOW(new_info.root_content_to == into->info.root_content_to);
        #endif

        if(state || ignore_error)
        {
            //@TODO: inline the loop and root_till!
            isize root_till = into->info.root_size + into->info.prefix_size;
            for(Path_Segement_Iterator it = {0}; path_segment_iterate(&it, path);)
            {
                bool push_segment = true;
                String segment = it.segment;
                //Multiple separators next to each otehr
                if(segment.count == 0)
                    push_segment = false;
                //Single dot segment
                else if(remove_dot && string_is_equal(segment, STRING(".")))
                    push_segment = false;
                //pop segment
                else if(remove_dot_dot && string_is_equal(segment, STRING("..")))
                {
                    if(into->info.is_absolute)
                        push_segment = false;
                    else
                        push_segment = true;

                    //If there was no segment to pop push the ".." segment
                    if(into->info.segment_count > 0)
                    {
                        isize last_segment_i = string_find_last_path_separator(into->string, into->string.count);
                        if(last_segment_i < root_till)
                           last_segment_i = root_till;
                        String last_segement = string_tail(into->string, last_segment_i);
                        if(string_is_equal(last_segement, STRING("..")) == false)
                        {
                            builder_resize(&into->builder, last_segment_i);
                            into->info.segment_count -= 1;
                            push_segment = false;
                        }
                    }
                }

                //push segment 
                if(push_segment)
                {
                    if(into->info.segment_count > 0)
                        builder_push(&into->builder, slash);

                    builder_append(&into->builder, segment);
                    into->info.segment_count += 1;
                }
            }
        }

        if(path_is_empty(into->path))
        {
            builder_push(&into->builder, '.');
            into->info.segment_count += 1;
        }

        path_parse_rest(into->string, &into->info);

        //We know it 100% does not have trialing slash
        ASSERT(into->info.has_trailing_slash == false);

        if(into->info.segment_count > 0)
        {
            //1) If it is a directory but is not trailing slash
            // it must be '.' or '..' then we add slash
            // because thats the normal form.
            //2) If we explicitly want to make a directory
            //3) If the path was a directory and we dont want explicitly want to make a file
            if(into->info.is_directory || transform_dir || (path.info.is_directory && transform_file == false))
            {
                builder_push(&into->builder, slash);
                path_parse_rest(into->string, &into->info);
            }
        }
    }
    else
    {
        path_parse_rest(into->string, &into->info);
    }

    into->info.is_normalized = true;

    #ifdef DO_ASSERTS_SLOW
    Path_Info new_info = path_parse(into->string).info;
    new_info.is_normalized = into->info.is_normalized;
    new_info.segment_count = into->info.segment_count;
    ASSERT(memcmp(&new_info, &into->info, sizeof(new_info)) == 0);
    #endif
    
    PROFILE_STOP();
    return state;
}

EXTERNAL void path_builder_assign(Path_Builder* into, Path path, int flags)
{
    path_builder_clear(into);
    path_builder_append(into, path, flags);
}

EXTERNAL Path_Builder path_builder_dup(Allocator* alloc, Path_Builder to_copy)
{
    Path_Builder duped = {0};
    duped.builder = builder_from_string(alloc, to_copy.string);
    duped.info = to_copy.info;
    return duped;
}

EXTERNAL void path_normalize_in_place(Path_Builder* into, int flags)
{
    SCRATCH_SCOPE(arena) {
        String_Builder copy = builder_from_string(arena.alloc, into->string);
        Path path = path_parse(copy.string);
        path_builder_assign(into, path, flags);
    }
}

EXTERNAL Path_Builder path_normalize(Allocator* alloc, Path path, int flags)
{
    Path_Builder builder = path_builder_make(alloc, 0);
    path_builder_append(&builder, path, flags);
    return builder;
}

EXTERNAL Path_Builder path_concat_many(Allocator* alloc, const Path* paths, isize path_count)
{
    //A simple heuristic to try to guess the needed capacity
    isize combined_cap = 10;
    for(isize i = 0; i < path_count; i++)
        combined_cap += paths->string.count*9/8;

    Path_Builder builder = path_builder_make(alloc, combined_cap);
    for(isize i = 0; i < path_count; i++)
        path_builder_append(&builder, paths[i], 0);
    
    return builder;
}

EXTERNAL Path_Builder path_concat(Allocator* alloc, Path a, Path b)
{
    Path paths[2] = {a, b};
    return path_concat_many(alloc, paths, 2);
}


EXTERNAL void path_make_relative_into(Path_Builder* into, Path relative_to, Path path)
{
    path_builder_clear(into);
    
    PROFILE_START();
    //If path is relative path we and the relative_to path is absolute then 
    // we cannot make it any more relative than it currently is.
    //Same happens vice versa.
    //If both are empty the result is also empty
    if((path.info.is_absolute == false && relative_to.info.is_absolute)
        || (path.info.is_absolute && relative_to.info.is_absolute == false)
        || (path_is_empty(relative_to) && path_is_empty(path)))
    {
        path_builder_assign(into, path, 0);
    }
    else
    {
        SCRATCH_SCOPE(arena) 
        {
            //Make paths normalized if they are not invarinat already. 
            // It is very likely that at least relative_to will be invarinat since 
            // most often it will be a path to the current executable which is cached
            // in normalized form.
            Path reli = path_strip_to_containing_directory(relative_to);
            Path pathi = path;

            Path_Builder reli_builder = {0}; 
            Path_Builder pathi_builder = {0}; 
            if(relative_to.info.is_normalized == false)
            {
                reli_builder = path_normalize(arena.alloc, relative_to, 0); 
                reli = reli_builder.path;
            }
            if(path.info.is_normalized == false)
            {
                pathi_builder = path_normalize(arena.alloc, path, 0); 
                pathi = pathi_builder.path;
            }

            //If roots differ we cannot make it more relative
            if(string_is_equal(path_get_root(reli), path_get_root(pathi)) == false)
                path_builder_assign(into, path, 0);
            else
            {
                Path_Segement_Iterator rel_it = {0};
                Path_Segement_Iterator path_it = {0};
                while(true)
                {
                    bool has_rel = path_segment_iterate(&rel_it, reli);
                    bool has_path = path_segment_iterate(&path_it, pathi);
                    bool are_equal = string_is_equal(rel_it.segment, path_it.segment);
                    path_builder_append(into, path_parse(path_get_prefix(pathi)), 0);

                    //If both are present and same do nothing
                    if(has_rel && has_path && are_equal)
                    {
                        //nothing
                    }
                    //If they were same and end the same then also do nothing
                    else if(has_rel == false && has_path == false && are_equal)
                    {
                        path_builder_append(into, path_parse_cstring("."), 0);
                        break;
                    }
                    else
                    {
                        //If rel is shorter than path add all remainig segments of `path` into `into`
                        if(has_rel == false)
                        {
                            builder_append(&into->builder, path_it.segment);
                            while(path_segment_iterate(&path_it, pathi))
                            {
                                builder_push(&into->builder, '/');
                                builder_append(&into->builder, path_it.segment);
                            }

                        }
                        //If there was a difference in the path or path is shorter
                        // we add appropriate amountof ".." segments then the rest of the path
                        else
                        {
                            builder_append(&into->builder, STRING(".."));
                            while(path_segment_iterate(&rel_it, reli))
                                builder_append(&into->builder, STRING("/.."));

                            builder_push(&into->builder, '/');
                            builder_append(&into->builder, path_it.segment);
                            while(path_segment_iterate(&path_it, pathi))
                            {
                                builder_push(&into->builder, '/');
                                builder_append(&into->builder, path_it.segment);
                            }
                        }
                    
                        path_normalize_in_place(into, path.info.is_directory ? PATH_FLAG_TRANSFORM_TO_DIR : PATH_FLAG_TRANSFORM_TO_FILE);
                        break;
                    }
                }
            }
        }
    }
    PROFILE_STOP();
}

EXTERNAL void path_make_absolute_into(Path_Builder* into, Path relative_to, Path path)
{
    path_builder_clear(into);
    if(path.info.is_absolute == false)
        path_builder_append(into, relative_to, 0);
    path_builder_append(into, path, 0);
}

EXTERNAL Path_Builder path_make_relative(Allocator* alloc, Path relative_to, Path path)
{
    Path_Builder out = path_builder_make(alloc, 0);
    path_make_relative_into(&out, relative_to, path);
    return out;
}

EXTERNAL Path_Builder path_make_absolute(Allocator* alloc, Path relative_to, Path path)
{
    Path_Builder out = path_builder_make(alloc, 0);
    path_make_absolute_into(&out, relative_to, path);
    return out;
}

EXTERNAL Path path_get_executable()
{
    static uint32_t init = 0;
    static Path_Builder builder = {0};
    if(platform_once_begin(&init))
    {
        Path path = path_parse_cstring(platform_get_executable_path());
        builder = path_normalize(allocator_get_malloc(), path, PATH_FLAG_TRANSFORM_TO_FILE);
        platform_once_end(&init);
    }

    return builder.path;
}

EXTERNAL Path path_get_executable_directory()
{
    static uint32_t init = 0;
    static Path_Builder builder = {0};
    if(platform_once_begin(&init))
    {
        Path exe_path = path_parse_cstring(platform_get_executable_path());
        Path containing = path_strip_to_containing_directory(exe_path);
        builder = path_normalize(allocator_get_malloc(), containing, PATH_FLAG_TRANSFORM_TO_DIR);
        platform_once_end(&init);
    }

    return builder.path;
}

EXTERNAL Path path_get_startup_working_directory()
{
    static uint32_t init = 0;
    static Path_Builder builder = {0};
    if(platform_once_begin(&init))
    {
        Path working_path = path_parse_cstring(platform_directory_get_startup_working());
        builder = path_normalize(allocator_get_malloc(), working_path, PATH_FLAG_TRANSFORM_TO_DIR);
        platform_once_end(&init);
    }
    
    return builder.path;
}

EXTERNAL Path_Builder path_get_current_working_directoryXXX(Allocator* alloc, Platform_Error* error_or_null)
{
    char backing[1024];
    isize curr_size = ARRAY_LEN(backing);
    void* buffer = backing;

    for(int i = 0; i < 16; i++)
    {
        bool needs_bigger_buffer = false;
        Platform_Error error = platform_directory_get_current_working(buffer, curr_size, &needs_bigger_buffer);
        if(error_or_null)
            *error_or_null = error;
        if(needs_bigger_buffer == false)
            break;

        curr_size *= 2;
        if(buffer == backing)
            buffer = NULL;

        buffer = realloc(buffer, curr_size);
    }

    Path_Builder builder = path_normalize(alloc, path_parse_cstring(buffer), 0);
    if(buffer != backing)
        free(buffer);

    return builder;
}

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_PATH_TEST)) && !defined(JOT_PATH_HAS_TEST)
#define JOT_PATH_HAS_TEST

#define TEST_STRING_EQ(str1, str2) TEST(string_is_equal((str1), (str2)), "'%.*s' == '%.*s'", (int) str1.count, str1.data, (int) str2.count, str2.data)

enum {
    TEST_PATH_IS_DIR = 1,
    TEST_PATH_IS_EMPTY = 2,
    TEST_PATH_IS_ABSOLUTE = 4,
    TEST_PATH_TRAILING_SLASH = 8,
};

void test_single_path(const char* path, const char* prefix, const char* root, const char* directories, const char* filename, const char* extension, int flags)
{
    PROFILE_START();
    String _path = string_of(path);
    Path parsed = path_parse(_path);
    String _prefix = path_get_prefix(parsed);
    String _root = path_get_root(parsed);
    String _directories = path_get_directory(parsed);
    String _filename = path_get_filename(parsed);
    String _extension = path_get_extension(parsed);
    
    TEST_STRING_EQ(_prefix, string_of(prefix));
    TEST_STRING_EQ(_root, string_of(root));
    TEST_STRING_EQ(_directories, string_of(directories));
    TEST_STRING_EQ(_filename, string_of(filename));
    TEST_STRING_EQ(_extension, string_of(extension));

    TEST(parsed.info.is_absolute == ((flags & TEST_PATH_IS_ABSOLUTE) > 0));
    TEST(parsed.info.is_directory == ((flags & TEST_PATH_IS_DIR) > 0));
    TEST(parsed.info.has_trailing_slash == ((flags & TEST_PATH_TRAILING_SLASH) > 0));
    PROFILE_STOP();
}

void test_path_normalize(int flags, const char* cpath, const char* cexpected)
{
    PROFILE_START();
    const char* prefixes[] = {"", "\\\\?\\", "\\\\.\\"};
    for(isize i = 0; i < ARRAY_LEN(prefixes); i++)
    {
        SCRATCH_SCOPE(arena) {
            String_Builder prefixed_path = string_concat(arena.alloc, string_of(prefixes[i]), string_of(cpath));
            String_Builder prefixed_expected = string_concat(arena.alloc, string_of(prefixes[i]), string_of(cexpected));

            Path path = path_parse(prefixed_path.string);
            Path expected = path_parse(prefixed_expected.string);

            Path_Builder canonical = path_normalize(arena.alloc, path, flags);
            TEST_STRING_EQ(canonical.string, expected.string);
        }
    }
    PROFILE_STOP();
}

void test_canonicalize_with_roots_and_prefixes(int flags, const char* cabs_path, const char* cexpected)
{
    PROFILE_START();
    const char* roots[]      = {"\\", "C:/", "F:\\", "//Server/", "\\\\xxserverxx\\"};
    const char* norm_roots[] = {"/", "C:/", "F:/", "//Server/", "//xxserverxx/"};
    
    for(isize i = 0; i < ARRAY_LEN(roots); i++)
    {
        SCRATCH_SCOPE(arena) {
            String_Builder prefixed_path = string_concat(arena.alloc, string_of(roots[i]), string_of(cabs_path));
            String_Builder prefixed_expected = string_concat(arena.alloc, string_of(norm_roots[i]), string_of(cexpected));

            test_path_normalize(flags, prefixed_path.data, prefixed_expected.data);
        }
    }
    PROFILE_STOP();
}

enum {
    TEST_PATH_MAKE_RELATIVE,
    TEST_PATH_MAKE_ABSOLUTE,
};

void test_path_make_relative_absolute_with_prefixes(int flags, const char* crelative, const char* cpath, const char* cexpected)
{
    const char* prefixes[] = {"", "\\\\?\\", "\\\\.\\"};
    for(isize i = 0; i < ARRAY_LEN(prefixes); i++)
    {
        SCRATCH_SCOPE(arena) {
            String_Builder prefixed_relative = string_concat(arena.alloc, string_of(prefixes[i]), string_of(crelative));
            String_Builder prefixed_path = string_concat(arena.alloc, string_of(prefixes[i]), string_of(cpath));
            String_Builder prefixed_expected = string_concat(arena.alloc, string_of(prefixes[i]), string_of(cexpected));

            Path relative = path_parse(prefixed_relative.string);
            Path path = path_parse(prefixed_path.string);
            Path expected = path_parse(prefixed_expected.string);

            Path_Builder transformed = {0};
            if(flags == TEST_PATH_MAKE_RELATIVE)
                transformed = path_make_relative(arena.alloc, relative, path);
            else
                transformed = path_make_absolute(arena.alloc, relative, path);

            TEST_STRING_EQ(transformed.string, expected.string);
        }
    }
}

void test_path_strip_first(const char* path, const char* expected_head, const char* expected_tail)
{
    Path head = {0};
    Path tail = path_strip_first_segment(path_parse_cstring(path), &head);

    TEST_STRING_EQ(head.string, string_of(expected_head));
    TEST_STRING_EQ(tail.string, string_of(expected_tail));
}

void test_path_strip_last(const char* path, const char* expected_head, const char* expected_tail)
{
    Path tail = {0};
    Path head = path_strip_last_segment(path_parse_cstring(path), &tail.string);

    TEST_STRING_EQ(head.string, string_of(expected_head));
    TEST_STRING_EQ(tail.string, string_of(expected_tail));
}

void test_path()
{
    PROFILE_START();
    test_single_path("", "", "", "", "", "", TEST_PATH_IS_DIR);
    test_single_path(".", "", "", ".", "", "", TEST_PATH_IS_DIR);
    test_single_path("..", "", "", "..", "", "", TEST_PATH_IS_DIR);
    test_single_path("./", "", "", ".", "", "", TEST_PATH_IS_DIR | TEST_PATH_TRAILING_SLASH);
    test_single_path("../", "", "", "..", "", "", TEST_PATH_IS_DIR | TEST_PATH_TRAILING_SLASH);
    test_single_path("/", "", "/", "", "", "", TEST_PATH_IS_DIR | TEST_PATH_IS_ABSOLUTE);

    test_single_path("file.txt", "", "", "", "file.txt", "txt", 0);
    test_single_path("C:/my/files/file.txt", "", "C:/", "my/files", "file.txt", "txt", TEST_PATH_IS_ABSOLUTE);
    test_single_path("/my/files/file/", "", "/", "my/files/file", "", "", TEST_PATH_IS_ABSOLUTE | TEST_PATH_IS_DIR | TEST_PATH_TRAILING_SLASH);
    test_single_path("my/files/file", "", "", "my/files", "file", "", 0);
    test_single_path("my/files/file/..", "", "", "my/files/file/..", "", "", TEST_PATH_IS_DIR);
    test_single_path("\\\\?\\C:my/files/file", "\\\\?\\", "C:", "my/files", "file", "", 0);
    test_single_path("//Server/my/files/.gitignore", "", "//Server/", "my/files", ".gitignore", "gitignore", TEST_PATH_IS_ABSOLUTE);

    //Strip first
    test_path_strip_first("", "", "");
    test_path_strip_first("hello", "hello", "");
    test_path_strip_first("C:/", "C:/", "");
    test_path_strip_first("C:/..", "C:/..", "");
    test_path_strip_first("C:/my/files/file.txt", "C:/my/", "files/file.txt");
    test_path_strip_first("/files/path/to/directory/", "/files/", "path/to/directory/");
    test_path_strip_first("files/path/to/directory/", "files/", "path/to/directory/");

    //Strip last
    test_path_strip_last("", "", "");
    test_path_strip_last("hello", "", "hello");
    test_path_strip_last("C:/", "C:/", "");
    test_path_strip_last("C:/..", "C:/", "..");
    test_path_strip_last("C:/my/files/file.txt", "C:/my/files/", "file.txt");
    test_path_strip_last("/files/path/to/directory/", "/files/path/to/", "directory");
    test_path_strip_last("files/path/to/directory/", "files/path/to/", "directory");

    //Relative
    test_path_normalize(0, "", "");
    test_path_normalize(0, ".", "./");
    test_path_normalize(0, "..", "../");
    test_path_normalize(0, "C:..", "C:../");
    test_path_normalize(0, "file", "file");
    test_path_normalize(0, "file\\\\..", "./");
    test_path_normalize(0, "dir///dir///..", "dir/");
    test_path_normalize(0, "../.././file", "../../file");
    test_path_normalize(0, "dir/../../\\file", "../file");
    test_path_normalize(0, "C:dir/../../file", "C:../file");
    test_path_normalize(0, "dir\\dir\\..\\file", "dir/file");
    test_path_normalize(0, "dir\\dir\\..\\././file", "dir/file");
    
    //Absolute
    test_canonicalize_with_roots_and_prefixes(0, "", "");
    test_canonicalize_with_roots_and_prefixes(0, "././", "");
    test_canonicalize_with_roots_and_prefixes(0, "./.././", "");
    test_canonicalize_with_roots_and_prefixes(0, "file", "file");
    test_canonicalize_with_roots_and_prefixes(0, "file/..", "");
    test_canonicalize_with_roots_and_prefixes(0, "dir/dir/..", "dir/");
    test_canonicalize_with_roots_and_prefixes(0, "xxx/../../dir/xxx/../././file", "dir/file");

    //Flags
    test_path_normalize(PATH_FLAG_BACK_SLASH, "xxx/../../dir/xxx/../././file", "..\\dir\\file");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_NO_REMOVE_DOT, "xxx/./../dir/xxx/../././file", "xxx/dir/././file");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_NO_REMOVE_DOT_DOT, "xxx/./../dir/xxx/../././file", "xxx/../dir/xxx/../file");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_NO_REMOVE_DOT_DOT | PATH_FLAG_NO_REMOVE_DOT, "xxx/./../dir/xxx\\../.\\./file", "xxx/./../dir/xxx/../././file");

    //Transform to dir
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "", "");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, ".", "./");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "..", "../");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "dir/..", "./");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "file", "file/");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "file/", "file/");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "dir/file", "dir/file/");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_DIR, "dir/file/", "dir/file/");

    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, ".", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "dir/..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "file", "file/");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "file/", "file/");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "dir/file", "dir/file/");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_DIR, "dir/file/", "dir/file/");

    //Transform to file
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "", "");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, ".", "./");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "..", "../");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "dir/..", "./");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "file", "file");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "file/", "file");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "dir/file", "dir/file");
    test_path_normalize(PATH_FLAG_TRANSFORM_TO_FILE, "dir/file/", "dir/file");

    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, ".", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "dir/..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "file", "file");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "file/", "file");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "dir/file", "dir/file");
    test_canonicalize_with_roots_and_prefixes(PATH_FLAG_TRANSFORM_TO_FILE, "dir/file/", "dir/file");

    //Make absolute
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "", "", "");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "", ".", "./");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "", "..", "../");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "bye\\dir/", "hello\\.\\world/file.txt", "bye/dir/hello/world/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "D:/bye\\dir/", "hello\\.\\world/file.txt", "D:/bye/dir/hello/world/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "/bye\\dir/", "..\\hello/./world/file.txt", "/bye/hello/world/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "bye\\dir/", "C:/hello\\.\\world/file.txt", "C:/hello/world/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_ABSOLUTE, "D:/bye\\dir/", "C:/hello\\.\\world/file.txt", "C:/hello/world/file.txt");

    //Make relative
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "", "", "");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "C:/path/to/dir", "C:/path/to/world/file.txt", "../world/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "path/to/dir", "path/dir1/dir2/dir3/file.txt", "../../dir1/dir2/dir3/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "path/to/dir1/dir2/", "path/to/dir1/dir2/dir3/file.txt", "dir3/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "C:/path/to/dir1/dir2/", "C:/path/to/dir1/dir2/dir3/file.txt", "dir3/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "C:/path/to/dir", "path/to/world/file.txt", "path/to/world/file.txt");
    test_path_make_relative_absolute_with_prefixes(TEST_PATH_MAKE_RELATIVE, "path/to/dir", "C:/path/to/world/file.txt", "C:/path/to/world/file.txt");

    LOG_OKAY("PATH", "Done!");
    PROFILE_STOP();
}   

#endif