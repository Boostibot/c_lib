#ifndef JOT_PATH
#define JOT_PATH

#include "string.h"

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

INTERNAL void path_parse_prefix(String path, Path_Info* info)
{
    String prefix_path = path;

    //https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    String win32_file_namespace = STRING("\\\\?\\");    // "\\?\"
    String win32_device_namespace = STRING("\\\\.\\");  // "\\.\"

    info->prefix_size = 0;
    //Attempt to parse windows prefixes 
    if(string_is_prefixed_with(prefix_path, win32_file_namespace)) 
    {
        info->prefix_size = (i32) win32_file_namespace.size;
    }
    else if(string_is_prefixed_with(prefix_path, win32_device_namespace))
    {
        info->prefix_size = (i32) win32_device_namespace.size;
    }
}


INTERNAL void _path_parse_rest(String path, Path_Info* info)
{
    //Clear the overriden
    info->is_directory = false;
    info->directories_size = 0;
    info->file_size = 0;
    info->extension_size = 0;

    String root_path = string_tail(path, info->prefix_size);
    String directory_path = string_tail(path, info->prefix_size + info->root_size);

    if(root_path.size <= 0)
        info->is_directory = true; //empty path is sometimes current directory. Thus is a directory
    if(directory_path.size <= 0)
        info->is_directory = true; //just root is considered a directory
    else
    {
        //We consider path a directory path if it ends with slash. This incldues just "/" directory
        isize last = root_path.size - 1;
        ASSERT(last >= 0);
        info->is_directory = is_path_sep(root_path.data[last]);
        if(info->is_directory)
            info->directories_size = (i32) directory_path.size - 1;
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

enum {
    //This is an internal flag. It essentially skips the enire function
    // and only performs transformation of file to dir and vice versa.
    //Is used to implement the coresponding functions.
    _PATH_CANONICALIZE_TRANSFORM_DIR_FILE_ONLY = 1 << 30
};

EXPORT void path_canonicalize_in_place(Path_Builder* path, int flags)
{
    char slash = (flags & PATH_CANONICALIZE_BACK_SLASH) ? '\\' : '/';
    bool remove_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT) == 0;
    bool remove_dot_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT_DOT) == 0;
    bool transform_dir = (flags & PATH_CANONICALIZE_TRANSFORM_TO_DIR) > 0;
    bool transform_file = (flags & PATH_CANONICALIZE_TRANSFORM_TO_FILE) > 0;
    bool only_transform = (flags & _PATH_CANONICALIZE_TRANSFORM_DIR_FILE_ONLY) > 0;

    if(only_transform == false)
        path->info = path_parse(path->string).info;

    isize till_content = path->info.prefix_size + path->info.root_size;
    String rest = path_get_except_root(path->path);

    bool make_directory = path->info.is_directory;
    if(transform_dir)
        make_directory = true;
    else if(transform_file)
        make_directory = false;
        
    isize segment_count = 0;
    if(path_is_empty(path->path) == false)
    {
        {
            isize except_prefix_size = path->string.size - path->info.prefix_size;
            char* data = path->builder.data + path->info.prefix_size;
            for(isize i = 0; i < except_prefix_size; i++)
                if(data[i] == '\\')
                    data[i] = slash;
        }
        
        if(rest.size > 0 && only_transform == false)
        {
            isize read_i = 0;
            isize write_i = 0;
            char* rest_data = (char*) rest.data;

            for(isize i_next = 0; read_i < rest.size; read_i = i_next + 1)
            {
                ASSERT(write_i <= read_i);

                i_next = string_find_first_path_separator(rest, read_i);
                if(i_next == -1)
                    i_next = rest.size;

                String segment = string_range(rest, read_i, i_next);
                //Multiple separators next to each otehr
                if(segment.size == 0)
                {}
                //Single dot segment
                else if(remove_dot && string_is_equal(segment, STRING(".")))
                {}
                //pop segment
                else if(remove_dot_dot && string_is_equal(segment, STRING("..")))
                {
                    //If there was no segment to pop push the ".." segment
                    if(segment_count <= 0)
                    {
                        if(path->info.is_absolute == false)
                        {
                            String back = STRING("..");
                            CHECK_BOUNDS(back.size - 1, rest.size);
                            memmove(rest_data, back.data, back.size);
                            write_i = back.size;
                            segment_count = 1;
                        }
                        else
                        {
                            write_i = 0;
                            segment_count = 0;
                        }
                    }
                    //else trim to the last separator
                    else
                    {
                        write_i = string_find_last_path_separator(rest, write_i);
                        if(write_i == -1)
                            write_i = 0;
                        segment_count -= 1;
                    }
                }
                //push segment 
                else
                {
                    if(segment_count != 0)
                    {
                        CHECK_BOUNDS(write_i, rest.size);
                        rest_data[write_i++] = slash;
                    }
                        
                    CHECK_BOUNDS(write_i + segment.size - 1, rest.size);
                    memmove(rest_data + write_i, rest_data + read_i, segment.size);
                    write_i += segment.size;
                    segment_count += 1;
                }
            }
            
            //Trim the builder to the final write pos
            builder_resize(&path->builder, till_content + write_i);
        }

        ASSERT(path->builder.size >= path->info.prefix_size);
        if(path->builder.size == path->info.prefix_size)
            builder_push(&path->builder, '.');

        //We require all normal type directories to be '/' terminated even in root!
        bool is_directory = is_path_sep(path->builder.data[path->builder.size - 1]); 

        //If desired path is directory but we arent
        if(make_directory)
        {
            if(is_directory == false)
            {
                ASSERT(path->string.size > 0);
                builder_push(&path->builder, slash);
            }
        }
        //If desired path is file but we are a directory
        else
        {
            if(is_directory)
            {
                //if its just root then there is nothing we can do
                if(path->string.size > till_content)
                    builder_pop(&path->builder);
            }
        }

        _path_parse_rest(path->string, &path->info);
    }
    
    path->info.segment_count = (i32) segment_count;
    path->info.is_invariant = true;
}

