#include "platform.h"

#include <assert.h>
#include <windows.h>

#include <memoryapi.h>
#include <processthreadsapi.h>
#include <winnt.h>
#include <profileapi.h>
#include <hidusage.h>
#include <windowsx.h>
#include <intrin.h>
#include <direct.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")


enum { WINDOWS_PLATFORM_FILE_TYPE_PIPE = PLATFORM_FILE_TYPE_PIPE };
#undef PLATFORM_FILE_TYPE_PIPE



//=========================================
// Virtual memory
//=========================================
void* platform_virtual_reallocate(void* adress, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection)
{
    //CreateThread();
    if(action == PLATFORM_VIRTUAL_ALLOC_RELEASE)
    {
        (void) VirtualFree(adress, 0, MEM_RELEASE);  
        return NULL;
    }

    if(action == PLATFORM_VIRTUAL_ALLOC_DECOMMIT)
    {
        (void) VirtualFree(adress, bytes, MEM_DECOMMIT);  
        return NULL;
    }
 
    int prot = 0;
    if(protection == PLATFORM_MEMORY_PROT_NO_ACCESS)
        prot = PAGE_NOACCESS;
    if(protection == PLATFORM_MEMORY_PROT_READ)
        prot = PAGE_READONLY;
    else
        prot = PAGE_READWRITE;

    if(action == PLATFORM_VIRTUAL_ALLOC_RESERVE)
        return VirtualAlloc(adress, bytes, MEM_RESERVE, prot);
    else
        return VirtualAlloc(adress, bytes, MEM_COMMIT, prot);
}

void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t old_size, int64_t align)
{
    assert(align > 0 && new_size >= 0 && old_size >= 0);
    
    (void) old_size;
    #ifndef NDEBUG
    if(old_ptr != NULL && old_size != 0)
    {
        //@TODO: Remove this. Actually refactor the entire way we do allocations
        //int64_t correct_size = platform_heap_get_block_size(old_ptr, align);
        //assert(old_size == correct_size && "incorrect old_size passed to platform_heap_reallocate!");
    }
    #endif

    if(new_size == 0)
    {
        _aligned_free(old_ptr);
        return NULL;
    }
    return _aligned_realloc(old_ptr, (size_t) new_size, (size_t) align);
}


int64_t platform_heap_get_block_size(void* old_ptr, int64_t align)
{
    int64_t size = _aligned_msize(old_ptr, (size_t) align, 0);
    return size;
}

//=========================================
// Threading
//=========================================
int64_t platform_thread_get_proccessor_count()
{
    return GetCurrentProcessorNumber();
}

#pragma warning(disable : 4057)
//typedef DWORD (WINAPI *PTHREAD_START_ROUTINE)(
//    LPVOID lpThreadParameter
//    );
Platform_Thread platform_thread_create(int (*func)(void*), void* context, int64_t stack_size)
{
    Platform_Thread thread = {0};
    thread.handle = CreateThread(0, stack_size, func, context, 0, &thread.id);
    return thread;
}
void platform_thread_destroy(Platform_Thread* thread)
{
    Platform_Thread null = {0};
    CloseHandle(thread->handle);
    *thread = null;
}

int32_t platform_thread_get_id()
{
    return GetCurrentThreadId();
}
void platform_thread_yield()
{
    SwitchToThread();
}
void platform_thread_sleep(int64_t ms)
{
    Sleep((DWORD) ms);
}

void platform_thread_exit(int code)
{
    ExitThread(code);
}

int platform_thread_join(Platform_Thread thread)
{
    WaitForSingleObject(thread.handle, INFINITE);
    DWORD code = 0;
    GetExitCodeThread(thread.handle, &code);
    return code;
}


//=========================================
// Timings
//=========================================
int64_t platform_perf_counter()
{
    LARGE_INTEGER ticks;
    ticks.QuadPart = 0;
    (void) QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
}

int64_t platform_perf_counter_startup()
{
    static int64_t base = 0;
    if(base == 0)
        base = platform_perf_counter();
    return base;
}

int64_t platform_perf_counter_frequency()
{
    static int64_t freq = 0;
    if(freq == 0)
    {
        LARGE_INTEGER ticks;
        ticks.QuadPart = 0;
        (void) QueryPerformanceFrequency(&ticks);
        freq = ticks.QuadPart;
    }
    return freq;
}

//time -> filetime
//(ts * 10000000LL) + 116444736000000000LL = t
//(ts * 10LL) + 116444736000LL = tu = t/1 0000 000
    
//filetime -> time
//ts = (t - 116444736000000000LL) / 10000000;
//t / 10000000 - 11644473600LL = ts;
//t / 10 - 11644473600 000 000LL = tu = ts*1 0000 000;
static int64_t _filetime_to_epoch_time(FILETIME t)  
{    
    ULARGE_INTEGER ull;    
    ull.LowPart = t.dwLowDateTime;    
    ull.HighPart = t.dwHighDateTime;
    int64_t tu = ull.QuadPart / 10 - 11644473600000000LL;
    return tu;
}

static FILETIME _epoch_time_to_filetime(int64_t tu)  
{    
    ULARGE_INTEGER time_value;
    FILETIME time;
    time_value.QuadPart = (tu + 11644473600000000LL)*10;

    time.dwLowDateTime = time_value.LowPart;
    time.dwHighDateTime = time_value.HighPart;
    return time;
}
static time_t _filetime_to_time_t(FILETIME ft)  
{    
    ULARGE_INTEGER ull;    
    ull.LowPart = ft.dwLowDateTime;    
    ull.HighPart = ft.dwHighDateTime;    
    return (time_t) (ull.QuadPart / 10000000ULL - 11644473600ULL);  
}

int64_t platform_universal_epoch_time()
{
    FILETIME filetime;
    GetSystemTimeAsFileTime(&filetime);
    int64_t epoch_time = _filetime_to_epoch_time(filetime);
    return epoch_time;
}

int64_t _platform_startup_time = 0;

int64_t platform_startup_epoch_time()
{
    if(_platform_startup_time == 0)
        _platform_startup_time = platform_universal_epoch_time();

    return _platform_startup_time;
}

int64_t platform_local_epoch_time()
{
    FILETIME filetime;
    GetSystemTimeAsFileTime(&filetime);
    FILETIME local_filetime = {0};
    bool okay = FileTimeToLocalFileTime(&filetime, &local_filetime);
    assert(okay); (void) okay;
    int64_t epoch_time = _filetime_to_epoch_time(local_filetime);
    return epoch_time;
}

