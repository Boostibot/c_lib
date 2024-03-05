#ifndef JOT_FILE
#define JOT_FILE

#include "string.h"

EXPORT bool file_read_entire_append_into(String file_path, String_Builder* append_into);
EXPORT bool file_read_entire(String file_path, String_Builder* data);
EXPORT bool file_append_entire(String file_path, String data);
EXPORT bool file_write_entire(String file_path, String data);

EXPORT bool   path_get_full_from(String_Builder* into, String path, String base);
EXPORT String path_get_full_ephemeral_from(String path, String base);
EXPORT void   path_get_relative_from(String_Builder* into, String path, String base);
EXPORT String path_get_relative_ephemeral_from(String path, String base);

EXPORT bool   path_get_full(String_Builder* into, String path);
EXPORT String path_get_full_ephemeral(String path);
EXPORT void   path_get_relative(String_Builder* into, String path);
EXPORT String path_get_relative_ephemeral(String path);

EXPORT String path_get_executable();
EXPORT String path_get_executable_directory();
EXPORT String path_get_current_working_directory();
EXPORT String path_get_file_directory(String file_path);

EXPORT String path_get_name_from_path(String path);


// "/path/forward" + "filename.txt" ---> "/path/forward/filename.txt"
// ""              + "filename.txt" ---> "filename.txt"
// "C:/"           + "filename.txt" ---> "C:/filename.txt"


// identity + path = ???
// 
// if path is relative nonempty, identity can be "." or ""
// if path is relative empty,    identity must be "."
// if path is absolute,          identity must be ""
//
// => there is no clear candidate for the identity element in the usual path
//    convention (both unix and windows)

// Thus we declare "" to be indentity and define path_append() which will operate on the
// identity element correctly according to the given prefix.

// String -> Path_String { String, Info }
// 
// Path_Builder only takes valid Path_Strings and produces valid path strings. It also accumulates errors.

// What about splitting into individual segments? Do we want linked list style approach?

// String -> Path_Segement_Array { String, Info }

// No probably what we want is String -> Path_Info 
// and then Path_Builder { String_Array segments, Info } 
// bool path_append(Path_Builder* path, Path path);



// Represents the following:
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
//       This is because non normalized directory paths can bu dont have to end
//       on /. This makes sure that both cases have the same size.
// 
typedef struct Path_Info {
    i32 prefix_size;
    i32 root_size;
    i32 directories_size;
    i32 file_size;
    i32 extension_size;
    i32 segment_count;
    bool is_absolute;
    bool is_non_empty;
    bool is_directory;
} Path_Info;

typedef union Path_Builder {
    struct {
        Allocator* allocator;
        String string;
    };
    struct {
        String_Builder builder;
        Path_Info info;
    };
} Path_Builder;

typedef struct Path {
    String string;
    Path_Info info;
} Path;

EXPORT Path   path_parse(String path);
EXPORT Path   path_parse_c(const char* path);
EXPORT String path_get_prexif(Path path);
EXPORT String path_get_root(Path path);
EXPORT String path_get_except_root(Path path);
EXPORT String path_get_directories(Path path);
EXPORT String path_get_extension(Path path);
EXPORT String path_get_filename(Path path);

enum {
    PATH_APPEND_EVEN_WITH_ERROR = 1,
};

