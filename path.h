#ifndef JOT_PATH
#define JOT_PATH

#include "string.h"
#include "log.h"

// This is a not exhaustive filepath handling facility. 
// We require all strings to pass through some basic parsing and be wrapped in Path struct. 
// This makes it easy to distinguish desiganted paths from any other strings. 
// Further we define Path_Builder which is guranteed to always be in invarinat form.
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
//  F - file_size
//  E - extension_size
//  M* - This / is explicitly not including in directory_size.
//       This is because non normalized directory paths can but dont have to end
//       on /. This makes sure that both cases have the same size.
// 
//
// All handling in this file respects the above categories and nothing more.
// Notably prefix is ignored in almost all operations but still is properly propagated.
//
// Path_Builder is in invariant form. The following algrohitm is used 
// (modified version of the one use std::filesystem::path. We respect "/" traling to denote directories. We also respect windows prefixes.)
// 1 - If the path is empty, stop (normal form of an empty path is an empty path).
// 2 - Replace each directory-separator (which may consist of multiple /) with a single /
// 3 - Replace each slash character in the root-name with / (but not in prefix which is left uncahnged!)
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
// 3) Includes only / 
// 4) Absolute paths do not contain any "." or ".." segments
// 5) Relative paths contain "." segments at the last position and ".." at the first position
//
//@NOTE: We attempt to cover a few edge cases and gain as much insigth into the
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

//@TODO: chnage categories to: Note that we dont even need to store
//       the info about root end. We never need to treat those segments any differently
//       from anything else. The only effect it has is on the directoryness.
//       Anyhow we dont need file_size and extension size.
//
//       Is asbolute: Yes needs to be present because is very non trivial to get
//       Is directory: Yes depends on root, trailing slash, .. and .
//       Is invariant: Yes can save us a lot of perf on common operations
//       has trailing slash: Not very necessary if we have segments_size
//
// //?/......C:/.....path/to/directory/........... 
// <-prefix-><-root-><---segments----><-trailing->
//              
// //?/......C:/.....path/to/directory/file.txt 
// <-prefix-><-root-><--------segments-------->
//                                     <-file->
//                                        <ext>

typedef struct Path_Info {
    i32 prefix_size;
    i32 root_content_from;
    i32 root_content_to;
    i32 root_size;
    i32 directories_size;
    i32 file_size;
    i32 extension_size;
    i32 segment_count;
    bool is_absolute;
    bool is_directory;
    bool is_invariant; 
    bool has_trailing_slash;
    Path_Root_Kind root_kind;

    //Denotes if is in the canonical representation
    // is only set for Path_Info from Path_Builder, but is still
    // useful since we use Path as the interface type and the information
    // of canonicity can save us some time.
} Path_Info;

typedef struct Path {
    String string;
    Path_Info info;
} Path;

typedef union Path_Builder {
    struct {
        String_Builder builder;
        Path_Info info;
    };

    struct {
        Allocator* allocator;
        isize capacity;
        String string;
    };
    
    struct {
        Allocator* _pad1;
        isize _pad2;
        Path path;
    };
} Path_Builder;

enum {
    PATH_APPEND_EVEN_WITH_ERROR = 1,
    PATH_APPEND_NO_CANONICALIZE = 2,
};
enum {
    PATH_CANONICALIZE_NO_REMOVE_DOT = 4,
    PATH_CANONICALIZE_NO_REMOVE_DOT_DOT = 8,
    PATH_CANONICALIZE_BACK_SLASH = 16,
    PATH_CANONICALIZE_TRANSFORM_TO_DIR = 32, 
    PATH_CANONICALIZE_TRANSFORM_TO_FILE = 64, 
    PATH_CANONICALIZE_NO_ROOT = 128,
    PATH_CANONICALIZE_NO_PREFIX = 256,
};
EXPORT Path   path_parse(String path);
EXPORT Path   path_parse_cstring(const char* path);
EXPORT String path_get_prefix(Path path);
EXPORT String path_get_root(Path path);
EXPORT String path_get_except_root(Path path);
EXPORT String path_get_except_prefix(Path path);
EXPORT String path_get_directories(Path path);
EXPORT String path_get_extension(Path path);
EXPORT String path_get_filename(Path path);
EXPORT String path_get_stem(Path path);
EXPORT bool   path_is_empty(Path path);
EXPORT Path   path_get_file_directory(Path path);