Platform_Calendar_Time platform_epoch_time_to_calendar_time(int64_t epoch_time_usec)
{
    #define EPOCH_YEAR              (int64_t) 1970
    #define MILLISECOND_MICROSECOND (int64_t) 1000
    #define SECOND_MICROSECONDS     (int64_t) 1000000
    #define DAY_SECONDS             (int64_t) 86400
    #define YEAR_SECONDS            (int64_t) 31556952
    #define DAY_MICROSECONDS        (DAY_SECONDS * SECOND_MICROSECONDS)
    #define YEAR_MICROSECONDS       (YEAR_SECONDS * SECOND_MICROSECONDS)

    SYSTEMTIME systime = {0};
    FILETIME filetime = _epoch_time_to_filetime(epoch_time_usec);
    bool okay = FileTimeToSystemTime(&filetime, &systime);
    assert(okay);

    Platform_Calendar_Time time = {0};
    time.day = (int8_t) systime.wDay;
    time.day_of_week = (int8_t) systime.wDayOfWeek;
    time.hour = (int8_t) systime.wHour;
    time.millisecond = (int16_t) systime.wMilliseconds;
    time.minute = (int8_t) systime.wMinute;
    time.month = (int8_t) systime.wMonth - 1;
    time.second = (int8_t) systime.wSecond;
    time.year = (int32_t) systime.wYear;

    int64_t years_since_epoch = (int64_t) time.year - EPOCH_YEAR;
    int64_t microsec_diff = epoch_time_usec - years_since_epoch*YEAR_MICROSECONDS;
    //int64_t microsec_remainder = microsec_diff % YEAR_MICROSECONDS;
    //int64_t day_of_year = (microsec_remainder / DAY_MICROSECONDS);
    //int64_t microsecond = (microsec_remainder % MILLISECOND_MICROSECOND);
    int64_t microsecond = (microsec_diff % MILLISECOND_MICROSECOND);

    //time.day_of_year = (int16_t) day_of_year;
    time.microsecond = (int16_t) microsecond;

    //int64_t times = time.day_of_year;
    assert(0 <= time.month && time.month < 12);
    assert(0 <= time.day && time.day < 31);
    assert(0 <= time.hour && time.hour < 24);
    assert(0 <= time.minute && time.minute < 60);
    assert(0 <= time.second && time.second < 60);
    assert(0 <= time.millisecond && time.millisecond < 1000);
    assert(0 <= time.day_of_week && time.day_of_week < 7);
    //assert(0 <= time.day_of_year && time.day_of_year <= 365);
    return time;
}

int64_t platform_calendar_time_to_epoch_time(Platform_Calendar_Time calendar_time)
{
    SYSTEMTIME systime = {0};
    systime.wDay = calendar_time.day;
    systime.wDayOfWeek = calendar_time.day_of_week;
    systime.wHour = calendar_time.hour;
    systime.wMilliseconds = calendar_time.millisecond;
    systime.wMinute = calendar_time.minute;
    systime.wMonth = calendar_time.month;
    systime.wSecond = calendar_time.second;
    systime.wYear = (WORD) calendar_time.year;

    FILETIME filetime;
    bool okay = SystemTimeToFileTime(&systime, &filetime);
    assert(okay);

    int64_t epoch_time = _filetime_to_epoch_time(filetime);
    epoch_time += calendar_time.microsecond;

    #ifndef NDEBUG
    Platform_Calendar_Time roundtrip = platform_epoch_time_to_calendar_time(epoch_time);
    assert(memcmp(&roundtrip, &calendar_time, sizeof calendar_time) == 0 && "roundtrip must be correct");
    #endif // !NDEBUG

    return epoch_time;
}

//=========================================
// Filesystem
//=========================================

#define IO_ALIGN 8
#define IO_LOCAL_BUFFER_SIZE 1024
#define IO_NORMALIZE_LINUX 0
#define IO_NORMALIZE_DIRECTORY 0
#define IO_NORMALIZE_FILE 0

typedef struct _Buffer
{
    void* data;
    int32_t item_size;
    int32_t is_alloced;
    int64_t size;
    int64_t capacity;
} _Buffer;

static wchar_t* _wstring(_Buffer stack);
static char*    _string(_Buffer stack);
static void     _buffer_init_backed(_Buffer* buffer, void* backing, int64_t backing_size, int32_t item_size);
static _Buffer  _buffer_make(int64_t item_size);
static void*    _buffer_resize(_Buffer* stack, int64_t new_size);
static int64_t  _buffer_push(_Buffer* stack, const void* item, int64_t item_size);
static int64_t  _buffer_append(_Buffer* stack, const void* items, int64_t item_size, int64_t item_count);
static void*    _buffer_at(_Buffer* stack, int64_t index);
static void     _buffer_deinit(_Buffer* stack);

static int64_t _wcslen(const wchar_t* str)
{
    if(str == NULL)
        return 0;
    else
        return (int64_t) wcslen(str);
}
static int64_t _strlen(const char* str)
{
    if(str == NULL)
        return 0;
    else
        return (int64_t) strlen(str);
}

static void _utf16_to_utf8(const wchar_t* utf16, int64_t utf16len, _Buffer* append_to) 
{
    if (utf16 == NULL || utf16len == 0) 
    {
        _buffer_resize(append_to, 0);
        return;
    }

    int utf8len = WideCharToMultiByte(CP_UTF8, 0, utf16, (int) utf16len, NULL, 0, NULL, NULL);
    int64_t size_before = append_to->size;
    _buffer_resize(append_to, size_before + utf8len);
    WideCharToMultiByte(CP_UTF8, 0, utf16, (int) utf16len, _string(*append_to) + size_before, (int) utf8len, 0, 0);
}
    
static void _utf8_to_utf16(const char* utf8, int64_t utf8len, _Buffer* append_to) 
{
    if (utf8 == NULL || utf8len == 0) 
    {
        _buffer_resize(append_to, 0);
        return;
    }

    int utf16len = MultiByteToWideChar(CP_UTF8, 0, utf8, (int) utf8len, NULL, 0);
    int64_t size_before = append_to->size;
    _buffer_resize(append_to, size_before + utf8len);
    MultiByteToWideChar(CP_UTF8, 0, utf8, (int) utf8len, _wstring(*append_to) + size_before, (int) utf16len);
}