EXPORT bool path_builder_append_custom(Path_Builder* builder, Path path, int flags)
{
    if(path.string.size == 0)
        return true;
    
    bool state = true;
    //If is completely empty
    if(builder->string.size == 0)
    {
        builder_assign(&builder->builder, path.string);
        builder->info = path.info;
    }
    //If has just prefix (unusual)
    else if(path_is_empty(builder->path))
    {
        String not_prefix = string_tail(path.string, path.info.prefix_size);
        builder_append(&builder->builder, not_prefix);
        
        if(flags & PATH_APPEND_NO_CANONICALIZE)
            builder->info = path_parse(builder->string).info;
    }
    //Concatenate
    else
    {
        //Cannot concatenate two absolute paths!
        if(builder->info.is_absolute && path.info.is_absolute)
            state = false;

        if(state || (flags & PATH_APPEND_EVEN_WITH_ERROR))
        {
            String not_root = path_get_except_root(path);
            String not_root_builder = path_get_except_root(builder->path);
            if(not_root_builder.size > 0)
                builder_push(&builder->builder, '/');
                
            builder_append(&builder->builder, not_root);
        }
        
        if(flags & PATH_APPEND_NO_CANONICALIZE)
            _path_parse_rest(builder->string, &builder->info);
    }

    if((flags & PATH_APPEND_NO_CANONICALIZE) == false)
        path_canonicalize_in_place(builder, flags);

    return state;
}

EXPORT bool path_builder_append(Path_Builder* builder, Path path)
{
    return path_builder_append_custom(builder, path, 0);
}

EXPORT Path_Builder path_canonicalize(Allocator* alloc, Path path, int flags)
{
    Path_Builder builder = path_builder_make(alloc, 0);
    path_builder_append_custom(&builder, path, flags & ~PATH_APPEND_NO_CANONICALIZE);
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
        path_builder_append_custom(&builder, paths[i], PATH_APPEND_NO_CANONICALIZE | PATH_APPEND_EVEN_WITH_ERROR);

    path_canonicalize_in_place(&builder, 0);
    return builder;
}

EXPORT Path_Builder path_concat(Allocator* alloc, Path a, Path b)
{
    Path paths[2] = {a, b};
    return path_concat_many(alloc, paths, 2);
}

EXPORT const char* path_builder_get_cstring(Path_Builder path)
{
    return cstring_escape(path.string.data);
}