EXPORT Path_Builder path_builder_make(Allocator* alloc_or_null, isize initial_capacity_or_zero);
EXPORT const char*  path_builder_get_cstring(Path_Builder path);
EXPORT bool         path_builder_append(Path_Builder* builder, Path path);
EXPORT bool         path_builder_append_custom(Path_Builder* builder, Path path, int flags);
EXPORT void         path_builder_clear(Path_Builder* builder);
EXPORT void         path_canonicalize_in_place(Path_Builder* path, int flags);
EXPORT Path_Builder path_canonicalize(Allocator* alloc, Path path, int flags);
EXPORT Path_Builder path_concat(Allocator* alloc, Path a, Path b);
EXPORT Path_Builder path_concat_many(Allocator* alloc, const Path* paths, isize path_count);
EXPORT void         path_transform_to_file(Path_Builder* path);
EXPORT void         path_transform_to_directory(Path_Builder* path);
EXPORT void         path_make_relative_into(Path_Builder* into, Path relative_to, Path path);
EXPORT void         path_make_absolute_into(Path_Builder* into, Path relative_to, Path path);
EXPORT Path_Builder path_make_relative(Allocator* alloc, Path relative_to, Path path);
EXPORT Path_Builder path_make_absolute(Allocator* alloc, Path relative_to, Path path);

EXPORT Path path_get_executable();
EXPORT Path path_get_executable_directory();
EXPORT Path path_get_current_working_directory();

#endif

//@TEMP
#define JOT_ALL_IMPL
#define JOT_ALL_TEST

#if (defined(JOT_ALL_IMPL) || defined(JOT_PATH_IMPL)) && !defined(JOT_PATH_HAS_IMPL)
#define JOT_PATH_HAS_IMPL

EXPORT bool path_is_empty(Path path)
{
    return path.string.size <= path.info.prefix_size;
}

INTERNAL bool is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

isize string_find_first_path_separator(String string, isize from)
{
    for(isize i = from; i < string.size; i++)
        if(string.data[i] == '/' || string.data[i] == '\\')
            return i;

    return -1;
}

isize string_find_last_path_separator(String string, isize from)
{
    for(isize i = from; i-- > 0; )
        if(string.data[i] == '/' || string.data[i] == '\\')
            return i;

    return -1;
}