static wchar_t* _wstring(_Buffer stack)
{
    assert(stack.item_size == sizeof(wchar_t));
    return (wchar_t*) stack.data;
}

static char* _string(_Buffer stack)
{
    assert(stack.item_size == sizeof(char));
    return (char*) stack.data;
}

static void _buffer_init_backed(_Buffer* buffer, void* backing, int64_t backing_size, int32_t item_size)
{
    buffer->data = backing;
    buffer->item_size = item_size;
    buffer->is_alloced = false;
    buffer->capacity = backing_size/item_size;
}

#define CONCAT2(a, b) a ## b
#define CONCAT(a, b) CONCAT2(a, b)

#define _BUFFER_INIT_BACKED(buffer, type, backing_size) \
    char CONCAT(__backing__, __LINE__)[(backing_size) * sizeof(type)] = {0}; \
    _buffer_init_backed(buffer, CONCAT(__backing__, __LINE__), backing_size, sizeof(type))

static _Buffer _buffer_make(int64_t item_size)
{
    _Buffer out = {0};
    out.item_size = (int32_t) item_size;
    return out;
}

static void* _buffer_resize(_Buffer* stack, int64_t new_size)
{
    int64_t i_size = stack->item_size;
    assert(i_size != 0);
    assert(stack->size <= stack->capacity);
    assert(stack->item_size != 0);

    if(new_size >= stack->capacity)
    {
        int64_t new_capaity = 8;
        while(new_capaity < new_size + 1)
            new_capaity *= 2;

        void* prev_ptr = NULL;
        if(stack->is_alloced)
            prev_ptr = stack->data;
        stack->data = platform_heap_reallocate(new_capaity * i_size, prev_ptr, stack->capacity * i_size, IO_ALIGN);
        stack->is_alloced = true;

        //null newly added portion
        memset((char*) stack->data + stack->capacity*i_size, 0, (new_capaity - stack->capacity)*i_size);
        stack->capacity = new_capaity;
    }

    //Null terminates the buffer
    stack->size = new_size;
    memset((char*) stack->data + new_size*i_size, 0, i_size);
    return stack->data;
}


static int64_t _buffer_append(_Buffer* stack, const void* items, int64_t item_size, int64_t item_count)
{
    if(stack->item_size == 0)
        stack->item_size = (int32_t) item_size;

    assert(stack->item_size == item_size);
    _buffer_resize(stack, stack->size + item_count);

    memmove((char*) stack->data + (stack->size - item_count)*item_size, items, item_size*item_count);
    return stack->size;
}

static int64_t _buffer_push(_Buffer* stack, const void* item, int64_t item_size)
{
    return _buffer_append(stack, item, item_size, 1);

    //if(stack->item_size == 0)
    //    stack->item_size = (int32_t) item_size;

    //assert(stack->item_size == item_size);
    //_buffer_resize(stack, stack->size + 1);

    //memmove((char*) stack->data + (stack->size - 1)*item_size, item, item_size);
    //return stack->size;
}

static void* _buffer_at(_Buffer* stack, int64_t index)
{
    assert(0 <= index && index < stack->size);
    return (char*) stack->data + index*stack->item_size;
}

static void _buffer_deinit(_Buffer* stack)
{
    if(stack->is_alloced)
        (void) platform_heap_reallocate(0, stack->data, stack->capacity * stack->item_size, IO_ALIGN);
    
    _Buffer empty = {0};
    *stack = empty;
}

static void _w_concat(const wchar_t* a, const wchar_t* b, const wchar_t* c, _Buffer* output)
{
    int64_t a_size = _wcslen(a);
    int64_t b_size = _wcslen(b);
    int64_t c_size = _wcslen(c);
    int64_t composite_size = a_size + b_size + c_size;
        
    _buffer_resize(output, composite_size);
    memmove(_wstring(*output),                   a, sizeof(wchar_t) * a_size);
    memmove(_wstring(*output) + a_size,          b, sizeof(wchar_t) * b_size);
    memmove(_wstring(*output) + a_size + b_size, c, sizeof(wchar_t) * c_size);
}

void _normalize_convert_to_utf16_path(_Buffer* output, const char* path, int64_t path_size)
{
    assert(output->item_size == sizeof(wchar_t));

    //if(path_size > _MAX_PATH + 5)
    //{
    //    const wchar_t* long_path_prefix = L"\\\\?\\";
    //    _buffer_append(output, long_path_prefix, sizeof(wchar_t), _wcslen(long_path_prefix));
    //}

    _utf8_to_utf16(path, path_size, output);
    wchar_t* str = _wstring(*output);

    for(int64_t i = 0; i < output->size; i++)
    {
        if(str[i] == L'/')
            str[i] = L'\\';
    }
}

void _convert_to_utf8_normalize_path(_Buffer* output, const wchar_t* path, int64_t path_size)
{
    assert(output->item_size == sizeof(char));
    _utf16_to_utf8(path, path_size, output);

    char* str = _string(*output);

    for(int64_t i = 0; i < output->size; i++)
    {
        if(str[i] == '\\')
            str[i] = '/';
    }
}

void _normalize_convert_to_utf16_cpath(_Buffer* output, const char* null_terminated_path)
{
    int64_t new_path_size = _strlen(null_terminated_path);
    _normalize_convert_to_utf16_path(output, null_terminated_path, new_path_size);
}

void _convert_to_utf8_normalize_cpath(_Buffer* output, const wchar_t* null_terminated_path)
{
    int64_t new_path_size = _wcslen(null_terminated_path);
    _convert_to_utf8_normalize_path(output, null_terminated_path, new_path_size);
}

#define _TRANSLATED_ERRORS_SIMULATANEOUS 8
static char* _translated_errors[_TRANSLATED_ERRORS_SIMULATANEOUS] = {0};
static int64_t _translated_error_cursor = 0;

const char* platform_translate_error(Platform_Error error)
{
    LPVOID trasnlated = NULL;
    int64_t length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        (DWORD) error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR) &trasnlated,
        0, NULL );
    
    (void) length;
    LocalFree(_translated_errors[_translated_error_cursor]);
    _translated_errors[_translated_error_cursor] = trasnlated;
    _translated_error_cursor = (_translated_error_cursor + 1) % _TRANSLATED_ERRORS_SIMULATANEOUS;

    return (const char*) trasnlated;
}

