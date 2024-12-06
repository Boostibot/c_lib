#ifndef JOT_FILE
#define JOT_FILE

#include "string.h"
#include "platform.h"

EXTERNAL Platform_Error file_read_entire_append(String file_path, String_Builder* append_into, Platform_File_Info* info_or_null);
EXTERNAL Platform_Error file_read_entire(String file_path, String_Builder* data, Platform_File_Info* info_or_null);
EXTERNAL Platform_Error file_append_entire(String file_path, String data);
EXTERNAL Platform_Error file_write_entire(String file_path, String data);
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_FILE_IMPL)) && !defined(JOT_FILE_HAS_IMPL)
#define JOT_FILE_HAS_IMPL

EXTERNAL Platform_Error file_read_entire_append(String file_path, String_Builder* append_into, Platform_File_Info* info_or_null)
{
    PROFILE_START();
    Platform_File_Info info = {0};
    Platform_File file = {0};
    Platform_Error error = platform_file_info(file_path, &info);
    isize size_before = append_into->len;
    
    if(error == 0)
        error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_READ);
    if(error == 0)
    {
        isize read_bytes = 0;
        builder_resize(append_into, append_into->len + info.size);
        error = platform_file_read(&file, append_into->data + size_before, info.size, &read_bytes);
    }
    if(error != 0)
        builder_resize(append_into, size_before);

    if(info_or_null)
        *info_or_null = info;

    platform_file_close(&file);
    PROFILE_STOP();
    return error;
}

EXTERNAL Platform_Error file_read_entire(String file_path, String_Builder* data, Platform_File_Info* info_or_null)
{
    builder_clear(data);
    return file_read_entire_append(file_path, data, info_or_null);
}
EXTERNAL bool file_append_entire(String file_path, String data)
{
    PROFILE_START();
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_APPEND | PLATFORM_FILE_MODE_CREATE);
    
    if(error == 0)
    {
        platform_file_seek(&file, 0, PLATFORM_FILE_SEEK_FROM_END);
        error = platform_file_write(&file, data.data, data.len);
    }

    platform_file_close(&file);
    PROFILE_STOP();
    return error;
}
EXTERNAL bool file_write_entire(String file_path, String data)
{
    PROFILE_START();
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_MODE_WRITE | PLATFORM_FILE_MODE_CREATE);
    
    if(error == 0)
        error = platform_file_write(&file, data.data, data.len);

    platform_file_close(&file);
    PROFILE_STOP();
    return error;
}
#endif