INTERNAL void _path_parse_root(String path, Path_Info* info)
{
    memset(info, 0, sizeof(info));
    String prefix_path = path;

    //https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    String win32_file_namespace = STRING("\\\\?\\");    // "\\?\"
    String win32_device_namespace = STRING("\\\\.\\");  // "\\.\"

    //Attempt to parse windows prefixes 
    if(string_is_prefixed_with(prefix_path, win32_file_namespace)) 
    {
        info->prefix_size = (i32) win32_file_namespace.size;
    }
    else if(string_is_prefixed_with(prefix_path, win32_device_namespace))
    {
        info->prefix_size = (i32) win32_device_namespace.size;
    }

    i32 root_from = info->prefix_size;
    String root_path = string_tail(prefix_path, info->prefix_size);
    if(root_path.size == 0)
    {
        info->is_absolute = false;
        info->is_invariant = true;
    }
    else
    {
        //Windows UNC server path //My_Root
        if(root_path.size >= 2 && is_path_sep(root_path.data[0]) && is_path_sep(root_path.data[1]))
        {
            if(root_path.size == 2)
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
                    info->root_content_to = (i32) root_path.size;
                    info->root_size = (i32) root_path.size;
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
        else if(root_path.size >= 1 && is_path_sep(root_path.data[0]))
        {
            info->root_content_from = root_from;
            info->root_content_to = root_from;
            info->is_absolute = true;
            info->root_size = 1;
            
            info->root_kind = PATH_ROOT_SLASH;
        }
        //Windows style root
        else if(root_path.size >= 2 && char_is_alphabetic(root_path.data[0]) && root_path.data[1] == ':')
        {
            info->root_content_from = root_from;
            info->root_content_to = root_from + 1;

            //In windows "C:some_file" means relative path on the drive C
            // while "C:/some_file" is absolute path starting from root C
            if(root_path.size >= 3 && is_path_sep(root_path.data[2]))
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
}

INTERNAL void _path_parse_rest(String path, Path_Info* info)
{
    //Clear the overriden
    info->is_directory = false;
    info->is_directory = false;
    info->directories_size = 0;
    info->file_size = 0;
    info->extension_size = 0;

    String root_path = string_tail(path, info->prefix_size);
    String directory_path = string_tail(path, info->prefix_size + info->root_size);

    if(root_path.size <= 0)
    {
        info->is_directory = true; //empty path is sometimes current directory. Thus is a directory
        info->is_invariant = true; //Empty path is invarinat
    }
    if(directory_path.size <= 0)
    {
        info->is_directory = true; //just root is considered a directory
    }
    else
    {
        //We consider path a directory path if it ends with slash. This incldues just "/" directory
        isize last = root_path.size - 1;
        ASSERT(last >= 0);
        info->is_directory = is_path_sep(root_path.data[last]);
        if(info->is_directory)
        {
            info->directories_size = (i32) directory_path.size - 1;
            info->has_trailing_slash = true;
        }
    }

    if(info->is_directory == false)
    {
        //Find the last directory segment
        isize file_i = 0;
        isize dir_i = string_find_last_path_separator(directory_path, directory_path.size);
        if(dir_i < 0)
            dir_i = 0;
        else
            file_i = dir_i + 1;
            

        info->directories_size = (i32) dir_i;

        //Parse filename
        String filename_path = string_safe_tail(directory_path, file_i);
        if(filename_path.size > 0)
        {
            //If is . or .. then is actually a directory
            if(string_is_equal(filename_path, STRING(".")) || string_is_equal(filename_path, STRING("..")))
            {
                info->is_directory = true;
                info->directories_size = (i32) directory_path.size;
            }
            else
            {
                //find the extension if any    
                isize dot_i = string_find_last_char(filename_path, '.');
                if(dot_i == -1)
                    dot_i = filename_path.size;
                else
                    dot_i += 1;

                info->file_size = (i32) filename_path.size;
                info->extension_size = (i32) (filename_path.size - dot_i);
            }
        }
    }
}

EXPORT Path path_parse(String path)
{
    Path out_path = {path};
    _path_parse_root(path, &out_path.info);
    _path_parse_rest(path, &out_path.info);
    return out_path;
}

typedef struct Path_Segement_Iterator {
    String segment;
    isize segment_number; //one based segment index
    isize segment_from;
    isize segment_to;
} Path_Segement_Iterator;

EXPORT bool path_segment_iterate_string(Path_Segement_Iterator* it, String path, isize till_root_size)
{
    isize segment_from = till_root_size;
    if(it->segment_number != 0)
        segment_from = it->segment_to + 1;

    if(segment_from >= path.size)
        return false;
        
    isize segment_to = string_find_first_path_separator(path, segment_from);
        
    if(segment_to == -1)
        segment_to = path.size;
        
    it->segment_number += 1;
    it->segment_from = segment_from;
    it->segment_to = segment_to;
    it->segment = string_range(path, segment_from, segment_to);

    return true;
}

EXPORT bool path_segment_iterate(Path_Segement_Iterator* it, Path path)
{
    return path_segment_iterate_string(it, path.string, path.info.prefix_size + path.info.root_size);
}

EXPORT Path_Builder path_builder_make(Allocator* alloc_or_null, isize initial_capacity_or_zero)
{
    Path_Builder builder = {builder_make(alloc_or_null, initial_capacity_or_zero)};
    return builder;
}

EXPORT void path_builder_clear(Path_Builder* builder)
{
    memset(&builder->info, 0, sizeof builder->info);
    builder_clear(&builder->builder);
}

EXPORT void path_builder_append(Path_Builder* into, Path path, int flags)
{
    //@NOTE: this function is the main normalization function. It expects 
    // into to be in a valid state.

    char slash = (flags & PATH_CANONICALIZE_BACK_SLASH) ? '\\' : '/';
    bool remove_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT) == 0;
    bool remove_dot_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT_DOT) == 0;
    bool transform_dir = (flags & PATH_CANONICALIZE_TRANSFORM_TO_DIR) > 0;
    bool transform_file = (flags & PATH_CANONICALIZE_TRANSFORM_TO_FILE) > 0;
    bool add_prefix = (flags & PATH_CANONICALIZE_NO_PREFIX) == 0;
    bool add_root = (flags & PATH_CANONICALIZE_NO_ROOT) == 0;
    
    bool make_directory = path.info.is_directory;
    if(transform_dir)
        make_directory = true;
    else if(transform_file)
        make_directory = false;

    // path_builder_clear(into);
    builder_reserve(&into->builder, path.string.size*9/8 + 5);
    
    #ifdef DO_ASSERTS_SLOW
    bool has_trailing_slash = false;
    String except_root = path_get_except_root(into->path);
    if(except_root.size > 0 && is_path_sep(except_root.data[except_root.size - 1]))
        has_trailing_slash = true;

    ASSERT(into->info.has_trailing_slash == has_trailing_slash);
    #endif

    if(has_trailing_slash)
    {
        ASSERT(into->info.segment_count > 0);
        builder_resize(&into->builder, into->builder.size - 1);        
        into->info.has_trailing_slash = false;
    }

    bool has_prefix = path_get_prefix(into->path).size > 0;
    bool was_empty = path_is_empty(into->path);
    if(add_prefix && was_empty && has_prefix == false)
    {
        String prefix = path_get_prefix(path);
        builder_append(&into->builder, prefix);
        into->info.prefix_size = prefix.size;
    }

    if(path_is_empty(path) == false)
    {
        if(add_root && was_empty)
        {
            String root_content = path_get_root_content(path);
            ASSERT(into->info.root_kind == PATH_ROOT_NONE);
            ASSERT(into->info.root_size == 0);
            ASSERT(into->info.root_content_from == 0);
            ASSERT(into->info.root_content_to == 0);

            isize before = into->builder.size;
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
                        into->info.root_content_from = into->builder.size;
                        builder_append(&into->builder, root_content);
                        into->info.root_content_to = into->builder.size;

                        builder_push(&into->builder, slash);
                    }
                    else
                        LOG_WARN("path", "Empty prefix' with PATH_ROOT_SERVER", string_escape_ephemeral(root_content));
                } break;

                case PATH_ROOT_WIN: {
                    char c = 'C';
                    if(root_content.size > 0 && char_is_alphabetic(root_content.data[0]))
                        c = root_content.data[0];
                    else
                        LOG_WARN("path", "Strange prefix '%s' with PATH_ROOT_WIN", string_escape_ephemeral(root_content));

                    //to uppercase
                    if('a' <= c && c <= 'z')
                        c = c - 'a' + 'A';

                    into->info.root_content_from = into->builder.size;
                    builder_push(&into->builder, c);
                    into->info.root_content_to = into->builder.size;

                    builder_push(&into->builder, ':');
                    if(path.info.is_absolute)
                        builder_push(&into->builder, slash);
                } break;

                case PATH_ROOT_UNKNOWN: {
                    into->info.root_content_from = into->builder.size;
                    builder_append(&into->builder, path_get_root(path));
                    into->info.root_content_to = into->builder.size;
                } break;
            }
            isize after = into->builder.size;
            if(path.info.root_kind != PATH_ROOT_NONE)
            {
                into->info.root_size = after - before;
                into->info.root_kind = path.info.root_kind;
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

        //@TODO: inline the loop and root_till!
        isize root_till = into->path.string.size - path_get_except_root(into->path).size;
        for(Path_Segement_Iterator it = {0}; path_segment_iterate(&it, path);)
        {
            bool push_segment = true;
            String segment = it.segment;
            //Multiple separators next to each otehr
            if(segment.size == 0)
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
                    isize last_segment_i = string_find_last_path_separator(into->string, into->string.size);
                    if(last_segment_i >= root_till)
                    {
                        String last_segement = string_tail(into->string, last_segment_i);
                        if(string_is_equal(last_segement, STRING("..")) == false)
                        {
                            builder_resize(&into->builder, last_segment_i);
                            into->info.segment_count -= 1;
                            push_segment = false;
                        }
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

        if(path_is_empty(into->path))
        {
            builder_push(&into->builder, '.');
            into->info.segment_count += 1;
        }

        if(make_directory && into->info.segment_count > 0)
            builder_push(&into->builder, slash);
    }

    _path_parse_rest(into->string, &into->info);
    path.info.is_invariant = true;

    #ifdef DO_ASSERTS_SLOW
    Path_Info new_info = path_parse(into->string).info;
    new_info.is_invariant = true;
    ASSERT(memcmp(&new_info, &into->info, sizeof(new_info)) == 0);
    #endif
}

EXPORT void path_builder_assign(Path_Builder* into, Path path, int flags)
{
    path_builder_clear(into);
    path_builder_append(into, path, flags);
}
EXPORT void path_normalize_in_place(Path_Builder* into, int flags)
{
    Arena arena = scratch_arena_acquire();
    String_Builder copy = builder_from_string(&arena.allocator, into->string);
    Path path = path_parse(copy.string);
    path_builder_assign(into, path, flags);
    arena_release(&arena);
}

EXPORT Path_Builder path_normalize(Allocator* alloc, Path path, int flags)
{
    Path_Builder builder = path_builder_make(alloc, 0);
    path_builder_append(&builder, path, flags);
    return builder;
}

EXPORT Path_Builder path_concat_many(Allocator* alloc, const Path* paths, isize path_count)
{
    //A simple heuristic to try to guess the needed capacity
    isize combined_cap = 10;
    for(isize i = 0; i < path_count; i++)
        combined_cap += paths->string.size*9/8;

    Path_Builder builder = path_builder_make(alloc, combined_cap);
    for(isize i = 0; i < path_count; i++)
        path_builder_append(&builder, paths[i], 0);
    
    return builder;
}

EXPORT Path_Builder path_concat(Allocator* alloc, Path a, Path b)
{
    Path paths[2] = {a, b};
    return path_concat_many(alloc, paths, 2);
}

//@TODO
EXPORT void path_transform_to_file(Path_Builder* path)
{
    path_canonicalize_in_place(path, PATH_CANONICALIZE_TRANSFORM_TO_FILE);
}
EXPORT void path_transform_to_directory(Path_Builder* path)
{
    path_canonicalize_in_place(path, PATH_CANONICALIZE_TRANSFORM_TO_DIR);
}

EXPORT Path path_parse_cstring(const char* path)
{
    return path_parse(string_make(path));
}

EXPORT String path_get_prefix(Path path)
{
    String out = string_head(path.string, path.info.prefix_size);
    return out;
}

EXPORT String path_get_root(Path path)
{
    String out = string_range(path.string, path.info.prefix_size, path.info.prefix_size + path.info.root_size);
    return out;
}

EXPORT String path_get_root_content(Path path)
{
    String out = string_range(path.string, path.info.root_content_from, path.info.root_content_to);
    return out;
}

EXPORT String path_get_except_prefix(Path path)
{
    String out = string_tail(path.string, path.info.prefix_size);
    return out;
}

EXPORT String path_get_except_root(Path path)
{
    String out = string_tail(path.string, path.info.prefix_size + path.info.root_size);
    return out;
}

EXPORT String path_get_directories(Path path)
{
    isize from = path.info.prefix_size + path.info.root_size;
    String out = string_range(path.string, from, from + path.info.directories_size);
    return out;
}

EXPORT String path_get_filename(Path path)
{
    String out = string_range(path.string, path.string.size - path.info.file_size, path.string.size);
    return out;
}

EXPORT Path path_get_file_directory(Path path)
{
    Path out = path;
    if(path.info.is_directory == false)
    {
        out.info.extension_size = 0;
        out.info.file_size = 0;
        out.info.is_directory = true;

        isize to = path.info.prefix_size + path.info.root_size + path.info.directories_size;
        //If possible include the '/'. This allows us to stay invarinat if our path is invariant.
        if(to < path.string.size) 
        {
            ASSERT(is_path_sep(path.string.data[to]));
            out.string = string_head(path.string, to + 1); 
        }
        else
        {
            out.string = string_head(path.string, to); 
            out.info.is_invariant = false; //Couldnt add the '/' making our path not invariant
        }
    }

    return out;
}

EXPORT String path_get_stem(Path path)
{
    String filename = path_get_filename(path);
    if(path.info.extension_size > 0)
        filename = string_head(filename, filename.size - path.info.extension_size - 1); 

    return filename;
}

EXPORT String path_get_extension(Path path)
{
    String out = string_range(path.string, path.string.size - path.info.extension_size, path.string.size);
    return out;
}

EXPORT Path path_get_executable()
{
    static bool was_parsed = false;
    static Path_Builder builder = {0};
    if(was_parsed == false)
    {
        Path path = path_parse_cstring(platform_get_executable_path());
        builder = path_canonicalize(allocator_get_static(), path, PATH_CANONICALIZE_TRANSFORM_TO_FILE);
    }

    return builder.path;
}

EXPORT Path path_get_executable_directory()
{
    static bool was_parsed = false;
    static Path_Builder builder = {0};
    if(was_parsed == false)
    {
        Path path = path_parse_cstring(platform_get_executable_path());
        Path dir = path_get_file_directory(path);
        builder = path_canonicalize(allocator_get_static(), dir, PATH_CANONICALIZE_TRANSFORM_TO_DIR);
    }

    return builder.path;
}

EXPORT Path path_get_current_working_directory()
{
    static Path_Builder cached = {0};
    static String_Builder last = {0};
    
    const char* cwd = platform_directory_get_current_working();
    if(last.data == NULL || strcmp(cwd, last.data) != 0)
    {
        if(last.allocator == NULL)
            builder_init(&last, allocator_get_static());

        String cwd_string = string_make(cwd);
        builder_assign(&last, cwd_string);

        path_builder_clear(&cached);
        path_builder_append(&cached, path_parse(cwd_string));
    }

    return cached.path;
}

EXPORT void path_make_relative_into(Path_Builder* into, Path relative_to, Path path)
{
    path_builder_clear(into);
    
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
        Arena arena = scratch_arena_acquire();
        
        //Make paths invariant if they are not invarinat already. 
        // It is very likely that at least relative_to will be invarinat since 
        // most often it will be a path to the current executable which is cached
        // in invariant form.
        Path reli = path_get_file_directory(relative_to);
        Path pathi = path;

        Path_Builder reli_builder = {0}; 
        Path_Builder pathi_builder = {0}; 
        if(relative_to.info.is_invariant == false)
        {
            reli_builder = path_canonicalize(&arena.allocator, relative_to, 0); 
            reli = reli_builder.path;
        }
        if(path.info.is_invariant == false)
        {
            pathi_builder = path_canonicalize(&arena.allocator, path, 0); 
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
                path_builder_append(into, path_parse(path_get_prefix(pathi)));

                //If both are present and same do nothing
                if(has_rel && has_path && are_equal)
                {
                    
                }
                //If they were same and end the same then also do nothing
                else if(has_rel == false && has_path == false && are_equal)
                {
                    path_builder_append(into, path_parse_cstring("."));
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
                    // we add appropriate ammount of ".." segments then the rest of the path
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
                    
                    path_normalize_in_place(into, path.info.is_directory ? PATH_CANONICALIZE_TRANSFORM_TO_DIR : PATH_CANONICALIZE_TRANSFORM_TO_FILE);
                    break;
                }
            }
        }

        arena_release(&arena);
    }
}