void _translated_deinit_all()
{
    for(int64_t i = 0; i < _TRANSLATED_ERRORS_SIMULATANEOUS; i++)
    {
        LocalFree((HLOCAL) _translated_errors[i]);
        _translated_errors[i] = NULL;
    }
}

Platform_Error _error_code(bool state)
{
    if(state)
        return PLATFORM_ERROR_OK;
    else
        return (Platform_Error) GetLastError();
}

//Makes a temporary buffer called buffer_name filled with utf16 representation of path.
//buffer gets automatically freed upon exit. Return or other control from must not be used to
// for it to work properly
#define _TEMP_CONVERT_UTF16(buffer_name, path)                          \
    _Buffer buffer_name = {0};                                          \
    _BUFFER_INIT_BACKED(&buffer_name, wchar_t, IO_LOCAL_BUFFER_SIZE);   \
    _normalize_convert_to_utf16_cpath(&buffer_name, path);              \
    for(; buffer_name.item_size != 0 ;_buffer_deinit(&buffer_name))

Platform_Error platform_file_create(const char* file_path, bool* was_just_created)
{
    bool state = true;
    _TEMP_CONVERT_UTF16(normalized_path, file_path) {
        HANDLE handle = CreateFileW(_wstring(normalized_path), 0, 0, NULL, OPEN_ALWAYS, 0, NULL);
        state = handle != INVALID_HANDLE_VALUE;

        if(was_just_created != NULL)
        {
            DWORD last_error = GetLastError();
            if(last_error == ERROR_ALREADY_EXISTS)
                *was_just_created = false;
            else
                *was_just_created = true;
        }

        CloseHandle(handle);
    }

    return _error_code(state);
}
Platform_Error platform_file_remove(const char* file_path, bool* was_deleted_deleted)
{
    bool state = true;
    _TEMP_CONVERT_UTF16(normalized_path, file_path) {
        SetFileAttributesW(_wstring(normalized_path), FILE_ATTRIBUTE_NORMAL);
        state = !!DeleteFileW(_wstring(normalized_path));
        
        if(was_deleted_deleted != NULL)
            *was_deleted_deleted = state;

        DWORD last_error = GetLastError();
        if(last_error == ERROR_FILE_NOT_FOUND)
        {
            state = true;
            if(was_deleted_deleted != NULL)
                *was_deleted_deleted = false;
        }
    }

    return _error_code(state);
}

Platform_Error platform_file_move(const char* new_path, const char* old_path)
{       
    bool state = true;
    _TEMP_CONVERT_UTF16(new_path_norm, new_path) {
        _TEMP_CONVERT_UTF16(old_path_norm, old_path) {
            state = !!MoveFileExW(_wstring(old_path_norm), _wstring(new_path_norm), MOVEFILE_COPY_ALLOWED);
        }
    }
    
    return _error_code(state);
}

Platform_Error platform_file_copy(const char* copy_to_path, const char* copy_from_path)
{
    bool state = true;
    _TEMP_CONVERT_UTF16(to_path_norm, copy_to_path) {
        _TEMP_CONVERT_UTF16(from_path_norm, copy_from_path) {
            state = !!CopyFileW(_wstring(from_path_norm), _wstring(to_path_norm), true);
        }
    }
    
    return _error_code(state);
}

