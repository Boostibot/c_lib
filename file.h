#ifndef JOT_FILE
#define JOT_FILE

#include "string.h"
#include "path.h"

EXPORT bool file_read_entire_append_into(String file_path, String_Builder* append_into);
EXPORT bool file_read_entire(String file_path, String_Builder* data);
EXPORT bool file_append_entire(String file_path, String data);
EXPORT bool file_write_entire(String file_path, String data);
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FILE_IMPL)) && !defined(JOT_FILE_HAS_IMPL)
#define JOT_FILE_HAS_IMPL

#if 0
#include "vformat.h"
#include "string.h"

#include <stdio.h>
#include <errno.h>

EXPORT bool file_read_entire_append_into(String file_path, String_Builder* append_into)
{
    enum {CHUNK_SIZE = 4096*16};
    isize size_before = append_into->size;
    isize read_bytes = 0;

    const char* full_path = cstring_ephemeral(file_path);
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
        LOG_ERROR("file.h", "error reading file '%s': %s", cstring_ephemeral(file_path), strerror(errno));
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
    
    const char* full_path = cstring_ephemeral(file_path);
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
    
    LOG_ERROR("file.h", "error appending to a file '%s': %s", cstring_ephemeral(file_path), strerror(errno));
    return false;
}

EXPORT bool file_write_entire(String file_path, String contents)
{
    if(_file_write_entire_append_into(file_path, contents, "wb"))
        return true;
    
    LOG_ERROR("file.h", "error writing file '%s': %s", cstring_ephemeral(file_path), strerror(errno));
    return false;
}

#else

EXPORT bool file_read_entire_append_into(String file_path, String_Builder* append_into)
{
    Platform_File_Info info = {0};
    Platform_File file = {0};
    Platform_Error error = platform_file_info(file_path, &info);
    isize size_before = append_into->size;
    
    if(error == 0)
        error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_READ);
    if(error == 0)
    {
        isize read_bytes = 0;
        builder_resize(append_into, append_into->size + info.size);
        error = platform_file_read(&file, append_into->data + size_before, info.size, &read_bytes);
    }

    if(error != 0)
    {
        builder_resize(append_into, append_into->size);
        LOG_ERROR("file.h", "error writing file '%.*s': %s", STRING_PRINT(file_path), platform_translate_error(error));
    }

    platform_file_close(&file);
    return error == 0;
}

EXPORT bool file_read_entire(String file_path, String_Builder* data)
{
    builder_clear(data);
    return file_read_entire_append_into(file_path, data);
}
EXPORT bool file_append_entire(String file_path, String data)
{
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_APPEND);
    
    if(error == 0)
    {
        platform_file_seek(&file, 0, PLATFORM_FILE_SEEK_FROM_END);
        error = platform_file_write(&file, data.data, data.size);
    }

    if(error != 0)
        LOG_ERROR("file.h", "error appending file '%.*s': %s", STRING_PRINT(file_path), platform_translate_error(error));
    platform_file_close(&file);
    return error == 0;
}
EXPORT bool file_write_entire(String file_path, String data)
{
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_WRITE);
    
    if(error == 0)
        error = platform_file_write(&file, data.data, data.size);

    if(error != 0)
        LOG_ERROR("file.h", "error writing file '%.*s': %s", STRING_PRINT(file_path), platform_translate_error(error));

    platform_file_close(&file);
    return error == 0;
}
#endif

#endif