EXPORT void path_make_absolute_into(Path_Builder* into, Path relative_to, Path path)
{
    path_builder_clear(into);
    if(path.info.is_absolute == false)
        path_builder_append(into, relative_to);
    path_builder_append(into, path);
}
EXPORT Path_Builder path_make_relative(Allocator* alloc, Path relative_to, Path path)
{
    Path_Builder out = path_builder_make(alloc, 0);
    path_make_relative_into(&out, relative_to, path);
    return out;
}
EXPORT Path_Builder path_make_absolute(Allocator* alloc, Path relative_to, Path path)
{
    Path_Builder out = path_builder_make(alloc, 0);
    path_make_absolute_into(&out, relative_to, path);
    return out;
}

#endif

#if (defined(JOT_ALL_TEST) || defined(JOT_PATH_TEST)) && !defined(JOT_PATH_HAS_TEST)
#define JOT_PATH_HAS_TEST

#define TEST_STRING_EQ(str1, str2) TEST(string_is_equal(str1, str2), "\"" STRING_FMT "\" == \"" STRING_FMT "\"", STRING_PRINT(str1), STRING_PRINT(str2))

enum {
    PATH_IS_DIR = 1,
    PATH_IS_EMPTY = 2,
    PATH_IS_ABSOLUTE = 4,
};