EXPORT void path_transform_to_file(Path_Builder* path)
{
    path_canonicalize_in_place(path, PATH_CANONICALIZE_TRANSFORM_TO_FILE | _PATH_CANONICALIZE_TRANSFORM_DIR_FILE_ONLY);
}
EXPORT void path_transform_to_directory(Path_Builder* path)
{
    path_canonicalize_in_place(path, PATH_CANONICALIZE_TRANSFORM_TO_DIR | _PATH_CANONICALIZE_TRANSFORM_DIR_FILE_ONLY);
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
    isize to = path.info.prefix_size + path.info.root_size + path.info.directories_size;

    Path out = {0};
    out.info = path.info;
    out.info.extension_size = 0;
    out.info.file_size = 0;
    out.info.is_directory = true;

    //If possible include the '/'. This allows us to stay invarinat if our path is invariant.
    if(to < path.string.size) 
    {
        ASSERT(is_path_sep(path.string.data[to]));
        out.string = string_head(path.string, to + 1); 
    }
    else
        out.string = string_head(path.string, to); 
        
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

EXPORT void path_builder_assign(Path_Builder* into, Path path)
{
    path_builder_clear(into);
    path_builder_append(into, path);
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
        path_builder_assign(into, path);
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

        String rel_root = path_get_root(reli);
        String path_root = path_get_root(pathi);

        //If roots differ we cannot make it more relative
        if(string_is_equal(rel_root, path_root) == false)
            path_builder_assign(into, path);
        else
        {
            //C:/the/base/path/
            // 
            //C:/the/file.txt                 -> ../../file.txt
            //C:/the/base/path/next/file.txt  -> next/file.txt
            
            //the/base/path/
            // 
            //the/file.txt                 -> ../../file.txt
            //the/base/path/next/file.txt  -> next/file.txt

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
                    int transform_to = path.info.is_directory ? PATH_CANONICALIZE_TRANSFORM_TO_DIR : PATH_CANONICALIZE_TRANSFORM_TO_FILE;
                    
                    //@TODO: I dont really know if direct append is safe to do. Safer would be to 
                    //       use path_builder_append() always. For the moment we assert equality.
                    //       We will want to revisit this in the future if there are no problems and use the faster approach
                    
                    //If rel is shorter than path add all remainig segments of `path` into `into`
                    if(has_rel == false)
                    {
                        #ifdef DO_ASSERTS_SLOW
                            Path_Builder control = path_builder_make(&arena.allocator, 0); 
                            path_builder_append(&control, path_parse(path_get_prefix(pathi)));
                            path_builder_append(&control, path_parse(path_it.segment));
                        #endif

                        builder_append(&into->builder, path_it.segment);
                        while(path_segment_iterate(&path_it, pathi))
                        {
                            #ifdef DO_ASSERTS_SLOW
                                path_builder_append(&control, path_parse(path_it.segment));
                            #endif

                            builder_push(&into->builder, '/');
                            builder_append(&into->builder, path_it.segment);
                        }

                        path_canonicalize_in_place(into, transform_to);
                        
                        #ifdef DO_ASSERTS_SLOW
                            if(path.info.is_directory)
                                path_transform_to_directory(&control);
                            else
                                path_transform_to_file(&control);

                            ASSERT_SLOW(string_is_equal(control.string, into->string), "Control must be equal!");
                        #endif
                    }
                    //If there was a difference in the path or path is shorter
                    // we add appropriate ammount of ".." segments then the rest of the path
                    else
                    {
                        #ifdef DO_ASSERTS_SLOW
                            Path_Builder control = path_builder_make(&arena.allocator, 0); 
                            path_builder_append(&control, path_parse(path_get_prefix(pathi)));
                            path_builder_append(&control, path_parse(STRING("..")));
                        #endif

                        builder_append(&into->builder, STRING(".."));
                        while(path_segment_iterate(&rel_it, reli))
                        {
                            #ifdef DO_ASSERTS_SLOW
                                path_builder_append(&control, path_parse(STRING("..")));
                            #endif

                            builder_append(&into->builder, STRING("/.."));
                        }
                            
                        #ifdef DO_ASSERTS_SLOW
                            path_builder_append(&control, path_parse(path_it.segment));
                        #endif

                        builder_append(&into->builder, STRING("/"));
                        builder_append(&into->builder, path_it.segment);
                        while(path_segment_iterate(&path_it, pathi))
                        {
                            #ifdef DO_ASSERTS_SLOW
                                path_builder_append(&control, path_parse(path_it.segment));
                            #endif

                            builder_push(&into->builder, '/');
                            builder_append(&into->builder, path_it.segment);
                        }
                        
                        path_canonicalize_in_place(into, transform_to);

                        #ifdef DO_ASSERTS_SLOW
                            if(path.info.is_directory)
                                path_transform_to_directory(&control);
                            else
                                path_transform_to_file(&control);

                            ASSERT_SLOW(string_is_equal(control.string, into->string), "Control must be equal! '%s' != '%s' ", control.builder.data, into->builder.data);
                        #endif
                    }

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
        path_builder_append_custom(into, relative_to, PATH_APPEND_NO_CANONICALIZE | PATH_APPEND_EVEN_WITH_ERROR);
    path_builder_append_custom(into, path, PATH_APPEND_NO_CANONICALIZE | PATH_APPEND_EVEN_WITH_ERROR);
    path_canonicalize_in_place(into, 0);
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

#pragma warning(disable:4200) //nonstandard extension used: zero-sized array in struct/union
typedef struct Path_Segment {
    struct Path_Segment* prev;
    struct Path_Segment* next;
    isize size;
    char data[];
} Path_Segment;

typedef struct Path_List {
    Allocator* alloc;

    String prefix;
    String root;

    Path_Segment* segment_first;
    Path_Segment* segment_last;
    isize segment_count;
    
    bool is_directory;
    bool is_absolute;
    Path_Root_Kind root_kind;
} Path_List;

#include "list.h"

String path_segment_string(const Path_Segment* segment)
{
    if(segment == NULL)
        return STRING("");

    String out = {segment->data, segment->size};
    return out;
}

void path_segment_deallocate(Path_Segment* segment_or_null, Allocator* alloc)
{
    isize needed_size = sizeof(Path_Segment) + segment_or_null->size + 1;
    allocator_deallocate(alloc, segment_or_null, needed_size, DEF_ALIGN);
}

Path_Segment* path_segment_allocate(Allocator* alloc, isize size)
{
    isize needed_size = sizeof(Path_Segment) + size + 1;
    Path_Segment* out = (Path_Segment*) allocator_allocate(alloc, needed_size, DEF_ALIGN);
    memset(out, 0, needed_size);
    out->size = size;
    return out;
}

Path_Segment* path_segment_allocate_with(Allocator* alloc, String data)
{
    Path_Segment* out = path_segment_allocate(alloc, data.size);
    memcpy(out->data, data.data, data.size);
    return out;
}

void path_list_pop(Path_List* path_list)
{
    if(path_list->segment_last)
    {
        path_list->segment_count -= 1;
        Path_Segment* popped = path_list->segment_last;
        bilist_pop_back(&path_list->segment_first, &path_list->segment_last); 
        path_segment_deallocate(popped, path_list->alloc);
    }
}

//Is unsafe because does not respect normal segments so we can push a segemnt for exmaple
// containing root thus resulting in "path/to/file//?/C:/" which is very much invalid
void path_list_push_unsafe(Path_List* path_list, String segment, int flags)
{
    bool remove_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT) == 0;
    bool remove_dot_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT_DOT) == 0;

    bool push_segment = true;
    if(segment.size == 0)
    {
        push_segment = false;
    }
    else if(remove_dot && string_is_equal(segment, STRING(".")))
    {
        push_segment = false;
    }
    else if(remove_dot_dot && string_is_equal(segment, STRING("..")))
    {
        //If is absolute we never output ".." segment
        //If is relative we output ".." when is first such segment
        // or the one before was also ".." 
        // (and thus by induction all before are ".." as well).
        // In all both we default to output and only not output it 
        // if a previous segment was successfully removed
        if(path_list->is_absolute)
            push_segment = false;
        else
            push_segment = true;

        //Only pop the last segment if there is something to pop and the previous one was ".."
        if(path_list->segment_last && string_is_equal(path_segment_string(path_list->segment_last), STRING("..")) == false)
        {
            path_list_pop(path_list);
            push_segment = false;
        }
    }

    if(push_segment)
    {
        path_list->alloc = allocator_or_default(path_list->alloc);
        path_list->segment_count += 1;
        Path_Segment* segment_node = path_segment_allocate_with(path_list->alloc, segment);
        bilist_push_back(&path_list->segment_first, &path_list->segment_last, segment_node);
    }
}

bool path_list_append(Path_List* path_list, Path path, int flags)
{
    bool keep_root = (flags & PATH_CANONICALIZE_NO_ROOT) == 0;
    bool keep_prefix = (flags & PATH_CANONICALIZE_NO_PREFIX) == 0;

    bool transform_file = (flags & PATH_CANONICALIZE_TRANSFORM_TO_FILE) > 0;
    bool transform_dir = (flags & PATH_CANONICALIZE_TRANSFORM_TO_DIR) > 0;
    bool make_directory = path.info.is_directory;
    if(transform_dir)
        make_directory = true;
    else if(transform_file)
        make_directory = false;

    //cannot apped 
    bool state = true;
    if(keep_root && path_list->is_absolute == false && path.info.is_absolute == true)
        state = false;

    path_list->alloc = allocator_or_default(path_list->alloc);
    if(keep_prefix && path_list->prefix.size == 0 && path.info.prefix_size > 0)
        path_list->prefix = builder_from_string(path_list->alloc, path_get_prefix(path)).string;

    if(keep_root && path_list->root_kind == PATH_ROOT_NONE && path.info.root_kind != PATH_ROOT_NONE)
    {
        path_list->root = builder_from_string(path_list->alloc, path_get_root_content(path)).string;
        path_list->root_kind = path.info.root_kind;
        path_list->is_absolute = path.info.is_absolute;
    }

    path_list->is_directory = make_directory;

    for(Path_Segement_Iterator it = {0}; path_segment_iterate(&it, path); )
        path_list_push_unsafe(path_list, it.segment, flags);

    return state;
}

void path_list_push(Path_List* path_list, String segment)
{
    Path path = path_parse(segment);
    path_list_append(path_list, path, PATH_CANONICALIZE_NO_ROOT | PATH_CANONICALIZE_NO_PREFIX);
}

void path_list_normalize(Path_List* path_list, int flags)
{
    bool remove_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT) == 0;
    bool remove_dot_dot = (flags & PATH_CANONICALIZE_NO_REMOVE_DOT_DOT) == 0;
    
    bool transform_file = (flags & PATH_CANONICALIZE_TRANSFORM_TO_FILE) > 0;
    bool transform_dir = (flags & PATH_CANONICALIZE_TRANSFORM_TO_DIR) > 0;
    if(transform_dir)
        path_list->is_directory = true;
    else if(transform_file)
        path_list->is_directory = false;

    for(Path_Segment* curr = path_list->segment_first; curr != NULL;)
    {
        Path_Segment* next = curr->next;
        Path_Segment* prev = curr->prev;
        String segment = path_segment_string(curr);

        bool pop_curr = false;
        bool pop_prev = false;
        if(segment.size == 0)
            pop_curr = true;
        else if(remove_dot && string_is_equal(segment, STRING(".")))
            pop_curr = true;
        else if(remove_dot_dot && string_is_equal(segment, STRING("..")))
        {
            if(path_list->is_absolute)
                pop_curr = true;
            else
                pop_curr = false;

            //Only pop the last segment if there is something to pop and the previous one was ".."
            if(prev && string_is_equal(path_segment_string(prev), STRING("..")) == false)
            {
                pop_curr = true;
                pop_prev = true;
            }
        }

        if(pop_curr)
        {
            path_list->segment_count -= 1;
            bilist_remove(&path_list->segment_first, &path_list->segment_first, curr); 
            path_segment_deallocate(curr, path_list->alloc);
        }
        
        if(pop_prev)
        {
            path_list->segment_count -= 1;
            bilist_remove(&path_list->segment_first, &path_list->segment_first, prev); 
            path_segment_deallocate(prev, path_list->alloc);
        }

        curr = next;
    }
}

