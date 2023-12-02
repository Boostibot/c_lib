#ifndef JOT_FILE
#define JOT_FILE

#include "platform.h"
#include "allocator.h"
#include "string.h"
#include "error.h"
#include "profile.h"

EXPORT Error file_read_entire_append_into(String file_path, String_Builder* append_into);
EXPORT Error file_read_entire(String file_path, String_Builder* data);
EXPORT Error file_append_entire(String file_path, String data);
EXPORT Error file_write_entire(String file_path, String data);
EXPORT Error file_create(String file_path, bool* was_just_created);
EXPORT Error file_remove(String file_path, bool* was_just_removed);

EXPORT Error  path_get_full_from(String_Builder* into, String path, String base);
EXPORT String path_get_full_ephemeral_from(String path, String base);
EXPORT void   path_get_relative_from(String_Builder* into, String path, String base);
EXPORT String path_get_relative_ephemeral_from(String path, String base);

EXPORT Error  path_get_full(String_Builder* into, String path);
EXPORT String path_get_full_ephemeral(String path);
EXPORT void   path_get_relative(String_Builder* into, String path);
EXPORT String path_get_relative_ephemeral(String path);

EXPORT String path_get_executable();
EXPORT String path_get_executable_directory();
EXPORT String path_get_current_working_directory();
EXPORT String path_get_file_directory(String file_path);


EXPORT String path_get_name_from_path(String path);

// Represents the following:
// //?//C:/Users/Program_Files/./../Dir/file.txt
// <---><-><-------------------------->|<------>
//   P   R         D                   |  F  <->
//                                     M*      E
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
    bool is_valid;
    bool is_relative;
} Path_Info;

typedef struct Path {
    Path_Info info;
    String path;
} Path;

EXPORT Path_Info path_parse(String path);
EXPORT String path_get_part_prexif(String path, Path_Info info);
EXPORT String path_get_part_root(String path, Path_Info info);
EXPORT String path_get_part_diretcories(String path, Path_Info info);
EXPORT String path_get_part_extension(String path, Path_Info info);
EXPORT String path_get_part_filename(String path, Path_Info info);

//path_parse: String -> Path_Info fast entirely on my side. Should be okay since we have the parse.h. 
//path_validate: String -> File_Info
//path_get_full: String -> Path

//path_get_fr

//Path is fully safe path. It however does not have to point to a valid file.


#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FILE_IMPL)) && !defined(JOT_FILE_HAS_IMPL)
#define JOT_FILE_HAS_IMPL

#include "log.h"
#include "format.h"

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
            array_init(&file_global_state.relative_paths[i], file_global_state.alloc);
            
        for(isize i = 0; i < FILE_EPHEMERAL_SLOT_COUNT; i++)
            array_init(&file_global_state.full_paths[i], file_global_state.alloc);

        file_global_state.full_path_used_count = 0;
        file_global_state.relative_path_used_count = 0;
        file_global_state.is_init = true;
    }
}

EXPORT Error path_get_full_from(String_Builder* into, String path, String base)
{
    //@TEMP: implement this in platform. Implement platform allocator support
    array_clear(into);
    if(base.size > 0)
    {
        builder_append(into, base);
        if(base.data[base.size - 1] != '/')
            array_push(into, '/');
    }

    builder_append(into, path);
    return ERROR_OK;
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
    String out_string = string_from_builder(file_global_state.relative_paths[curr_index]);
    file_global_state.relative_path_used_count += 1;

    return out_string;
}

EXPORT String path_get_full_ephemeral_from(String path, String base)
{
    _file_init_global_state();

    isize curr_index = file_global_state.full_path_used_count % FILE_EPHEMERAL_SLOT_COUNT;
    Error error = path_get_full_from(&file_global_state.full_paths[curr_index], path, base);
    String out_string = string_from_builder(file_global_state.full_paths[curr_index]);
    file_global_state.full_path_used_count += 1;

    if(error_is_ok(error) == false)
        LOG_ERROR("FILE", "Failed to get full path of file " STRING_FMT " with error: " ERROR_FMT, STRING_PRINT(path), ERROR_PRINT(error));

    return out_string;
}

EXPORT Error  path_get_full(String_Builder* into, String path) 
{ 
    return path_get_full_from(into, path, path_get_executable_directory()); 
}
EXPORT String path_get_full_ephemeral(String path)
{
    return path_get_full_ephemeral_from(path, path_get_executable_directory()); 
}
EXPORT void path_get_relative(String_Builder* into, String path)
{
    path_get_relative_from(into, path, path_get_executable_directory()); 
}
EXPORT String path_get_relative_ephemeral(String path)
{
    return path_get_relative_ephemeral_from(path, path_get_executable_directory()); 
}

#include <stdio.h>

EXPORT Error file_read_entire_append_into(String file_path, String_Builder* append_into)
{
    PERF_COUNTER_START(c);
    Platform_Memory_Mapping mapping = {0};
    Platform_Error error = platform_file_memory_map(file_path, 0, &mapping);
    if(error == 0)
    {
        //@NOTE: if this fails because we dont have enough memory then the file remains mapped!
        //@TODO: make this proper!
        array_append(append_into, (char*) mapping.address, mapping.size);
        platform_file_memory_unmap(&mapping);
    }

    PERF_COUNTER_END(c);
    return error_from_platform(error);
}

EXPORT Error file_read_entire(String file_path, String_Builder* data)
{
    array_clear(data);
    return file_read_entire_append_into(file_path, data);
}

EXPORT Error file_append_entire(String file_path, String contents)
{
    PERF_COUNTER_START(c);
    Platform_Memory_Mapping mapping = {0};
    Platform_Error error = platform_file_memory_map(file_path, -contents.size, &mapping);
    if(error == 0)
    {
        u8* bytes = (u8*) mapping.address;
        u8* last_bytes = bytes + mapping.size - contents.size;
        memcpy(last_bytes, contents.data, contents.size);
        platform_file_memory_unmap(&mapping);
    }

    PERF_COUNTER_END(c);
    return error_from_platform(error);
}

EXPORT Error file_write_entire(String file_path, String contents)
{
    PERF_COUNTER_START(c);
    Platform_Memory_Mapping mapping = {0};
    Platform_Error error = platform_file_memory_map(file_path, contents.size, &mapping);
    if(error == 0)
    {
        memcpy(mapping.address, contents.data, contents.size);
        platform_file_memory_unmap(&mapping);
    }

    PERF_COUNTER_END(c);
    return error_from_platform(error);
}

EXPORT Error file_create(String file_path, bool* was_just_created)
{
    PERF_COUNTER_START(c);

    Platform_Error error = platform_file_create(file_path, was_just_created);
    
    PERF_COUNTER_END(c);
    return error_from_platform(error);
}

EXPORT Error file_remove(String file_path, bool* was_just_removed)
{
    PERF_COUNTER_START(c);

    Platform_Error error = platform_file_remove(file_path, was_just_removed);
    
    PERF_COUNTER_END(c);
    return error_from_platform(error);
}

#endif