void test_single_path(const char* path, const char* prefix, const char* root, const char* directories, const char* filename, const char* extension, int flags)
{
    String _path = string_make(path);
    Path parsed = path_parse(_path);
    String _prefix = path_get_prefix(parsed);
    String _root = path_get_root(parsed);
    String _directories = path_get_directories(parsed);
    String _filename = path_get_filename(parsed);
    String _extension = path_get_extension(parsed);
    
    TEST_STRING_EQ(_prefix, string_make(prefix));
    TEST_STRING_EQ(_root, string_make(root));
    TEST_STRING_EQ(_directories, string_make(directories));
    TEST_STRING_EQ(_filename, string_make(filename));
    TEST_STRING_EQ(_extension, string_make(extension));

    TEST(parsed.info.is_absolute == (flags & PATH_IS_ABSOLUTE) > 0);
    TEST(parsed.info.is_directory == (flags & PATH_IS_DIR) > 0);
}

void test_path_normalize(int flags, const char* cpath, const char* cexpected)
{
    const char* prefixes[] = {"", "\\\\?\\", "\\\\.\\"};

    for(isize i = 0; i < STATIC_ARRAY_SIZE(prefixes); i++)
    {
        Arena arena = scratch_arena_acquire();
        String_Builder prefixed_path = string_concat(&arena.allocator, string_make(prefixes[i]), string_make(cpath));
        String_Builder prefixed_expected = string_concat(&arena.allocator, string_make(prefixes[i]), string_make(cexpected));

        Path path = path_parse(prefixed_path.string);
        Path expected = path_parse(prefixed_expected.string);

        String_Builder canonical = path_normalize(&arena.allocator, path, flags);
        TEST_STRING_EQ(canonical.string, expected.string);
        arena_release(&arena);
    }
}