void path_list_to_string(String_Builder* into, Path_List path_list, int flags)
{
    builder_clear(into);

    char slash = flags & PATH_CANONICALIZE_BACK_SLASH ? '\\' : '/';
    builder_append(into, path_list.prefix);
    switch(path_list.root_kind)
    {
        case PATH_ROOT_NONE : {} break;

        case PATH_ROOT_SLASH: {
            builder_push(into, slash);
        } break;
        case PATH_ROOT_SLASH_SLASH: {
            builder_push(into, slash);
            builder_push(into, slash);
        } break;
        case PATH_ROOT_SERVER: {
            builder_push(into, slash);
            builder_push(into, slash);
            if(path_list.root.size > 0)
            {
                builder_append(into, path_list.root);
                builder_push(into, slash);
            }
            else
                LOG_WARN("path", "Empty prefix' with PATH_ROOT_SERVER", string_escape_ephemeral(path_list.root));
        } break;

        case PATH_ROOT_WIN: {
            char c = 'C';
            if(path_list.root.size > 0 && char_is_alphabetic(path_list.root.data[0]))
                c = path_list.root.data[0];
            else
                LOG_WARN("path", "Strange prefix '%s' with PATH_ROOT_WIN", string_escape_ephemeral(path_list.root));

            //to uppercase
            if('a' <= c && c <= 'z')
                c = c - 'a' + 'A';

            builder_push(into, c);
            builder_push(into, ':');
            if(path_list.is_absolute)
                builder_push(into, slash);
        } break;

        case PATH_ROOT_UNKNOWN: {
            builder_append(into, path_list.root);
        } break;
    }
    
    isize written_segments = 0;
    for(Path_Segment* curr = path_list.segment_first; curr != NULL; curr = curr->next)
    {
        if(curr->size == 0)
            continue;

        if(written_segments != 0)
            builder_push(into, slash);

        builder_append(into, path_segment_string(curr));
        written_segments += 1;
    }

    if(path_list.is_absolute == false && written_segments == 0)
    {
        builder_push(into, '.');
        written_segments += 1;
    }
    
    bool transform_file = (flags & PATH_CANONICALIZE_TRANSFORM_TO_FILE) > 0;
    bool transform_dir = (flags & PATH_CANONICALIZE_TRANSFORM_TO_DIR) > 0;
    bool make_directory = path_list.is_directory;
    if(transform_dir)
        make_directory = true;
    else if(transform_file)
        make_directory = false;

    if(make_directory && written_segments != 0)
        builder_push(into, slash);
}

