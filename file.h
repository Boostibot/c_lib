#ifndef JOT_FILE
#define JOT_FILE

#include "string.h"
#include "path.h"

EXTERNAL bool file_read_entire_append_into(String file_path, String_Builder* append_into, Log log);
EXTERNAL bool file_read_entire(String file_path, String_Builder* data, Log log);
EXTERNAL bool file_append_entire(String file_path, String data, Log log);
EXTERNAL bool file_write_entire(String file_path, String data, Log log);
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FILE_IMPL)) && !defined(JOT_FILE_HAS_IMPL)
#define JOT_FILE_HAS_IMPL

EXTERNAL bool file_read_entire_append_into(String file_path, String_Builder* append_into, Log log)
{
    PROFILE_START();
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
        builder_resize(append_into, size_before);
        LOG(log, "error reading file '%.*s': %s", STRING_PRINT(file_path), platform_translate_error(error));
    }

    platform_file_close(&file);
    PROFILE_END();
    return error == 0;
}

EXTERNAL bool file_read_entire(String file_path, String_Builder* data, Log log)
{
    builder_clear(data);
    return file_read_entire_append_into(file_path, data, log);
}
EXTERNAL bool file_append_entire(String file_path, String data, Log log)
{
    PROFILE_START();
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_APPEND | PLATFORM_FILE_MODE_CREATE);
    
    if(error == 0)
    {
        platform_file_seek(&file, 0, PLATFORM_FILE_SEEK_FROM_END);
        error = platform_file_write(&file, data.data, data.size);
    }

    if(error != 0)
        LOG(log, "error appending file '%.*s': %s", STRING_PRINT(file_path), platform_translate_error(error));
    platform_file_close(&file);
    PROFILE_END();
    return error == 0;
}
EXTERNAL bool file_write_entire(String file_path, String data, Log log)
{
    PROFILE_START();
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_WRITE | PLATFORM_FILE_MODE_CREATE);
    
    if(error == 0)
        error = platform_file_write(&file, data.data, data.size);

    if(error != 0)
        LOG(log, "error writing file '%.*s': %s", STRING_PRINT(file_path), platform_translate_error(error));

    platform_file_close(&file);
    PROFILE_END();
    return error == 0;
}
#endif