void test_canonicalize_with_roots_and_prefixes(int flags, const char* cabs_path, const char* cexpected)
{
    const char* roots[]      = {"\\", "C:/", "F:\\", "//Server/", "\\\\xxserverxx\\"};
    const char* norm_roots[] = {"/", "C:/", "F:/", "//Server/", "//xxserverxx/"};
    
    for(isize i = 0; i < STATIC_ARRAY_SIZE(roots); i++)
    {
        Arena arena = scratch_arena_acquire();
        String_Builder prefixed_path = string_concat(&arena.allocator, string_make(roots[i]), string_make(cabs_path));
        String_Builder prefixed_expected = string_concat(&arena.allocator, string_make(norm_roots[i]), string_make(cexpected));

        test_path_normalize(flags, prefixed_path.data, prefixed_expected.data);
        arena_release(&arena);
    }
}

enum {
    TEST_MAKE_RELATIVE,
    TEST_MAKE_ABSOLUTE,
};

void test_make_relative_absolute_with_prefixes(int flags, const char* crelative, const char* cpath, const char* cexpected)
{
    const char* prefixes[] = {"", "\\\\?\\", "\\\\.\\"};
    for(isize i = 0; i < STATIC_ARRAY_SIZE(prefixes); i++)
    {
        Arena arena = scratch_arena_acquire();

        String_Builder prefixed_relative = string_concat(&arena.allocator, string_make(prefixes[i]), string_make(crelative));
        String_Builder prefixed_path = string_concat(&arena.allocator, string_make(prefixes[i]), string_make(cpath));
        String_Builder prefixed_expected = string_concat(&arena.allocator, string_make(prefixes[i]), string_make(cexpected));

        Path relative = path_parse(prefixed_relative.string);
        Path path = path_parse(prefixed_path.string);
        Path expected = path_parse(prefixed_expected.string);

        String_Builder transformed = {0};
        if(flags == TEST_MAKE_RELATIVE)
            transformed = _path_make_relative(&arena.allocator, relative, path);
        else
            transformed = _path_make_absolute(&arena.allocator, relative, path);

        TEST_STRING_EQ(transformed.string, expected.string);
        int k = 0; (void) k;

        arena_release(&arena);
    }
}

