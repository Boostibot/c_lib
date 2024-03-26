#ifndef JOT_FILE
#define JOT_FILE

#include "string.h"
#include "path.h"

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

EXPORT String path_get_name_from_path(String path);

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

enum {FILE_EPHEMERAL_SLOT_COUNT = 4};

typedef struct File_Global_State
{
    bool is_init;
    bool _padding[7];
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