Path_List path_list_make(Allocator* alloc_or_null, Path path, int flags)
{
    Path_List out = {alloc_or_null};
    path_list_append(&out, path, flags);
    return out;
}
Path_List path_list_duplicate(Allocator* alloc_or_null, Path_List list)
{
    Allocator* alloc = allocator_or_default(alloc_or_null);
    Path_List out = {alloc};
    out.prefix = builder_from_string(alloc, list.prefix).string;
    out.root = builder_from_string(alloc, list.root).string;
    for(Path_Segment* curr_path = list.segment_first; curr_path != NULL; curr_path = curr_path->next)
        path_list_push_unsafe(&out, path_segment_string(curr_path), 0);

    out.is_absolute = list.is_absolute;
    out.is_directory = list.is_directory;
    out.root_kind = list.root_kind;
    return out;
}

Path_List path_list_make_relative_from_lists(Allocator* alloc_or_null, Path_List relative_to, Path_List path)
{
    Allocator* alloc = allocator_or_default(alloc_or_null);
    Path_List out = {alloc};

    //We cannot do:
    // relative_to: relative/path/to/
    // path       : C:/absolute/path/to/file.txt
    // 
    //... and the other case is already relative thus there is nothing to be done
    // relative_to: C:/absolute/path/to/file.txt
    // path       : relative/path/to/
    //
    //Also if even the root differs we can just copy the whole thing again.
    if(relative_to.is_absolute != path.is_absolute 
        || string_is_equal(path.root, relative_to.root) == false
        || path.root_kind != relative_to.root_kind)
    {
        out = path_list_duplicate(alloc, path);
    }
    else
    {
        out.prefix = builder_from_string(alloc, path.prefix).string;
        out.is_directory = path.is_directory;
        out.is_absolute = false;

        //Loop untill we reach a difference in one of the lists or if 
        Path_Segment* curr_rela = relative_to.segment_first;
        Path_Segment* curr_path = path.segment_first;
        while(true)
        {
            String rela_segment = path_segment_string(curr_rela);
            String path_segment = path_segment_string(curr_path);
            bool are_equal = string_is_equal(rela_segment, path_segment);

            //If both are present and same do nothing
            if(curr_rela && curr_path && are_equal)
            {
                 //nothing   
            }
            //If they were same and end the same then also do nothing
            else if(curr_rela == false && curr_path == false && are_equal)
            {
                break;
            }
            else
            {
                //If rel is shorter than path add all remainig segments of `path` into `into`
                if(curr_rela == NULL)
                {   
                    //@NOTE: Its okay to use unsafe functions here since this can only backfire if
                    // the library user has already used path_list_push_unsafe() to insert something bad
                    for(; curr_path != NULL; curr_path = curr_path->next)
                        path_list_push_unsafe(&out, path_segment_string(curr_path), 0);
                }
                //If there was a difference in the path or path is shorter
                // we add appropriate ammount of ".." segments then the rest of the path
                else
                {
                    for(; curr_rela != NULL; curr_rela = curr_rela->next)
                        path_list_push_unsafe(&out, STRING(".."), 0);

                    for(; curr_path != NULL; curr_path = curr_path->next)
                        path_list_push_unsafe(&out, path_segment_string(curr_path), 0);
                }

                break;
            }

            curr_rela = curr_rela->next;
            curr_path = curr_path->next;
        }
    }

    return out;
}