void test_path()
{
    test_single_path("", "", "", "", "", "", PATH_IS_DIR);
    test_single_path(".", "", "", ".", "", "", PATH_IS_DIR);
    test_single_path("..", "", "", "..", "", "", PATH_IS_DIR);
    test_single_path("./", "", "", ".", "", "", PATH_IS_DIR);
    test_single_path("../", "", "", "..", "", "", PATH_IS_DIR);

    test_single_path("file.txt", "", "", "", "file.txt", "txt", 0);
    test_single_path("C:/my/files/file.txt", "", "C:/", "my/files", "file.txt", "txt", PATH_IS_ABSOLUTE);
    test_single_path("/my/files/file/", "", "/", "my/files/file", "", "", PATH_IS_ABSOLUTE | PATH_IS_DIR);
    test_single_path("my/files/file", "", "", "my/files", "file", "", 0);
    test_single_path("\\\\?\\C:my/files/file", "\\\\?\\", "C:", "my/files", "file", "", 0);
    test_single_path("//Server/my/files/.gitignore", "", "//Server/", "my/files", ".gitignore", "gitignore", PATH_IS_ABSOLUTE);

    //Relative
    test_path_normalize(0, "", "./");
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
    test_path_normalize(PATH_CANONICALIZE_BACK_SLASH, "xxx/../../dir/xxx/../././file", "..\\dir\\file");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_NO_REMOVE_DOT, "xxx/./../dir/xxx/../././file", "xxx/dir/././file");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_NO_REMOVE_DOT_DOT, "xxx/./../dir/xxx/../././file", "xxx/../dir/xxx/../file");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_NO_REMOVE_DOT_DOT | PATH_CANONICALIZE_NO_REMOVE_DOT, "xxx/./../dir/xxx\\../.\\./file", "xxx/./../dir/xxx/../././file");

    #if 1
    //Transform to dir
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "", "./");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, ".", "./");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "..", "../");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "dir/..", "./");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "file", "file/");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "file/", "file/");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "dir/file", "dir/file/");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "dir/file/", "dir/file/");

    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, ".", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "dir/..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "file", "file/");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "file/", "file/");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "dir/file", "dir/file/");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_DIR, "dir/file/", "dir/file/");

    //Transform to file
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "", ".");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, ".", ".");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "..", "..");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "dir/..", ".");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "file", "file");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "file/", "file");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "dir/file", "dir/file");
    test_path_normalize(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "dir/file/", "dir/file");

    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, ".", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "dir/..", "");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "file", "file");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "file/", "file");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "dir/file", "dir/file");
    test_canonicalize_with_roots_and_prefixes(PATH_CANONICALIZE_TRANSFORM_TO_FILE, "dir/file/", "dir/file");
    #endif

    //Make absolute
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "", "", "./");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "", ".", "./");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "", "..", "../");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "bye\\dir/", "hello\\.\\world/file.txt", "bye/dir/hello/world/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "D:/bye\\dir/", "hello\\.\\world/file.txt", "D:/bye/dir/hello/world/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "/bye\\dir/", "..\\hello/./world/file.txt", "/bye/hello/world/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "bye\\dir/", "C:/hello\\.\\world/file.txt", "C:/hello/world/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_ABSOLUTE, "D:/bye\\dir/", "C:/hello\\.\\world/file.txt", "C:/hello/world/file.txt");

    //Make relative
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "", "", "./");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "C:/path/to/dir", "C:/path/to/world/file.txt", "../world/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "path/to/dir", "path/dir1/dir2/dir3/file.txt", "../../dir1/dir2/dir3/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "path/to/dir1/dir2/", "path/to/dir1/dir2/dir3/file.txt", "dir3/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "C:/path/to/dir1/dir2/", "C:/path/to/dir1/dir2/dir3/file.txt", "dir3/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "C:/path/to/dir", "path/to/world/file.txt", "path/to/world/file.txt");
    test_make_relative_absolute_with_prefixes(TEST_MAKE_RELATIVE, "path/to/dir", "C:/path/to/world/file.txt", "C:/path/to/world/file.txt");

    LOG_OKAY("PATH", "Done!");
    exit(0);
}   

#endif