void platform_file_memory_unmap(Platform_Memory_Mapping* mapping)
{
    if(mapping == NULL)
        return;

    HANDLE hFile = (HANDLE) mapping->state[0];
    HANDLE hMap = (HANDLE) mapping->state[1];
    LPVOID lpBasePtr = mapping->address;

    if(lpBasePtr != NULL)
        UnmapViewOfFile(lpBasePtr);
    if(hMap != NULL && hMap != INVALID_HANDLE_VALUE)
        CloseHandle(hMap);
    if(hFile != NULL && hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
        
    memset(mapping, 0, sizeof *mapping);
}

Platform_Error platform_file_memory_map(const char* file_path, int64_t desired_size_or_zero, Platform_Memory_Mapping* mapping)
{
    memset(mapping, 0, sizeof *mapping);

    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = INVALID_HANDLE_VALUE;
    LPVOID lpBasePtr = NULL;
    LARGE_INTEGER liFileSize;
    liFileSize.QuadPart = 0;

    DWORD disposition = 0;
    if(desired_size_or_zero == 0)
        disposition = OPEN_EXISTING;
    else
        disposition = OPEN_ALWAYS;
        
    _TEMP_CONVERT_UTF16(normalized_path, file_path) {
        wchar_t* wfile_path = _wstring(normalized_path);
        hFile = CreateFileW(
            wfile_path,                            // lpFileName
            GENERIC_READ | GENERIC_WRITE,          // dwDesiredAccess
            FILE_SHARE_READ | FILE_SHARE_WRITE,    // dwShareMode
            NULL,                                  // lpSecurityAttributes
            disposition,                           // dwCreationDisposition
            FILE_ATTRIBUTE_NORMAL,                 // dwFlagsAndAttributes
            0);                                    // hTemplateFile
    }

    if (hFile == INVALID_HANDLE_VALUE)
        goto error;

    if (!GetFileSizeEx(hFile, &liFileSize)) 
        goto error;

    //If the file is completely empty 
    // we dont perform any more operations
    // return a valid pointer and size of 0
    if (liFileSize.QuadPart == 0 && desired_size_or_zero == 0) 
    {
        CloseHandle(hFile);
        mapping->size = 0;
        mapping->address = NULL;
        return PLATFORM_ERROR_OK;
    }

    LARGE_INTEGER desired_size = {0};
    if(desired_size_or_zero == 0)
        desired_size.QuadPart = liFileSize.QuadPart;
    if(desired_size_or_zero > 0)
    {
        desired_size.QuadPart = desired_size_or_zero;

        //if is desired smaller shrinks the file
        if(desired_size_or_zero < liFileSize.QuadPart)
        {
            DWORD dwPtrLow = SetFilePointer(hFile, desired_size.LowPart, &desired_size.HighPart,  FILE_BEGIN); 
            if(dwPtrLow == INVALID_SET_FILE_POINTER)
                goto error;

            if(SetEndOfFile(hFile) == FALSE)
                goto error;
        }
    }
    if(desired_size_or_zero < 0)
        desired_size.QuadPart = -desired_size_or_zero + liFileSize.QuadPart;

    hMap = CreateFileMappingW(
        hFile,
        NULL,                          // Mapping attributes
        PAGE_READWRITE ,               // Protection flags
        desired_size.HighPart,         // MaximumSizeHigh
        desired_size.LowPart,          // MaximumSizeLow
        NULL);                         // Name
    if (hMap == 0) 
        goto error;

    lpBasePtr = MapViewOfFile(
        hMap,
        FILE_MAP_ALL_ACCESS,   // dwDesiredAccess
        0,                     // dwFileOffsetHigh
        0,                     // dwFileOffsetLow
        0);                    // dwNumberOfBytesToMap
    if (lpBasePtr == NULL) 
        goto error;

    mapping->size = desired_size.QuadPart;
    mapping->address = lpBasePtr;
    mapping->state[0] = (uint64_t) hFile;
    mapping->state[1] = (uint64_t) hMap;

    return PLATFORM_ERROR_OK;

    error: {
        DWORD err = GetLastError();
        if(hMap != INVALID_HANDLE_VALUE)
            CloseHandle(hMap);
        if(hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);

        return err;
    }
}

static bool _is_file_link(const wchar_t* directory_path)
{
    HANDLE file = CreateFileW(directory_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    size_t requiredSize = GetFinalPathNameByHandleW(file, NULL, 0, FILE_NAME_NORMALIZED);
    CloseHandle(file);

    return requiredSize == 0;
}

Platform_Error platform_file_info(const char* file_path, Platform_File_Info* info)
{    
    WIN32_FILE_ATTRIBUTE_DATA native_info;
    memset(&native_info, 0, sizeof(native_info)); 
    memset(info, 0, sizeof(*info)); 
    
    bool state = true;
    _TEMP_CONVERT_UTF16(normalized_path, file_path) {
        state = !!GetFileAttributesExW(_wstring(normalized_path), GetFileExInfoStandard, &native_info);
        
        if(native_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            info->is_link = _is_file_link(_wstring(normalized_path));
    }
    if(!state)
        return _error_code(state);
            
    info->created_epoch_time = _filetime_to_epoch_time(native_info.ftCreationTime);
    info->last_access_epoch_time = _filetime_to_epoch_time(native_info.ftLastAccessTime);
    info->last_write_epoch_time = _filetime_to_epoch_time(native_info.ftLastWriteTime);
    info->size = ((int64_t) native_info.nFileSizeHigh << 32) | ((int64_t) native_info.nFileSizeLow);
        
    if(native_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        info->type = PLATFORM_FILE_TYPE_DIRECTORY;
    else
        info->type = PLATFORM_FILE_TYPE_FILE;

    return _error_code(state);
}
    
//@TODO: create a cyclically overriden buffers that will get used for utf8 conversions
// this ensures that they will not ever have to get freed while periodally freeing them
//We have lets say 4 allocations slots. We allocate into the first then the second then the third and so on.
// once we run out of slots we allocate only if we need to grow. If we use a slot some number of times (2x lets say)
// we reallocate it to ensure that the space shrinks once not needed.

Platform_Error platform_directory_create(const char* dir_path)
{
    bool state = true;
    _TEMP_CONVERT_UTF16(normalized_path, dir_path) {
        state = !!CreateDirectoryW(_wstring(normalized_path), NULL);
    }

    return _error_code(state);
}
    
Platform_Error platform_directory_remove(const char* dir_path)
{
    bool state = true;
    _TEMP_CONVERT_UTF16(normalized_path, dir_path) {
        state = !!RemoveDirectoryW(_wstring(normalized_path));
    }

    return _error_code(state);
}

static _Buffer _alloc_full_path(const wchar_t* local_path)
{
    _Buffer full_path = {0};
    _BUFFER_INIT_BACKED(&full_path, wchar_t, IO_LOCAL_BUFFER_SIZE);

    int64_t needed_size = GetFullPathNameW(local_path, 0, NULL, NULL);
    if(needed_size > full_path.size)
    {
        _buffer_resize(&full_path, needed_size);
        needed_size = GetFullPathNameW(local_path, (DWORD) full_path.size, _wstring(full_path), NULL);
    }
    
    _Buffer out_string = _buffer_make(sizeof(char));
    _convert_to_utf8_normalize_path(&out_string, _wstring(full_path), full_path.size);

    _buffer_deinit(&full_path);
    return out_string;
}

typedef struct Directory_Visitor
{
    WIN32_FIND_DATAW current_entry;
    HANDLE first_found;
    bool failed;
} Directory_Visitor;

#define WIO_FILE_MASK_ALL L"\\*.*"

static Directory_Visitor _directory_iterate_init(const wchar_t* dir_path, const wchar_t* file_mask)
{
    _Buffer built_path = {0};
    _BUFFER_INIT_BACKED(&built_path, wchar_t, IO_LOCAL_BUFFER_SIZE);
    _w_concat(dir_path, file_mask, NULL, &built_path);

    Directory_Visitor visitor = {0};
    assert(built_path.data != NULL);
    visitor.first_found = FindFirstFileW(_wstring(built_path), &visitor.current_entry);
    while(visitor.failed == false && visitor.first_found != INVALID_HANDLE_VALUE)
    {
        if(wcscmp(visitor.current_entry.cFileName, L".") == 0
            || wcscmp(visitor.current_entry.cFileName, L"..") == 0)
            visitor.failed = !FindNextFileW(visitor.first_found, &visitor.current_entry);
        else
            break;
    }

    _buffer_deinit(&built_path);
    return visitor;
}

static Platform_Error _directory_iterate_has(const Directory_Visitor* visitor)
{
    return visitor->first_found != INVALID_HANDLE_VALUE && visitor->failed == false;
}

static void _directory_iterate_next(Directory_Visitor* visitor)
{
    visitor->failed = visitor->failed || !FindNextFileW(visitor->first_found, &visitor->current_entry);
}

static void _directory_iterate_deinit(Directory_Visitor* visitor)
{
    FindClose(visitor->first_found);
}

static Platform_Error _directory_list_contents_alloc(const wchar_t* directory_path, _Buffer* entries, int64_t max_depth)
{
    typedef struct Dir_Context
    {
        Directory_Visitor visitor;
        const wchar_t* path;    
        int64_t depth;          
        int64_t index;          
    } Dir_Context;

    Dir_Context first = {0};

    
    first.visitor = _directory_iterate_init(directory_path, WIO_FILE_MASK_ALL);
    first.path = directory_path;
    if(_directory_iterate_has(&first.visitor) == false)
        return _error_code(false);
    
    _Buffer stack = {0};
    _Buffer built_path = {0};

    _BUFFER_INIT_BACKED(&stack, Dir_Context, 16);
    _BUFFER_INIT_BACKED(&built_path, wchar_t, IO_LOCAL_BUFFER_SIZE);

    _buffer_push(&stack, &first, sizeof(first));

    const int64_t MAX_RECURSION = 10000;
        
    //While the queue is not empty iterate
    for(int64_t reading_from = 0; reading_from < stack.size; reading_from++)
    {
        Dir_Context* dir_context = (Dir_Context*) _buffer_at(&stack, reading_from);
        for(; _directory_iterate_has(&dir_context->visitor); _directory_iterate_next(&dir_context->visitor), dir_context->index++)
        {
            //Build up our file path using the passed in 
            //  [path] and the file/foldername we just found: 
            _w_concat(dir_context->path, L"\\", dir_context->visitor.current_entry.cFileName, &built_path);
            assert(built_path.data != 0);
        
            Platform_File_Info info = {0};
            info.created_epoch_time = _filetime_to_epoch_time(dir_context->visitor.current_entry.ftCreationTime);
            info.last_access_epoch_time = _filetime_to_epoch_time(dir_context->visitor.current_entry.ftLastAccessTime);
            info.last_write_epoch_time = _filetime_to_epoch_time(dir_context->visitor.current_entry.ftLastWriteTime);
            info.size = ((int64_t) dir_context->visitor.current_entry.nFileSizeHigh << 32) | ((int64_t) dir_context->visitor.current_entry.nFileSizeLow);
        
            info.type = PLATFORM_FILE_TYPE_FILE;
            if(dir_context->visitor.current_entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                info.type = PLATFORM_FILE_TYPE_DIRECTORY;
            else
                info.type = PLATFORM_FILE_TYPE_FILE;

            if(info.is_link)
                info.is_link = _is_file_link(_wstring(built_path));  


            int flag = IO_NORMALIZE_LINUX;
            if(info.type == PLATFORM_FILE_TYPE_DIRECTORY)
                flag |= IO_NORMALIZE_DIRECTORY;
            else
                flag |= IO_NORMALIZE_FILE;
            _Buffer out_string = _alloc_full_path(_wstring(built_path));

            Platform_Directory_Entry entry = {0};
            entry.info = info;
            entry.path = _string(out_string);
            entry.path_size = out_string.size;
    
            _buffer_push(entries, &entry, sizeof(entry));

            bool recursion = dir_context->depth + 1 < max_depth || max_depth <= 0;
            if(info.type == PLATFORM_FILE_TYPE_DIRECTORY && info.is_link == false && recursion)
            {
                _Buffer next_path = _buffer_make(sizeof(wchar_t));
                _w_concat(_wstring(built_path), NULL, NULL, &next_path);

                Dir_Context next = {0};
                next.visitor = _directory_iterate_init(_wstring(next_path), WIO_FILE_MASK_ALL);
                next.depth = dir_context->depth + 1;
                next.path = _wstring(next_path);
                assert(next.depth < MAX_RECURSION && "must not get stuck in an infinite loop");
                _buffer_push(&stack, &next, sizeof(next));
            }
        }
    }

    for(int64_t i = 0; i < stack.size; i++)
    {
        Dir_Context* dir_context = (Dir_Context*) _buffer_at(&stack, i);
        if(dir_context->path != directory_path)
            platform_heap_reallocate(0, (void*) dir_context->path, 0, IO_ALIGN);
        _directory_iterate_deinit(&dir_context->visitor);
    }
    
    //Null terminate the entries
    Platform_Directory_Entry terminator = {0};
    _buffer_push(entries, &terminator, sizeof(terminator));

    _buffer_deinit(&stack);
    _buffer_deinit(&built_path);
    return _error_code(true);
}

Platform_Error platform_directory_list_contents_alloc(const char* directory_path, Platform_Directory_Entry** entries, int64_t* entries_count, int64_t max_depth)
{
    assert(entries != NULL && entries_count != NULL);
    _Buffer entries_stack = _buffer_make(sizeof(Platform_Directory_Entry));

    Platform_Error error = _error_code(true);
    _TEMP_CONVERT_UTF16(normalized_path, directory_path) {
        error = _directory_list_contents_alloc(_wstring(normalized_path), &entries_stack, max_depth);
    }

    *entries = (Platform_Directory_Entry*) entries_stack.data;
    *entries_count = entries_stack.size;
    return error;
}

void platform_directory_list_contents_free(Platform_Directory_Entry* entries)
{
    if(entries == NULL)
        return;

    for(int64_t i = 0; entries[i].path != NULL; i++)
        platform_heap_reallocate(0, entries[i].path, 0, IO_ALIGN);
            
    platform_heap_reallocate(0, entries, 0, IO_ALIGN);
}

int64_t _directory_current_working_generation = 0;

Platform_Error platform_directory_set_current_working(const char* new_working_dir)
{
    bool state = true;
    _TEMP_CONVERT_UTF16(normalized_path, new_working_dir) {
         state = _wchdir(_wstring(normalized_path)) == 0;
    }

    _directory_current_working_generation += 1;
    return _error_code(state);
}

const char* platform_directory_get_current_working()
{
    static char* cached = NULL;
    static int64_t cached_generation = -1;
    if(cached_generation != _directory_current_working_generation)
    {
        wchar_t* current_working = _wgetcwd(NULL, 0);
        if(current_working == NULL)
        {
            _Buffer output = _buffer_make(sizeof(char));
            _convert_to_utf8_normalize_cpath(&output, current_working);
            free(current_working);
            cached = _string(output);
        }

        cached_generation = _directory_current_working_generation;
    }

    return cached;
}


const char* platform_get_executable_path()
{
    static char* cached = NULL;
    if(cached == NULL)
    {
        _Buffer wide = _buffer_make(sizeof(wchar_t));
        _BUFFER_INIT_BACKED(&wide, wchar_t, MAX_PATH);

        for(int64_t i = 0; i < 50; i++)
        {
            int64_t len = GetModuleFileNameW(NULL, _wstring(wide), (DWORD) wide.size);

            if(len < wide.size)
                break;

            _buffer_resize(&wide, wide.size * 2);
        }

        _Buffer out_buffer = _buffer_make(sizeof(char));
        _utf16_to_utf8(_wstring(wide), wide.size, &out_buffer);

        cached = _string(out_buffer);
    }
    
    return cached;
}

//=========================================
// Window managmenet
//=========================================
Platform_Window_Popup_Controls platform_window_make_popup(Platform_Window_Popup_Style desired_style, const char* message, const char* title)
{
    int style = 0;
    int icon = 0;
    switch(desired_style)
    {
        case PLATFORM_POPUP_STYLE_OK:            style = MB_OK; break;
        case PLATFORM_POPUP_STYLE_ERROR:         style = MB_OK; icon = MB_ICONERROR; break;
        case PLATFORM_POPUP_STYLE_WARNING:       style = MB_OK; icon = MB_ICONWARNING; break;
        case PLATFORM_POPUP_STYLE_INFO:          style = MB_OK; icon = MB_ICONINFORMATION; break;
        case PLATFORM_POPUP_STYLE_RETRY_ABORT:   style = MB_ABORTRETRYIGNORE; icon = MB_ICONWARNING; break;
        case PLATFORM_POPUP_STYLE_YES_NO:        style = MB_YESNO; break;
        case PLATFORM_POPUP_STYLE_YES_NO_CANCEL: style = MB_YESNOCANCEL; break;
        default: style = MB_OK; break;
    }
    
    int value = 0;
    _TEMP_CONVERT_UTF16(message_wide, message) { 
        _TEMP_CONVERT_UTF16(title_wide, title) { 
            wchar_t* message_ptr = _wstring(message_wide);
            wchar_t* title_ptr = _wstring(title_wide);

            value = MessageBoxW(0, message_ptr, title_ptr, style | icon);
        }
    }

    switch(value)
    {
        case IDABORT: return PLATFORM_POPUP_CONTROL_ABORT;
        case IDCANCEL: return PLATFORM_POPUP_CONTROL_CANCEL;
        case IDCONTINUE: return PLATFORM_POPUP_CONTROL_CONTINUE;
        case IDIGNORE: return PLATFORM_POPUP_CONTROL_IGNORE;
        case IDYES: return PLATFORM_POPUP_CONTROL_YES;
        case IDNO: return PLATFORM_POPUP_CONTROL_NO;
        case IDOK: return PLATFORM_POPUP_CONTROL_OK;
        case IDRETRY: return PLATFORM_POPUP_CONTROL_RETRY;
        case IDTRYAGAIN: return PLATFORM_POPUP_CONTROL_RETRY;
        default: return PLATFORM_POPUP_CONTROL_OK;
    }
}

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")

// Some versions of imagehlp.dll lack the proper packing directives themselves
// so we need to do it.
#pragma pack( push, before_imagehlp, 8 )
#include <imagehlp.h>
#pragma pack( pop, before_imagehlp )

int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count)
{
    if(stack_size == 0)
        return 0;

    int64_t captured = CaptureStackBackTrace((DWORD) skip_count + 1, (DWORD) stack_size, stack, NULL);
    return captured;
}

#define MAX_MODULES 128 
#define MAX_NAME_LEN 2048

static bool   stack_trace_init = false;
static HANDLE stack_trace_process;
static DWORD  stack_trace_error = 0;

static void _platform_stack_trace_init(const char* search_path)
{
    if(stack_trace_init)
        return;

    stack_trace_process = GetCurrentProcess();
    if (!SymInitialize(stack_trace_process, search_path, false)) 
    {
        assert(false);
        stack_trace_error = GetLastError();
        return;
    }

    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
    SymSetOptions(symOptions);
    
    DWORD module_handles_size_needed = 0;
    HMODULE module_handles[MAX_MODULES] = {0};
    TCHAR module_filename[MAX_NAME_LEN] = {0};
    TCHAR module_name[MAX_NAME_LEN] = {0};
    EnumProcessModules(stack_trace_process, module_handles, sizeof(module_handles), &module_handles_size_needed);
    
    DWORD module_count = module_handles_size_needed/sizeof(HMODULE);
    for(int64_t i = 0; i < module_count; i++)
    {
        HMODULE module_handle = module_handles[i];
        assert(module_handle != 0);
        MODULEINFO module_info = {0};
        GetModuleInformation(stack_trace_process, module_handle, &module_info, sizeof(module_info));
        GetModuleFileNameExW(stack_trace_process, module_handle, module_filename, sizeof(module_filename));
        GetModuleBaseNameW(stack_trace_process, module_handle, module_name, sizeof(module_name));
        
        bool load_state = SymLoadModuleExW(stack_trace_process, 0, module_filename, module_name, (DWORD64)module_info.lpBaseOfDll, (DWORD) module_info.SizeOfImage, 0, 0);
        if(load_state == false)
        {
            assert(false);
            stack_trace_error = GetLastError();
        }
    }

    stack_trace_init = true;
}

static void _platform_stack_trace_deinit()
{
    SymCleanup(stack_trace_process);
}

void platform_translate_call_stack(Platform_Stack_Trace_Entry* tanslated, const void** stack, int64_t stack_size)
{
    if(stack_size == 0)
        return;

    _platform_stack_trace_init("");
    char symbol_info_data[sizeof(SYMBOL_INFO) + MAX_NAME_LEN + 1] = {0};

    DWORD offset_from_symbol = 0;
    IMAGEHLP_LINE64 line = {0};
    line.SizeOfStruct = sizeof line;

    Platform_Stack_Trace_Entry null_entry = {0};
    for(int64_t i = 0; i < stack_size; i++)
    {
        Platform_Stack_Trace_Entry* entry = tanslated + i;
        DWORD64 address = (DWORD64) stack[i];
        *entry = null_entry;

        if (address == 0)
            continue;

        memset(symbol_info_data, '\0', sizeof symbol_info_data);

        SYMBOL_INFO* symbol_info = (SYMBOL_INFO*) symbol_info_data;
        symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol_info->MaxNameLen = MAX_NAME_LEN;
        DWORD64 displacement = 0;
        SymFromAddr(stack_trace_process, address, &displacement, symbol_info);
            
        if (symbol_info->Name[0] != '\0')
        {
            UnDecorateSymbolName(symbol_info->Name, entry->function, sizeof entry->function, UNDNAME_COMPLETE);
        }
           
        IMAGEHLP_MODULE module_info = {0};
        module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE);
        bool module_info_state = SymGetModuleInfo64(stack_trace_process, address, &module_info);
        if(module_info_state)
        {
            int64_t copy_size = sizeof module_info.ImageName;
            if(copy_size > sizeof entry->module - 1)
                copy_size = sizeof entry->module - 1;

            memmove(entry->module, module_info.ImageName, copy_size);
        }
            
        if (SymGetLineFromAddr64(stack_trace_process, address, &offset_from_symbol, &line)) 
        {
            entry->line = line.LineNumber;
            
            int64_t copy_size = strlen(line.FileName);
            if(copy_size > sizeof entry->file - 1)
                copy_size = sizeof entry->file - 1;

            memmove(entry->file, line.FileName, copy_size);
        }
    }
}

static int64_t _platform_stack_trace_walk(CONTEXT context, HANDLE process, HANDLE thread, DWORD image_type, void** frames, int64_t frame_count, int64_t skip_count)
{
    STACKFRAME64 frame = {0};
    #ifdef _M_IX86
        DWORD native_image = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset    = context.Eip;
        frame.AddrPC.Mode      = AddrModeFlat;
        frame.AddrFrame.Offset = context.Ebp;
        frame.AddrFrame.Mode   = AddrModeFlat;
        frame.AddrStack.Offset = context.Esp;
        frame.AddrStack.Mode   = AddrModeFlat;
    #elif _M_X64
        DWORD native_image = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset    = context.Rip;
        frame.AddrPC.Mode      = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rsp;
        frame.AddrFrame.Mode   = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode   = AddrModeFlat;
    #elif _M_IA64
        DWORD native_image = IMAGE_FILE_MACHINE_IA64;
        frame.AddrPC.Offset    = context.StIIP;
        frame.AddrPC.Mode      = AddrModeFlat;
        frame.AddrFrame.Offset = context.IntSp;
        frame.AddrFrame.Mode   = AddrModeFlat;
        frame.AddrBStore.Offset= context.RsBSP;
        frame.AddrBStore.Mode  = AddrModeFlat;
        frame.AddrStack.Offset = context.IntSp;
        frame.AddrStack.Mode   = AddrModeFlat;
    #else
        #error "Unsupported platform"
    #endif

    if(image_type == 0)
        image_type = native_image; 
    
    (void) process;
    int64_t i = 0;
    for(; i < frame_count; i++)
    {
        bool ok = StackWalk64(native_image, stack_trace_process, thread, &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
        if (ok == false)
            break;

        if(skip_count > 0)
        {
            skip_count--;
            i --;
            continue;
        }

        frames[i] = (void*) frame.AddrPC.Offset;
    }
    
    return i;
}

typedef void (*Sandbox_Error_Func)(void* context, Platform_Sandox_Error error_code);

__declspec(thread) Sandbox_Error_Func sandbox_error_func;
__declspec(thread) void* sandbox_error_context;
__declspec(thread) Platform_Sandox_Error sanbox_error_code;
__declspec(thread) bool sandbox_is_signal_handler_set = false;

void platform_abort()
{
    RaiseException(PLATFORM_EXCEPTION_ABORT, 0, 0, NULL);
}

void platform_terminate()
{
    RaiseException(PLATFORM_EXCEPTION_TERMINATE, 0, 0, NULL);
}

LONG WINAPI _sanbox_exception_filter(EXCEPTION_POINTERS * ExceptionInfo)
{
    Platform_Sandox_Error error = PLATFORM_EXCEPTION_NONE;
    switch(ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            error = PLATFORM_EXCEPTION_ACCESS_VIOLATION;
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            error = PLATFORM_EXCEPTION_ACCESS_VIOLATION;
            break;
        case EXCEPTION_BREAKPOINT:
            error = PLATFORM_EXCEPTION_BREAKPOINT;
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            error = PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT;
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            error = PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND;
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            error = PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO;
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            error = PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT;
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            error = PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION;
            break;
        case EXCEPTION_FLT_OVERFLOW:
            error = PLATFORM_EXCEPTION_FLOAT_OVERFLOW;
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            error = PLATFORM_EXCEPTION_STACK_OVERFLOW;
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            error = PLATFORM_EXCEPTION_FLOAT_UNDERFLOW;
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            error = PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION;
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            error = PLATFORM_EXCEPTION_PAGE_ERROR;
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            error = PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO;
            break;
        case EXCEPTION_INT_OVERFLOW:
            error = PLATFORM_EXCEPTION_INT_OVERFLOW;
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            error = PLATFORM_EXCEPTION_OTHER;
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            error = PLATFORM_EXCEPTION_OTHER;
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            error = PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION;
            break;
        case EXCEPTION_SINGLE_STEP:
            error = PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP;
            break;
        case EXCEPTION_STACK_OVERFLOW:
            error = PLATFORM_EXCEPTION_STACK_OVERFLOW;
            break;
        case PLATFORM_EXCEPTION_ABORT:
            error = PLATFORM_EXCEPTION_ABORT;
            break;
        case PLATFORM_EXCEPTION_TERMINATE:
            error = PLATFORM_EXCEPTION_TERMINATE;
            break;
        default:
            error = PLATFORM_EXCEPTION_OTHER;
            break;
    }

    sanbox_error_code = error;
    if(sandbox_error_func != NULL && error != PLATFORM_EXCEPTION_STACK_OVERFLOW)
        sandbox_error_func(sandbox_error_context, sanbox_error_code);
    return EXCEPTION_EXECUTE_HANDLER;
}

Platform_Sandox_Error platform_exception_sandbox(
    void (*sandboxed_func)(void* context),   
    void* sandbox_context,
    void (*error_func)(void* context, Platform_Sandox_Error error_code),   
    void* error_context
)
{
    if(sandbox_is_signal_handler_set == false)
    {
        sandbox_is_signal_handler_set = true;
        SetUnhandledExceptionFilter(_sanbox_exception_filter);
        AddVectoredExceptionHandler(1, _sanbox_exception_filter);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    }
        
    Sandbox_Error_Func prev_func = sandbox_error_func;
    void* prev_context = sandbox_error_context;
    
    sandbox_error_func = error_func;
    sandbox_error_context = error_context;
    __try 
    { 
        sandboxed_func(sandbox_context);
    } 
    __except(1)
    { 
        sandbox_error_func = prev_func;
        sandbox_error_context = prev_context;
        return sanbox_error_code;
    }

    sandbox_error_func = prev_func;
    sandbox_error_context = prev_context;
    return 0;
}


void platform_init()
{
    platform_perf_counter();
    platform_startup_epoch_time();
}
void platform_deinit()
{
    _translated_deinit_all();
}