Path_List path_list_make_absolute_from_lists(Allocator* alloc_or_null, Path_List relative_to, Path_List path)
{
    Path_List out = {0};
    if(path.is_absolute)
        out = path_list_duplicate(alloc_or_null, path);
    else
    {
        out = path_list_duplicate(alloc_or_null, relative_to);
        out.is_directory = path.is_directory;
        for(Path_Segment* curr_path = path.segment_first; curr_path != NULL; curr_path = curr_path->next)
            path_list_push_unsafe(&out, path_segment_string(curr_path), 0);
    }

    return out;
}

Path_List path_list_make_relative(Allocator* alloc_or_null, Path relative_to, Path path)
{
    Arena arena = scratch_arena_acquire();
    Path_List relative_list = path_list_make(&arena.allocator, relative_to, 0);
    Path_List path_list = path_list_make(&arena.allocator, path, 0);
    Path_List out = path_list_make_relative_from_lists(alloc_or_null, relative_list, path_list);
    arena_release(&arena);

    return out;
}

Path_List path_list_make_absolute(Allocator* alloc_or_null, Path relative_to, Path path)
{
    Arena arena = scratch_arena_acquire();
    Path_List relative_list = path_list_make(&arena.allocator, relative_to, 0);
    Path_List path_list = path_list_make(&arena.allocator, path, 0);
    Path_List out = path_list_make_absolute_from_lists(alloc_or_null, relative_list, path_list);
    arena_release(&arena);

    return out;
}

String_Builder path_normalize(Allocator* alloc_or_null, Path path, int flags)
{
    String_Builder out = builder_make(alloc_or_null, path.string.size*9/8 + 10);
    Arena arena = scratch_arena_acquire();
    Path_List path_list = path_list_make(&arena.allocator, path, flags);
    path_list_to_string(&out, path_list, flags);
    arena_release(&arena);

    return out;
}

String_Builder _path_make_relative(Allocator* alloc_or_null, Path relative_to, Path path)
{
    String_Builder out = builder_make(alloc_or_null, MAX(relative_to.string.size, path.string.size) + 10);
    
    Arena arena = scratch_arena_acquire();
    Path_List path_list = path_list_make_relative(&arena.allocator, relative_to, path);
    path_list_to_string(&out, path_list, 0);
    arena_release(&arena);

    return out;
}

String_Builder _path_make_absolute(Allocator* alloc_or_null, Path relative_to, Path path)
{
    String_Builder out = builder_make(alloc_or_null, relative_to.string.size + path.string.size + 10);
    
    Arena arena = scratch_arena_acquire();
    Path_List path_list = path_list_make_absolute(&arena.allocator, relative_to, path);
    path_list_to_string(&out, path_list, 0);
    arena_release(&arena);

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