EXPORT bool path_builder_append(Path_Builder* builder, Path path, int flags)
{
    bool state = true;
    if(builder->info.is_non_empty == false)
    {
        builder_append(&builder->builder, path.string);
        builder->info = path.info;
    }
    else
    {
        if(builder->info.is_absolute && path.info.is_absolute)
            state = false;

        if(state || (flags & PATH_APPEND_EVEN_WITH_ERROR))
        {
            String not_root = path_get_except_root(path);
            builder_append(&builder->builder, not_root);
        }

        //Actually probably
        //@TODO: reparse but only the rest not root.
        //@TEMP
        Path reparsed = path_parse(builder->string);
        builder->info = reparsed.info;
    }

    return state;
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

EXPORT Path_Builder path_canonicalize(Path path, Allocator* alloc)
{
    String root = path_get_root(path);
    String rest = path_get_except_root(path);

    Path_Builder out = {alloc};
    if(root.size > 0)
        builder_append(&out.builder, root);

    if(rest.size > 0)
    {
        isize dir_i = 0;
        ASSERT(is_path_sep(rest.data[dir_i]) == false);
        for(; dir_i < rest.size;)
        {
            isize i_next = -1;
            for(isize k = dir_i; k < rest.size; k++)
                if(rest.data[k] == '/' || rest.data[k] == '\\')
                    i_next = k;

            if(i_next == -1)
                i_next = rest.size;

            String segment = string_range(rest, dir_i, i_next);
            if(segment.size == 0 || string_is_equal(segment, STRING(".")))
            {
                //nothing;
            }
            else if(string_is_equal(segment, STRING("..")))
            {
                //pop segment
                isize segment_from = string_find_last_path_separator(out.string, out.string.size);
                if(segment_from == -1 || segment_from < root.size)
                {
                    //If there was no segement to pop push the .. sgement
                    if(path.info.is_absolute == false)
                        builder_append(&out.builder, STRING("../"));
                    //If is absolute there is nowehere left to backup to
                    else
                        builder_resize(&out.builder, root.size);
                }
                else
                {
                    //else trim
                    builder_resize(&out.builder, segment_from);
                }
            }
            else
            {
                //push segment
                builder_append(&out.builder, segment);
                builder_push(&out.builder, '/');
            }

            dir_i = i_next + 1;
        }
    }

    bool is_directory = false;
    if(out.string.size > 0)
        is_directory = is_path_sep(out.string.data[out.string.size - 1]);

    //If desired path is directory but we arent
    if(path.info.is_directory)
    {
        if(is_directory == false)
        {
            if(out.string.size == 0)
                builder_append(&out.builder, STRING("./"));
            else
                builder_append(&out.builder, STRING("/"));
        }
    }
    //If desired path is file but we are a directory
    else
    {
        if(is_directory == true)
        {
            //if its just root then there is nothing we can do
            if(out.string.size == root.size)
            {}
            else
            {
                builder_push(&out.builder, '/');
            }
        }
    }

    return out;
}

EXPORT const char* path_builder_get_cstring(Path_Builder path)
{
    if(path.info.is_non_empty)
        return cstring_escape(path.string.data);
    else
        return ".";
}

//

EXPORT Path path_parse(String path)
{
    //@NOTE: We attempt to cover a few edge cases and gain as much insigth into the
    //       path as we can but we by no means attempt to be exhaustively correct
    //       for all special windows cases. As such this should be viewed as an
    //       approximation of the exact solution rather than the final product.

    Path_Info info = {0};
    String prefix_path = path;

    //https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    String win32_file_namespace = STRING("\\\\?\\");    // "\\?\"
    String win32_device_namespace = STRING("\\\\.\\");  // "\\.\"

    //Attempt to parse windows prefixes 
    if(string_is_prefixed_with(prefix_path, win32_file_namespace)) 
    {
        info.prefix_size = (i32) win32_file_namespace.size;
    }
    else if(string_is_prefixed_with(prefix_path, win32_device_namespace))
    {
        info.prefix_size = (i32) win32_device_namespace.size;
    }

    String root_path = string_tail(prefix_path, info.prefix_size);
    if(root_path.size == 0 || string_is_equal(root_path, STRING(".")))
    {
        info.is_non_empty = false;
        info.is_absolute = false;
        info.is_directory = true; //empty path is current directory. Thus a directory
    }
    else
    {
        info.is_non_empty = true;

        //unix style root
        if(root_path.size >= 1 && is_path_sep(root_path.data[0]))
        {
            info.is_absolute = true;
            info.root_size = 1;
        }
        //unix style home
        else if(root_path.size >= 1 && root_path.data[0] == '~')
        {
            info.is_absolute = true;
            info.root_size = 1;

            //We take both ~ and ~/ as valid 
            if(root_path.size >= 2 && is_path_sep(root_path.data[1]))
                info.root_size = 2;
        }
        //Windows style root
        else if(root_path.size >= 2 && char_is_alphabetic(root_path.data[0]) && root_path.data[1] == ':')
        {
            //In windows "C:some_file" means relative path on the drive C
            // while "C:/some_file" is absolute path starting from root C
            if(root_path.size >= 3 && is_path_sep(root_path.data[2]))
            {
                info.is_absolute = true;
                info.root_size = 3;
            }
            else
            {
                info.is_absolute = false;
                info.root_size = 2;
            }
        }
        //Windows UNC server path //My_Root
        else if(root_path.size >= 2 && is_path_sep(root_path.data[0]) && is_path_sep(root_path.data[1]))
        {
            isize root_end = string_find_first_path_separator(root_path, 2);
            if(root_end == -1)
                root_end = root_path.size;

            info.root_size = (i32) root_end;
            info.is_absolute = false;
        }
        
        //We consider path a directory path if it ends with slash. This incldues just "/" directory
        isize last = root_path.size - 1;
        ASSERT(last > 0);
        info.is_directory = is_path_sep(root_path.data[last]);

        //@TODO: From now on does not need to happen as far as I am concerned or at least not on 
        // internal calls from path_builder.

        //Parse directories
        String directory_path = string_tail(root_path, info.root_size);
        if(directory_path.size > 0)
        {
            //Find the last directory segment
            isize dir_i = directory_path.size;
            if(info.is_directory == false)
            {
                dir_i = string_find_last_path_separator(directory_path, directory_path.size);
                if(dir_i < 0)
                    dir_i = 0;
            }

            info.directories_size = (i32) dir_i;

            //Parse filename (of course only if is not a directory)
            if(info.is_directory == false)
            {
                String filename_path = string_safe_tail(directory_path, dir_i + 1);
                if(filename_path.size > 0)
                {
                    //If is . or .. then is actually a directory
                    if(string_is_equal(filename_path, STRING(".")) || string_is_equal(filename_path, STRING("..")))
                    {
                        info.is_directory = true;
                        info.directories_size = (i32) directory_path.size;
                    }
                    else
                    {
                        //find the extension if any    
                        isize dot_i = string_find_last_char(filename_path, '.');
                        if(dot_i == -1)
                            dot_i = filename_path.size;
                        else
                            dot_i += 1;

                        info.file_size = (i32) filename_path.size;
                        info.extension_size = (i32) (filename_path.size - dot_i);
                    }
                }
            }
        }
    }

    Path out_path = {0};
    out_path.info = info;
    out_path.string = path;
    return out_path;
}

EXPORT Path path_parse_c(const char* path)
{
    return path_parse(string_make(path));
}

typedef enum {
    PATH_IS_DIR = 1,
    PATH_IS_EMPTY = 2,
    PATH_IS_ABSOLUTE = 4,
} Path_Is_Dir;

void test_single_path(const char* path, const char* prefix, const char* root, const char* directories, const char* filename, const char* extension, Path_Is_Dir flags)
{
    String _path = string_make(path);
    Path parsed = path_parse(_path);
    String _prefix = path_get_prexif(parsed);
    String _root = path_get_root(parsed);
    String _directories = path_get_directories(parsed);
    String _filename = path_get_filename(parsed);
    String _extension = path_get_extension(parsed);

    if(prefix)
        TEST(string_is_equal(_prefix, string_make(prefix)),             STRING_FMT " == %s (%s)", STRING_PRINT(_prefix), prefix, path);
    if(root)
        TEST(string_is_equal(_root, string_make(root)),                 STRING_FMT " == %s (%s)", STRING_PRINT(_root), root, path);
    if(directories)
        TEST(string_is_equal(_directories, string_make(directories)),   STRING_FMT " == %s (%s)", STRING_PRINT(_directories), directories, path);
    if(filename)
        TEST(string_is_equal(_filename, string_make(filename)),         STRING_FMT " == %s (%s)", STRING_PRINT(_filename), filename, path);
    if(extension)
        TEST(string_is_equal(_extension, string_make(extension)),       STRING_FMT " == %s (%s)", STRING_PRINT(_extension), extension, path);

    TEST(parsed.info.is_absolute == (flags & PATH_IS_ABSOLUTE) > 0);
    TEST(parsed.info.is_directory == (flags & PATH_IS_DIR) > 0);
    TEST(parsed.info.is_non_empty != (flags & PATH_IS_EMPTY) > 0);
}

void test_path()
{
    test_single_path("", "", "", "", "", "", PATH_IS_EMPTY | PATH_IS_DIR);
    test_single_path(".", "", "", "", "", "", PATH_IS_EMPTY | PATH_IS_DIR);
    test_single_path("..", "", "", "", "", "", PATH_IS_DIR);
    test_single_path("./", "", "", "", "", "", PATH_IS_EMPTY | PATH_IS_DIR);
    test_single_path("../", "", "", "", "", "", PATH_IS_DIR);

    test_single_path("file.txt", "", "", "", "file.txt", "txt", 0);
    test_single_path("C:/my/files/file.txt", "", "C:/", "my/files", "file.txt", "txt", PATH_IS_ABSOLUTE);
    test_single_path("/my/files/file/", "", "/", "my/files/file", "", "", PATH_IS_ABSOLUTE | PATH_IS_DIR);
    test_single_path("my/files/file", "", "", "my/files", "file", "", 0);
    test_single_path("~/my/files/file/", "", "~/", "my/files/file", "", "", PATH_IS_ABSOLUTE | PATH_IS_DIR);
    test_single_path("~my/files/file", "", "~", "my/files", "file", "", PATH_IS_ABSOLUTE);
    test_single_path("\\\\?\\C:my/files/file", "\\\\?\\", "C:", "my/files", "file", "", PATH_IS_ABSOLUTE);
    test_single_path("//Server/files/.gitignore", "", "//Server", "my/files", "", "gitignore", PATH_IS_ABSOLUTE);
}

EXPORT String path_get_prexif(Path path)
{
    String out = string_head(path.string, path.info.prefix_size);
    return out;
}

EXPORT String path_get_root(Path path)
{
    String out = string_range(path.string, path.info.prefix_size, path.info.prefix_size + path.info.root_size);
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

EXPORT String path_get_extension(Path path)
{
    String out = string_range(path.string, path.string.size - path.info.extension_size, path.string.size);
    return out;
}


#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FILE_IMPL)) && !defined(JOT_FILE_HAS_IMPL)
#define JOT_FILE_HAS_IMPL

#include "vformat.h"
#include "string.h"

EXPORT String path_get_name_from_path(String path)
{
    if(path.size == 0)
        return STRING("");

    isize dot_pos = string_find_last_char(path, '.');
    if(dot_pos == -1)
        dot_pos = path.size;

    isize dir_pos = string_find_last_char_from(path, '/', dot_pos - 1);
    String name = string_range(path, dir_pos + 1, dot_pos);
    return name;
}

EXPORT String path_get_executable()
{
    return string_make(platform_get_executable_path());
}

EXPORT String path_get_executable_directory()
{
    String executable = path_get_executable();
    String executable_directory = path_get_file_directory(executable);
    return executable_directory;
}

EXPORT String path_get_current_working_directory()
{
    return string_make(platform_directory_get_current_working());
}

EXPORT String path_get_file_directory(String file_path)
{
    isize last_dir_index = string_find_last_char(file_path, '/') + 1;
    String dir_path = string_head(file_path, last_dir_index);
    return dir_path;
}

enum {FILE_EPHEMERAL_SLOT_COUNT = 4};

typedef struct File_Global_State
{
    bool is_init;
    isize full_path_used_count;
    isize relative_path_used_count;

    Allocator* alloc; 
    String_Builder full_paths[FILE_EPHEMERAL_SLOT_COUNT];
    String_Builder relative_paths[FILE_EPHEMERAL_SLOT_COUNT];
} File_Global_State;

File_Global_State file_global_state = {0};

INTERNAL void _file_init_global_state()
{
    if(file_global_state.is_init == false)
    {
        file_global_state.alloc = allocator_get_static();
    
        for(isize i = 0; i < FILE_EPHEMERAL_SLOT_COUNT; i++)
            builder_init(&file_global_state.relative_paths[i], file_global_state.alloc);
            
        for(isize i = 0; i < FILE_EPHEMERAL_SLOT_COUNT; i++)
            builder_init(&file_global_state.full_paths[i], file_global_state.alloc);

        file_global_state.full_path_used_count = 0;
        file_global_state.relative_path_used_count = 0;
        file_global_state.is_init = true;
    }
}

EXPORT bool path_get_full_from(String_Builder* into, String path, String base)
{
    //@TEMP: implement this in platform. Implement platform allocator support
    builder_clear(into);
    if(base.size > 0)
    {
        builder_append(into, base);
        if(base.data[base.size - 1] != '/')
            builder_push(into, '/');
    }

    builder_append(into, path);
    return true;
}

EXPORT void path_get_relative_from(String_Builder* into, String path, String base)
{
    if(string_is_prefixed_with(path, base))
    {
        String relative = string_tail(path, base.size);
        builder_assign(into, relative);
    }
    else
        builder_assign(into, path);
}

EXPORT String path_get_relative_ephemeral_from(String path, String base)
{
    _file_init_global_state();

    isize curr_index = file_global_state.relative_path_used_count % FILE_EPHEMERAL_SLOT_COUNT;
    path_get_relative_from(&file_global_state.relative_paths[curr_index], path, base);
    String out_string = file_global_state.relative_paths[curr_index].string;
    file_global_state.relative_path_used_count += 1;

    return out_string;
}

EXPORT String path_get_full_ephemeral_from(String path, String base)
{
    _file_init_global_state();

    isize curr_index = file_global_state.full_path_used_count % FILE_EPHEMERAL_SLOT_COUNT;
    bool state = path_get_full_from(&file_global_state.full_paths[curr_index], path, base);
    String out_string = file_global_state.full_paths[curr_index].string;
    file_global_state.full_path_used_count += 1;

    if(state == false)
        LOG_ERROR("FILE", "Failed to get full path of file '%s'", string_escape_ephemeral(path));

    return out_string;
}


EXPORT bool path_get_full(String_Builder* into, String path) 
{ 
    return path_get_full_from(into, path, string_make(platform_directory_get_current_working())); 
}
EXPORT String path_get_full_ephemeral(String path)
{
    return path_get_full_ephemeral_from(path, string_make(platform_directory_get_current_working())); 
}
EXPORT void path_get_relative(String_Builder* into, String path)
{
    path_get_relative_from(into, path, string_make(platform_directory_get_current_working())); 
}
EXPORT String path_get_relative_ephemeral(String path)
{
    return path_get_relative_ephemeral_from(path, string_make(platform_directory_get_current_working())); 
}

#include <stdio.h>
#include <errno.h>

EXPORT bool file_read_entire_append_into(String file_path, String_Builder* append_into)
{
    enum {CHUNK_SIZE = 4096*16};
    isize size_before = append_into->size;
    isize read_bytes = 0;

    const char* full_path = string_escape_ephemeral(file_path);
    FILE* file = fopen(full_path, "rb");
    bool had_eof = false;
    if(file != NULL)
    {
        while(true) 
        {
            builder_resize(append_into, size_before + read_bytes + CHUNK_SIZE);
            isize single_read = (isize) fread(append_into->data + size_before + read_bytes, 1, CHUNK_SIZE, file);
            if (single_read == 0)
            {
                if(feof(file))
                    had_eof = true;
                break;

            }

            read_bytes += single_read;
        }

        fclose(file);
        builder_resize(append_into, size_before + read_bytes);
    }

    if (file == NULL || had_eof == false) 
    {
        LOG_ERROR("file.h", "error reading file '%s': %s", string_escape_ephemeral(file_path), strerror(errno));
        return false;
    }
    else    
    {
        return true;
    }
}

EXPORT bool _file_write_entire_append_into(String file_path, String written, const char* open_mode)
{
    //Maximum read value allowed by the standard
    enum {MAX_READ = 2097152};
    isize wrote_bytes = 0;
    
    const char* full_path = string_escape_ephemeral(file_path);
    FILE* file = fopen(full_path, open_mode);
    if(file != NULL)
    {
        for(; wrote_bytes < written.size; )
        {
            isize written_size = written.size - wrote_bytes;
            if(written_size > MAX_READ)
                written_size = MAX_READ;

            isize single_write = (isize) fwrite(written.data + wrote_bytes, (size_t) written_size, 1, file);
            if (single_write == 0)
                break;

            wrote_bytes += single_write;
        }

        fclose(file);
    }
    
    if (file == NULL || ferror(file) || wrote_bytes != written.size) 
        return false;
    else    
        return true;
}

EXPORT bool file_read_entire(String file_path, String_Builder* append_into)
{
    builder_clear(append_into);
    return file_read_entire_append_into(file_path, append_into);
}

EXPORT bool file_append_entire(String file_path, String contents)
{
    if(_file_write_entire_append_into(file_path, contents, "ab"))
        return true;
    
    LOG_ERROR("file.h", "error appending to a file '%s': %s", string_escape_ephemeral(file_path), strerror(errno));
    return false;
}

EXPORT bool file_write_entire(String file_path, String contents)
{
    if(_file_write_entire_append_into(file_path, contents, "wb"))
        return true;
    
    LOG_ERROR("file.h", "error writing file '%s': %s", string_escape_ephemeral(file_path), strerror(errno));
    return false;
}

#endif
