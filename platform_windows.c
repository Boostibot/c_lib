#include <locale.h>
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
#ifdef APIENTRY
    #undef APIENTRY
#endif

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
    #define UNICODE
#endif 

#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable:4702) //Dissable "unrelachable code"
#pragma warning(disable:4820) //Dissable "Padding added to struct" 
#pragma warning(disable:4255) //Dissable "no function prototype given: converting '()' to '(void)"  
#pragma warning(disable:5045) //Dissable "Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified"  

#include "platform.h"

#undef near
#undef far

#define _TRANSLATED_ERRORS_SIMULATANEOUS 8
#define _EPHEMERAL_STRING_SIMULTANEOUS 4

typedef struct Platform_WString {
    const wchar_t* data;
    int64_t size;
} Platform_WString;

#define DEFINE_BUFFER_TYPE(T, Name) \
    typedef struct Name { \
        int32_t is_alloced; \
        int64_t size; \
        int64_t capacity; \
        T* data; \
    } Name; \

DEFINE_BUFFER_TYPE(void,               Buffer_Base)
DEFINE_BUFFER_TYPE(char,               String_Buffer)
DEFINE_BUFFER_TYPE(wchar_t,            WString_Buffer)

typedef struct Platform_State {
    Platform_Allocator allocator;

    int64_t startup_perf_counter;
    int64_t perf_counter_freq;
    int64_t startup_epoch_time;

    WString_Buffer ephemeral_strings[_EPHEMERAL_STRING_SIMULTANEOUS];
    int64_t ephemeral_strings_slot;

    char* translated_errors[_TRANSLATED_ERRORS_SIMULATANEOUS];
    int64_t translated_error_slot;

    String_Buffer directory_current_working_cached;
    String_Buffer directory_executable_cached;
    int64_t current_working_cached_generation;
    int64_t current_working_generation;

} Platform_State;

Platform_State gp_state = {0};

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
        //Dissable warning about MEM_DECOMMIT without MEM_RELEASE because thats the whole point of this opperation we are doing here.
        #pragma warning(disable:6250)
        (void) VirtualFree(adress, bytes, MEM_DECOMMIT);  
        #pragma warning(default:6250)
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

void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t align)
{
    assert(align > 0 && new_size >= 0);
    
    if(new_size == 0)
    {
        _aligned_free(old_ptr);
        return NULL;
    }
    void* data = _aligned_realloc(old_ptr, (size_t) new_size, (size_t) align);
    return data;
}


int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align)
{
    int64_t size = 0;
    if(old_ptr)
        size = _aligned_msize((void*) old_ptr, (size_t) align, 0);
    return size;
}


Platform_Error _paltform_get_last_error_code_if(bool state)
{
    Platform_Error err = PLATFORM_ERROR_OK;
    if(!state)
    {
        err = (Platform_Error) GetLastError();
        //If we failed yet there is no errror 
        // set to custom error.
        if(err == 0)
            err = PLATFORM_ERROR_OTHER;
    }
    return err;
}

void* _platform_internal_reallocate(int64_t new_size, void* old_ptr);

//=========================================
// Threading
//=========================================
#include  	<process.h>

int64_t platform_thread_get_proccessor_count()
{
    return GetCurrentProcessorNumber();
}

Platform_Error  platform_thread_launch(Platform_Thread* thread, void (*func)(void*), void* context, int64_t stack_size_or_zero)
{
    // platform_thread_deinit(thread);
    if(stack_size_or_zero <= 0)
        stack_size_or_zero = 0;
    thread->handle = (void*) _beginthread(func, (unsigned int) stack_size_or_zero, context);
    if(thread->handle)
        thread->id = GetThreadId(thread->handle);

    if(thread->handle)
        return PLATFORM_ERROR_OK;
    else
        return (Platform_Error) GetLastError();
}

Platform_Thread platform_thread_get_current()
{
    Platform_Thread out = {0};
    out.id = GetCurrentThreadId();
    out.handle = GetCurrentThread();
    return out;
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

void platform_thread_join(const Platform_Thread* threads, int64_t count)
{
    if(count == 1)
    {
        WaitForSingleObject((HANDLE) threads[0].handle, INFINITE);
    }
    else
    {
        bool wait_for_all = true;
        HANDLE handles[256] = {0};
        for(int64_t i = 0; i < count;)
        {
            int64_t handle_count = 0;
            for(; handle_count < 256; handle_count ++, i++)
                handles[handle_count] = (HANDLE) threads[i].handle;

            WaitForMultipleObjects((DWORD) handle_count, handles, wait_for_all, INFINITE);
        }
    }
}

void platform_thread_detach(Platform_Thread thread)
{
    assert(thread.handle);
    if(thread.handle != NULL)
    {
        bool state = CloseHandle(thread.handle);
        assert(state);
    }
}

Platform_Error platform_mutex_init(Platform_Mutex* mutex)
{
    platform_mutex_deinit(mutex);
    CRITICAL_SECTION* section = calloc(1, sizeof *section);
    if(section != 0)
        InitializeCriticalSection(section);

    mutex->handle = section;
    return _paltform_get_last_error_code_if(section != NULL);
}

void platform_mutex_deinit(Platform_Mutex* mutex)
{
    if(mutex->handle)
    {
        DeleteCriticalSection((CRITICAL_SECTION*) mutex->handle);
        memset(mutex, 0, sizeof mutex);
    }
}

void platform_mutex_lock(Platform_Mutex* mutex)
{
    assert(mutex->handle != NULL);
    EnterCriticalSection((CRITICAL_SECTION*) mutex->handle);
}

void platform_mutex_unlock(Platform_Mutex* mutex)
{
    assert(mutex->handle != NULL);
    LeaveCriticalSection((CRITICAL_SECTION*) mutex->handle);
}

bool platform_mutex_try_lock(Platform_Mutex* mutex)
{
    assert(mutex->handle != NULL);
    return (bool) TryEnterCriticalSection((CRITICAL_SECTION*) mutex->handle);
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
    if(gp_state.startup_perf_counter == 0)
        gp_state.startup_perf_counter = platform_perf_counter();
    return gp_state.startup_perf_counter;
}

int64_t platform_perf_counter_frequency()
{
    if(gp_state.perf_counter_freq == 0)
    {
        LARGE_INTEGER ticks;
        ticks.QuadPart = 0;
        (void) QueryPerformanceFrequency(&ticks);
        gp_state.perf_counter_freq = ticks.QuadPart;
    }
    return gp_state.perf_counter_freq;
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

int64_t platform_epoch_time()
{
    FILETIME filetime;
    GetSystemTimeAsFileTime(&filetime);
    int64_t epoch_time = _filetime_to_epoch_time(filetime);
    return epoch_time;
}

int64_t platform_startup_epoch_time()
{
    if(gp_state.startup_epoch_time == 0)
        gp_state.startup_epoch_time = platform_epoch_time();

    return gp_state.startup_epoch_time;
}

//returns the number of micro-seconds since the start of the epoch
// with respect to local timezones/daylight saving times and other
int64_t platform_epoch_time_from_local_time(int64_t local_time)
{
    FILETIME local_file_time = _epoch_time_to_filetime(local_time);
    FILETIME global_file_time = {0};

    bool okay = LocalFileTimeToFileTime(&local_file_time, &global_file_time);
    assert(okay); (void) okay;

    int64_t epoch_time = _filetime_to_epoch_time(global_file_time);
    return epoch_time;
}
int64_t platform_local_time_from_epoch_time(int64_t epoch_time)
{
    FILETIME global_file_time = _epoch_time_to_filetime(epoch_time);
    FILETIME local_file_time = {0};

    bool okay = FileTimeToLocalFileTime(&global_file_time, &local_file_time);
    assert(okay); (void) okay;

    int64_t local_time = _filetime_to_epoch_time(global_file_time);
    return local_time;
}

#if 0
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
#endif

Platform_Calendar_Time platform_calendar_time_from_epoch_time(int64_t epoch_time_usec)
{
    const int64_t _EPOCH_YEAR              = (int64_t) 1970;
    const int64_t _MILLISECOND_MICROSECOND = (int64_t) 1000;
    const int64_t _SECOND_MICROSECONDS     = (int64_t) 1000000;
    const int64_t _DAY_SECONDS             = (int64_t) 86400;
    const int64_t _YEAR_SECONDS            = (int64_t) 31556952;
    const int64_t _DAY_MICROSECONDS        = (_DAY_SECONDS * _SECOND_MICROSECONDS);
    const int64_t _YEAR_MICROSECONDS       = (_YEAR_SECONDS * _SECOND_MICROSECONDS);

    (void) _DAY_SECONDS;
    (void) _DAY_MICROSECONDS;

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

    int64_t years_since_epoch = (int64_t) time.year - _EPOCH_YEAR;
    int64_t microsec_diff = epoch_time_usec - years_since_epoch*_YEAR_MICROSECONDS;
    //int64_t microsec_remainder = microsec_diff % YEAR_MICROSECONDS;
    //int64_t day_of_year = (microsec_remainder / DAY_MICROSECONDS);
    //int64_t microsecond = (microsec_remainder % MILLISECOND_MICROSECOND);
    int64_t microsecond = (microsec_diff % _MILLISECOND_MICROSECOND);

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
    
    #ifndef NDEBUG
    int64_t epoch_time_roundtrip = platform_epoch_time_from_calendar_time(time);
    if(epoch_time_roundtrip != epoch_time_usec)
    {
        Platform_Calendar_Time roundtrip_time = platform_calendar_time_from_epoch_time(epoch_time_roundtrip); (void) roundtrip_time;
        assert(epoch_time_roundtrip == epoch_time_usec && "roundtrip must be correct");
    }
    #endif // !NDEBUG

    return time;
}

int64_t platform_epoch_time_from_calendar_time(Platform_Calendar_Time calendar_time)
{
    SYSTEMTIME systime = {0};
    systime.wDay = calendar_time.day;
    systime.wDayOfWeek = calendar_time.day_of_week;
    systime.wHour = calendar_time.hour;
    systime.wMilliseconds = calendar_time.millisecond;
    systime.wMinute = calendar_time.minute;
    systime.wMonth = calendar_time.month + 1;
    systime.wSecond = calendar_time.second;
    systime.wYear = (WORD) calendar_time.year;

    FILETIME filetime;
    bool okay = SystemTimeToFileTime(&systime, &filetime) != 0;
    assert(okay);

    int64_t epoch_time = _filetime_to_epoch_time(filetime);
    epoch_time += calendar_time.microsecond;

    return epoch_time;
}

Platform_Calendar_Time platform_local_calendar_time_from_epoch_time(int64_t epoch_time_usec)
{
    return platform_calendar_time_from_epoch_time(platform_local_time_from_epoch_time(epoch_time_usec));
}
int64_t platform_epoch_time_from_local_calendar_time(Platform_Calendar_Time calendar_time)
{   
    return platform_epoch_time_from_local_time(platform_epoch_time_from_calendar_time(calendar_time));
}

//=========================================
// Filesystem
//=========================================

#define IO_LOCAL_BUFFER_SIZE (MAX_PATH + 32)
#define IO_NORMALIZE_LINUX 0
#define IO_NORMALIZE_DIRECTORY 0
#define IO_NORMALIZE_FILE 0

#define buffer_init_backed(buff, backing_size) \
    _buffer_init_backed((Buffer_Base*) (void*) (buff), sizeof *(buff)->data, _malloca((backing_size) * sizeof *(buff)->data), (backing_size))

#define buffer_resize(buff, new_size) \
    _buffer_resize((Buffer_Base*) (void*) (buff), sizeof *(buff)->data, (new_size))
    
#define buffer_reserve(buff, new_size) \
    _buffer_reserve((Buffer_Base*) (void*) (buff), sizeof *(buff)->data, (new_size))

#define buffer_append(buff, items, items_count) \
    _buffer_append((Buffer_Base*) (void*) (buff), sizeof *(buff)->data, (items), (items_count), sizeof *(items))

#define buffer_deinit(buff) \
    _buffer_deinit((Buffer_Base*) (void*) (buff))

#define buffer_push(buff, item) \
    (buffer_reserve((buff), (buff)->size + 1), \
    (buff)->data[(buff)->size++] = (item))

void* _platform_internal_reallocate(int64_t new_size, void* old_ptr)
{
    if(gp_state.allocator.reallocate)
        return gp_state.allocator.reallocate(gp_state.allocator.context, new_size, old_ptr);
    else
        return platform_heap_reallocate(new_size, old_ptr, sizeof(size_t));
}

static void _buffer_deinit(Buffer_Base* buffer)
{
    if(buffer->is_alloced)
        (void) _platform_internal_reallocate(0, buffer->data);
    
    memset(buffer, 0, sizeof *buffer);
}

static void _buffer_init_backed(Buffer_Base* buffer, int64_t item_size, void* backing, int64_t backing_size)
{
    _buffer_deinit(buffer);
    buffer->data = backing;
    buffer->is_alloced = false;
    buffer->capacity = backing_size;
    memset(backing, 0, backing_size*item_size);
}

static void _buffer_reserve(Buffer_Base* buffer, int64_t item_size, int64_t new_cap)
{
    assert(item_size > 0);
    if(buffer->capacity > 0)
        assert(buffer->size < buffer->capacity);

    if(new_cap >= buffer->capacity)
    {
        void* new_data = NULL;
        int64_t new_capaity = 8;
        while(new_capaity <= new_cap)
            new_capaity *= 2;

        //If was allocated before just realloc. If is backed allocate and copy data over
        if(buffer->is_alloced)
            new_data = _platform_internal_reallocate(new_capaity * item_size, buffer->data);
        else
        {
            new_data = _platform_internal_reallocate(new_capaity * item_size, 0);
            memcpy(new_data, buffer->data, buffer->capacity*item_size);
        }

        //null newly added portion
        memset((char*) new_data + buffer->capacity*item_size, 0, (new_capaity - buffer->capacity)*item_size);
        buffer->capacity = new_capaity;
        buffer->data = new_data;
        buffer->is_alloced = true;
    }
}

static void _buffer_resize(Buffer_Base* buffer, int64_t item_size, int64_t new_size)
{
    _buffer_reserve(buffer, item_size, new_size);
    buffer->size = new_size;
    memset((char*) buffer->data + buffer->size*item_size, 0, item_size);
}

static void _buffer_append(Buffer_Base* buffer, int64_t item_size, const void* data, int64_t data_count, int64_t data_size)
{
    assert(item_size == data_size);
    _buffer_reserve(buffer, item_size, buffer->size + data_count);
    memcpy((char*) buffer->data + buffer->size*item_size, data, data_count*item_size);
    buffer->size += data_count;
    memset((char*) buffer->data + buffer->size*item_size, 0, item_size);
}

static char* _utf16_to_utf8(String_Buffer* append_to, Platform_WString str) 
{
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, str.data, (int) str.size, NULL, 0, NULL, NULL);
    buffer_resize(append_to, utf8len);
    WideCharToMultiByte(CP_UTF8, 0, str.data, (int) str.size, append_to->data, (int) utf8len, 0, 0);
    return append_to->data;
}

static wchar_t* _utf8_to_utf16(WString_Buffer* append_to, Platform_String str) 
{
    int utf16len = MultiByteToWideChar(CP_UTF8, 0, str.data, (int) str.size, NULL, 0);
    buffer_resize(append_to, utf16len);
    MultiByteToWideChar(CP_UTF8, 0, str.data, (int) str.size, append_to->data, (int) utf16len);
    return append_to->data;
}


const wchar_t* _ephemeral_wstring_convert(Platform_String path, bool normalize_path)
{
    enum {RESET_EVERY = 8, MAX_SIZE = MAX_PATH*2, MIN_SIZE = MAX_PATH};
    int64_t *slot = &gp_state.ephemeral_strings_slot;
    WString_Buffer* curr = &gp_state.ephemeral_strings[*slot % _EPHEMERAL_STRING_SIMULTANEOUS];

    //We periodacally shrink the strings so that we can use this
    //function regulary for small and big strings without fearing that we will
    //use too much memory
    if(*slot % RESET_EVERY < _EPHEMERAL_STRING_SIMULTANEOUS)
    {
        if(curr->capacity > MAX_SIZE)
            buffer_deinit(curr);
    }

    buffer_reserve(curr, MIN_SIZE);
    *slot = (*slot + 1) % _EPHEMERAL_STRING_SIMULTANEOUS;
    _utf8_to_utf16(curr, path);
    if(normalize_path)
    {
        for(int64_t i = 0; i < curr->size; i++)
        {
            if(curr->data[i] == '\\')
                curr->data[i] = '/';
        }
    }

    return curr->data;
}

char* _convert_to_utf8_normalize_path(String_Buffer* append_to_or_null, Platform_WString string, int normalize_flag)
{
    (normalize_flag);
    String_Buffer local = {0};
    String_Buffer* append_to = append_to_or_null ? append_to_or_null : &local;

    char* str = _utf16_to_utf8(append_to, string);
    for(int64_t i = 0; i < append_to->size; i++)
    {
        if(str[i] == '\\')
            str[i] = '/';
    }

    return str;
}

void _ephemeral_wstring_deinit_all()
{
    for(int64_t i = 0; i < _EPHEMERAL_STRING_SIMULTANEOUS; i++)
        buffer_deinit(&gp_state.ephemeral_strings[i]);
}

const wchar_t* _ephemeral_path(Platform_String path)
{
    return _ephemeral_wstring_convert(path, true);
}

static void _w_concat(WString_Buffer* output, const wchar_t* a, const wchar_t* b, const wchar_t* c)
{
    int64_t a_size = a ? wcslen(a) : 0;
    int64_t b_size = b ? wcslen(b) : 0;
    int64_t c_size = c ? wcslen(c) : 0;
    int64_t composite_size = a_size + b_size + c_size;
        
    buffer_resize(output, composite_size);
    memmove(output->data,                   a, sizeof(wchar_t) * a_size);
    memmove(output->data + a_size,          b, sizeof(wchar_t) * b_size);
    memmove(output->data + a_size + b_size, c, sizeof(wchar_t) * c_size);
}

const char* platform_translate_error(Platform_Error error)
{
    if(error == PLATFORM_ERROR_OTHER)
        return "Other platform specific error occured";

    char* trasnlated = NULL;
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
    LocalFree(gp_state.translated_errors[gp_state.translated_error_slot]);
    gp_state.translated_errors[gp_state.translated_error_slot] = trasnlated;
    gp_state.translated_error_slot = (gp_state.translated_error_slot + 1) % _TRANSLATED_ERRORS_SIMULATANEOUS;

    //Strips annoying trailing whitespace
    for(int64_t i = length; i-- > 0; )
    {
        if(isspace(trasnlated[i]))
            trasnlated[i] = '\0';
        else
            break;
    }

    return trasnlated;
}

void _translated_deinit_all()
{
    for(int64_t i = 0; i < _TRANSLATED_ERRORS_SIMULATANEOUS; i++)
    {
        LocalFree((HLOCAL) gp_state.translated_errors[i]);
        gp_state.translated_errors[i] = NULL;
    }
}

Platform_Error platform_file_create(Platform_String file_path, bool* was_just_created)
{
    bool state = true;

    const wchar_t* path = _ephemeral_path(file_path);
    HANDLE handle = CreateFileW(path, 0, 0, NULL, OPEN_ALWAYS, 0, NULL);
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

    return _paltform_get_last_error_code_if(state);
}
Platform_Error platform_file_remove(Platform_String file_path, bool* was_deleted_deleted)
{
    const wchar_t* path = _ephemeral_path(file_path);
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    bool state = !!DeleteFileW(path);
        
    if(was_deleted_deleted != NULL)
        *was_deleted_deleted = state;

    DWORD last_error = GetLastError();
    if(last_error == ERROR_FILE_NOT_FOUND)
    {
        state = true;
        if(was_deleted_deleted != NULL)
            *was_deleted_deleted = false;
    }

    return _paltform_get_last_error_code_if(state);
}

Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path)
{       
    const wchar_t* new_path_norm = _ephemeral_path(new_path);
    const wchar_t* old_path_norm = _ephemeral_path(old_path);
    bool state = !!MoveFileExW(old_path_norm, new_path_norm, MOVEFILE_COPY_ALLOWED);
    
    return _paltform_get_last_error_code_if(state);
}

Platform_Error platform_file_copy(Platform_String new_path, Platform_String old_path)
{
    const wchar_t* new_path_norm = _ephemeral_path(new_path);
    const wchar_t* old_path_norm = _ephemeral_path(old_path);
    bool state = !!CopyFileW(old_path_norm, new_path_norm, true);
    
    return _paltform_get_last_error_code_if(state);
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

Platform_Error platform_file_memory_map(Platform_String file_path, int64_t desired_size_or_zero, Platform_Memory_Mapping* mapping)
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
        
    const wchar_t* path = _ephemeral_path(file_path);
    hFile = CreateFileW(
        path,                            // lpFileName
        GENERIC_READ | GENERIC_WRITE,          // dwDesiredAccess
        FILE_SHARE_READ | FILE_SHARE_WRITE,    // dwShareMode
        NULL,                                  // lpSecurityAttributes
        disposition,                           // dwCreationDisposition
        FILE_ATTRIBUTE_NORMAL,                 // dwFlagsAndAttributes
        0);                                    // hTemplateFile

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

    {
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
    }

    error: {
        DWORD err = GetLastError();
        if(hMap != INVALID_HANDLE_VALUE && hMap != 0)
            CloseHandle(hMap);
        if(hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);

        return err;
    }
}

static Platform_Link_Type _get_link_type(const wchar_t* directory_path)
{
    HANDLE file = CreateFileW(directory_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    size_t requiredSize = GetFinalPathNameByHandleW(file, NULL, 0, FILE_NAME_NORMALIZED);
    CloseHandle(file);

    Platform_Link_Type link_type = PLATFORM_LINK_TYPE_NOT_LINK;
    if(requiredSize == 0)
        link_type = PLATFORM_LINK_TYPE_OTHER;
    
    return link_type;
}

Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null)
{    
    WIN32_FILE_ATTRIBUTE_DATA native_info = {0};
    Platform_File_Info info = {0};
    
    const wchar_t* path = _ephemeral_path(file_path);
    bool state = !!GetFileAttributesExW(path, GetFileExInfoStandard, &native_info);
        
    if(native_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        info.link_type = _get_link_type(path);
    if(!state)
        return _paltform_get_last_error_code_if(state);
            
    info.created_epoch_time = _filetime_to_epoch_time(native_info.ftCreationTime);
    info.last_access_epoch_time = _filetime_to_epoch_time(native_info.ftLastAccessTime);
    info.last_write_epoch_time = _filetime_to_epoch_time(native_info.ftLastWriteTime);
    info.size = ((int64_t) native_info.nFileSizeHigh << 32) | ((int64_t) native_info.nFileSizeLow);
        
    if(native_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        info.type = PLATFORM_FILE_TYPE_DIRECTORY;
    else
        info.type = PLATFORM_FILE_TYPE_FILE;

    if(info_or_null)
        *info_or_null = info;
    return _paltform_get_last_error_code_if(state);
}

BOOL _platform_directory_exists(const wchar_t* szPath)
{
  DWORD dwAttrib = GetFileAttributesW(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

Platform_Error platform_directory_create(Platform_String dir_path, bool* was_just_created_or_null)
{
    const wchar_t* path = _ephemeral_path(dir_path);
    bool state = true;
    bool was_created = true;
    if(_platform_directory_exists(path))
        was_created = false;
    else
    {
        state = !!CreateDirectoryW(path, NULL);
        was_created = state;
    }

    if(was_just_created_or_null)
        *was_just_created_or_null = was_created;

    return _paltform_get_last_error_code_if(state);
}
    
Platform_Error platform_directory_remove(Platform_String dir_path, bool* was_just_deleted_or_null)
{
    const wchar_t* path = _ephemeral_path(dir_path);
    bool state = true;
    bool was_deleted = true;
    if(_platform_directory_exists(path) == false)
        was_deleted = false;
    else
    {
        state = !!RemoveDirectoryW(path);
        was_deleted = state;
    }

    if(was_just_deleted_or_null)
        *was_just_deleted_or_null = was_deleted;

    return _paltform_get_last_error_code_if(state);
}

static char* _alloc_full_path(String_Buffer* buffer_or_null, const wchar_t* local_path, int normalize_flag)
{
    (void) normalize_flag;
    WString_Buffer full_path = {0};
    buffer_init_backed(&full_path, IO_LOCAL_BUFFER_SIZE);

    int64_t needed_size = GetFullPathNameW(local_path, 0, NULL, NULL);
    if(needed_size > full_path.size)
    {
        buffer_resize(&full_path, needed_size);
        needed_size = GetFullPathNameW(local_path, (DWORD) full_path.size, full_path.data, NULL);
    }
    
    Platform_WString full_path_str = {full_path.data, full_path.size};
    char* out = _convert_to_utf8_normalize_path(buffer_or_null, full_path_str, normalize_flag);
    buffer_deinit(&full_path);
    return out;
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
    WString_Buffer built_path = {0};
    buffer_init_backed(&built_path, IO_LOCAL_BUFFER_SIZE);
    _w_concat(&built_path, dir_path, file_mask, NULL);

    Directory_Visitor visitor = {0};
    assert(built_path.data != NULL);
    visitor.first_found = FindFirstFileW(built_path.data, &visitor.current_entry);
    while(visitor.failed == false && visitor.first_found != INVALID_HANDLE_VALUE)
    {
        if(wcscmp(visitor.current_entry.cFileName, L".") == 0
            || wcscmp(visitor.current_entry.cFileName, L"..") == 0)
            visitor.failed = !FindNextFileW(visitor.first_found, &visitor.current_entry);
        else
            break;
    }

    buffer_deinit(&built_path);
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

DEFINE_BUFFER_TYPE(Platform_Directory_Entry, Platform_Directory_Entry_Buffer);

static Platform_Error _directory_list_contents_alloc(const wchar_t* directory_path, Platform_Directory_Entry_Buffer* entries, int64_t max_depth)
{
    typedef struct Dir_Context {
        Directory_Visitor visitor;
        WString_Buffer path;    
        int64_t depth;          
        int64_t index;          
    } Dir_Context;

    DEFINE_BUFFER_TYPE(Dir_Context, Dir_Context_Buffer);

    Platform_Error error = PLATFORM_ERROR_OK;
    Dir_Context_Buffer stack = {0};
    WString_Buffer built_path = {0};
    Platform_WString directory_path_str = {0};
    directory_path_str.data = directory_path;
    directory_path_str.size = directory_path ? wcslen(directory_path) : 0;

    buffer_init_backed(&built_path, IO_LOCAL_BUFFER_SIZE);
    buffer_init_backed(&stack, 16);

    Dir_Context first = {0};
    first.visitor = _directory_iterate_init(directory_path, WIO_FILE_MASK_ALL);

    if(_directory_iterate_has(&first.visitor) == false)
        error = _paltform_get_last_error_code_if(false);
    else
    {
        buffer_append(&first.path, directory_path_str.data, directory_path_str.size); //TODO: no path copying!
        buffer_push(&stack, first);
        const int64_t MAX_RECURSION = 10000;
        for(int64_t reading_from = 0; reading_from < stack.size; reading_from++)
        {
            for(;;)
            {
                Dir_Context* dir_context = &stack.data[reading_from];
                Directory_Visitor* visitor = &dir_context->visitor;
                if(_directory_iterate_has(visitor) == false)
                    break;

                _w_concat(&built_path, dir_context->path.data, L"\\", visitor->current_entry.cFileName);
        
                Platform_File_Info info = {0};
                info.created_epoch_time = _filetime_to_epoch_time(visitor->current_entry.ftCreationTime);
                info.last_access_epoch_time = _filetime_to_epoch_time(visitor->current_entry.ftLastAccessTime);
                info.last_write_epoch_time = _filetime_to_epoch_time(visitor->current_entry.ftLastWriteTime);
                info.size = ((int64_t) visitor->current_entry.nFileSizeHigh << 32) | ((int64_t) visitor->current_entry.nFileSizeLow);
        
                info.type = PLATFORM_FILE_TYPE_FILE;
                if(visitor->current_entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    info.type = PLATFORM_FILE_TYPE_DIRECTORY;
                else
                    info.type = PLATFORM_FILE_TYPE_FILE;

                if(visitor->current_entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    info.link_type = _get_link_type(built_path.data);  

                int flag = IO_NORMALIZE_LINUX;
                if(info.type == PLATFORM_FILE_TYPE_DIRECTORY)
                    flag |= IO_NORMALIZE_DIRECTORY;
                else
                    flag |= IO_NORMALIZE_FILE;

                Platform_Directory_Entry entry = {0};
                entry.info = info;
                entry.path = _alloc_full_path(NULL, built_path.data, flag);
                entry.directory_depth = dir_context->depth;
                buffer_push(entries, entry);

                bool recursion = dir_context->depth + 1 < max_depth || max_depth <= 0;
                if(info.type == PLATFORM_FILE_TYPE_DIRECTORY && recursion)
                {
                    Dir_Context next = {0};
                    buffer_append(&next.path, built_path.data, built_path.size);
                    next.depth = dir_context->depth + 1;
                    next.visitor = _directory_iterate_init(next.path.data, WIO_FILE_MASK_ALL);

                    assert(next.depth < MAX_RECURSION && "must not get stuck in an infinite loop");
                    buffer_push(&stack, next);
                }
                
                //In case we reallocated
                dir_context = &stack.data[reading_from];
                visitor = &dir_context->visitor;
                _directory_iterate_next(visitor); 
                dir_context->index++;
            }
        }
        
        //Null terminate the entries
        Platform_Directory_Entry terminator = {0};
        buffer_push(entries, terminator);
    }

    for(int64_t i = 0; i < stack.size; i++)
    {
        Dir_Context* dir_context = &stack.data[i];
        buffer_deinit(&dir_context->path);
        _directory_iterate_deinit(&dir_context->visitor);
    }

    buffer_deinit(&stack);
    buffer_deinit(&built_path);
    return error;
}

Platform_Error platform_directory_list_contents_alloc(Platform_String directory_path, Platform_Directory_Entry** entries, int64_t* entries_count, int64_t max_depth)
{
    assert(entries != NULL && entries_count != NULL);
    Platform_Directory_Entry_Buffer entries_stack = {0};

    const wchar_t* path = _ephemeral_path(directory_path);
    Platform_Error error = _directory_list_contents_alloc(path, &entries_stack, max_depth);

    if(error == PLATFORM_ERROR_OK && entries)
        *entries = entries_stack.data;
    else
        buffer_deinit(&entries_stack);

    if(entries_count)
        *entries_count = entries_stack.size > 0 ? entries_stack.size - 1 : 0;

    return error;
}


//#pragma comment(lib, "dwmapi.lib")

void platform_directory_list_contents_free(Platform_Directory_Entry* entries)
{
    if(entries == NULL)
        return;

    int64_t i = 0;
    for(; entries[i].path != NULL; i++)
        _platform_internal_reallocate(0, entries[i].path);
          
    _platform_internal_reallocate(0, entries);
}

Platform_Error platform_directory_set_current_working(Platform_String new_working_dir)
{
    const wchar_t* path = _ephemeral_path(new_working_dir);

    bool state = _wchdir(path) == 0;
    gp_state.current_working_generation += 1;
    return _paltform_get_last_error_code_if(state);
}

const char* platform_directory_get_current_working()
{
    if(gp_state.current_working_cached_generation <= gp_state.current_working_generation)
    {
        wchar_t* current_working = _wgetcwd(NULL, 0);
        _alloc_full_path(&gp_state.directory_current_working_cached, current_working, IO_NORMALIZE_DIRECTORY);
        free(current_working);

        gp_state.current_working_cached_generation = gp_state.current_working_generation + 1;
    }
    
    assert(gp_state.directory_current_working_cached.data != NULL);
    return gp_state.directory_current_working_cached.data;
}

const char* platform_get_executable_path()
{
    if(gp_state.directory_executable_cached.data == NULL)
    {
        WString_Buffer wide = {0};
        buffer_init_backed(&wide, IO_LOCAL_BUFFER_SIZE);
        buffer_resize(&wide, MAX_PATH);

        for(int64_t i = 0; i < 16; i++)
        {
            buffer_resize(&wide, wide.size * 2);
            int64_t len = GetModuleFileNameW(NULL, wide.data, (DWORD) wide.size);
            if(len < wide.size)
                break;
        }
        
        _alloc_full_path(&gp_state.directory_executable_cached, wide.data, IO_NORMALIZE_FILE);
        buffer_deinit(&wide);
    }
    
    assert(gp_state.directory_executable_cached.data != NULL);
    return gp_state.directory_executable_cached.data;
}

const char* platform_get_executable_path_process()
{
    if(gp_state.directory_executable_cached.data == NULL)
    {
        HANDLE process = GetCurrentProcess();
        WString_Buffer wide = {0};
        buffer_init_backed(&wide, IO_LOCAL_BUFFER_SIZE);
        buffer_resize(&wide, MAX_PATH);

        bool had_success = false;
        for(int64_t i = 0; i < 16; i++)
        {
            DWORD size = (DWORD) wide.size;
            buffer_resize(&wide, wide.size * 2);
            if(QueryFullProcessImageNameW(process, 0, wide.data, &size))
            {
                buffer_resize(&wide, size);
                had_success = true;
                break;
            }
        }
        
        printf("%s\n", platform_translate_error(_paltform_get_last_error_code_if(false)));
        assert(had_success);
        _alloc_full_path(&gp_state.directory_executable_cached, wide.data, IO_NORMALIZE_FILE);
        buffer_deinit(&wide);
    }
    
    assert(gp_state.directory_executable_cached.data != NULL);
    return gp_state.directory_executable_cached.data;
}

static void _platform_cached_directory_deinit()
{
    buffer_deinit(&gp_state.directory_executable_cached);
    buffer_deinit(&gp_state.directory_current_working_cached);
}

/*
typedef bool (*_File_Watch_Func)(void* context);

typedef struct _Platform_File_Watch_Context {
    HANDLE watch_handle;
    bool (*user_func)(void* context);
    void* user_context;
    bool thread_exited;
} _Platform_File_Watch_Context;

void _file_watch_func(void* context)
{
    _Platform_File_Watch_Context* watch_context = (_Platform_File_Watch_Context*) context;
    assert(watch_context && "badly called");

    while(true)
    {
        DWORD wait_state = WaitForSingleObject(watch_context->watch_handle, INFINITE);
        if(wait_state == WAIT_OBJECT_0)
        {
            bool user_state = watch_context->user_func(watch_context->user_context);
            if(user_state == false)
                break;
        }
        //something else happened. It shouldnt but it did. We exit the thread.
        else
        {
            assert(wait_state != WAIT_ABANDONED && "only happens for mutexes");
            assert(wait_state != WAIT_TIMEOUT && "we didnt set timeout");
            break;
        }

        if(FindNextChangeNotification(watch_context->watch_handle) == false)
        {
            //Some error occured
            assert(false);
            break;
        }
    }

    if(watch_context->watch_handle != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(watch_context->watch_handle);

    watch_context->thread_exited = true;
}

Platform_Error platform_file_watch(Platform_File_Watch* file_watch, Platform_String file_or_dir_path, int32_t file_wacht_flags, bool (*async_func)(void* context), void* context)
{
    platform_file_unwatch(file_watch);
    assert(async_func != NULL);
    
    HANDLE watch_handle = INVALID_HANDLE_VALUE;
    _Platform_File_Watch_Context* watch_context = (_Platform_File_Watch_Context*) _platform_internal_reallocate(sizeof *watch_context, NULL);
    file_watch->handle = watch_context;
    Platform_Error error = PLATFORM_ERROR_OK;
    if(watch_context != NULL)
    {
        BOOL watch_subtree = false;
        DWORD notify_filter = 0;

        if(file_wacht_flags & PLATFORM_FILE_WATCH_CHANGE)
            notify_filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
        
        if(file_wacht_flags & PLATFORM_FILE_WATCH_DIR_NAME)
            notify_filter |= FILE_NOTIFY_CHANGE_DIR_NAME;
        
        if(file_wacht_flags & PLATFORM_FILE_WATCH_FILE_NAME)
            notify_filter |= FILE_NOTIFY_CHANGE_FILE_NAME;
        
        if(file_wacht_flags & PLATFORM_FILE_WATCH_ATTRIBUTES)
            notify_filter |= FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES;
        
        if(file_wacht_flags & PLATFORM_FILE_WATCH_RECURSIVE)
            watch_subtree = true;
        
        const wchar_t* path = _ephemeral_path(file_or_dir_path);
        watch_handle = FindFirstChangeNotificationW(path, watch_subtree, notify_filter);
        error = _paltform_get_last_error_code_if(watch_handle != INVALID_HANDLE_VALUE);

        if(error == PLATFORM_ERROR_OK)
        {
            watch_context->user_context = context;
            watch_context->user_func = async_func;
            watch_context->watch_handle = watch_handle;
            error = platform_thread_launch(&file_watch->thread, _file_watch_func, watch_context, 0);
        }
    }

    if(error != PLATFORM_ERROR_OK)
        platform_file_unwatch(file_watch);

    return error;
}
void platform_file_unwatch(Platform_File_Watch* file_watch)
{
    _Platform_File_Watch_Context* watch_context = (_Platform_File_Watch_Context*) file_watch->handle;
    if(watch_context)
    {
        if(watch_context->thread_exited == false)
        {
            //@TODO: inspect if this does what we want
            //TerminateThread (file_watch->thread.handle, 0);
            FindCloseChangeNotification(watch_context->watch_handle);
        }
        _platform_internal_reallocate(0, watch_context);
    }
    memset(file_watch, 0, sizeof *file_watch);
}
*/

//=========================================
// DLL management
//=========================================

Platform_Error platform_dll_load(Platform_DLL* dll, Platform_String path)
{
    const wchar_t* wpath = _ephemeral_wstring_convert(path, false);
    HMODULE hmodule = LoadLibraryW((const WCHAR*)wpath);
    
    if(dll)
        dll->handle = (void*) hmodule;

    if(hmodule == NULL)
        return (Platform_Error) GetLastError();
    else
        return (PLATFORM_ERROR_OK);
}

void platform_dll_unload(Platform_DLL* dll)
{
     HMODULE hmodule = (HMODULE)dll->handle;
     FreeLibrary(hmodule);
     memset(dll, 0, sizeof *dll);
}

void* platform_dll_get_function(Platform_DLL* dll, Platform_String name)
{
    String_Buffer temp = {0};
    buffer_init_backed(&temp, 256);
    buffer_append(&temp, name.data, name.size);
    HMODULE hmodule = (HMODULE)dll->handle;
    void* result = (void*) GetProcAddress(hmodule, temp.data);

    buffer_deinit(&temp);
    return result;
}

//=========================================
// Window managmenet
//=========================================
Platform_Window_Popup_Controls platform_window_make_popup(Platform_Window_Popup_Style desired_style, Platform_String message, Platform_String title)
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
    const wchar_t* message_wide = _ephemeral_wstring_convert(message, false);
    const wchar_t* title_wide = _ephemeral_wstring_convert(title, false);
    value = MessageBoxW(0, message_wide, title_wide, style | icon);

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

//CALLSTACK

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
    if(stack_size <= 0)
        return 0;

    int64_t captured = CaptureStackBackTrace((DWORD) skip_count + 1, (DWORD) stack_size, stack, NULL);
    return captured;
}

#define MAX_MODULES 128 
#define MAX_NAME_LEN 2048

typedef struct {
    CRITICAL_SECTION lock;
    bool   init;
    DWORD  error;
} Stack_Trace_State;

Stack_Trace_State stack_trace_state = {0};

static void _platform_stack_trace_init(const char* search_path)
{
    if(stack_trace_state.init)
        return;

    InitializeCriticalSection(&stack_trace_state.lock);
    EnterCriticalSection(&stack_trace_state.lock);

    if (!SymInitialize(GetCurrentProcess(), search_path, false)) 
    {
        assert(false);
        stack_trace_state.error = GetLastError();
    }
    else
    {
        DWORD symOptions = SymGetOptions();
        symOptions |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
        SymSetOptions(symOptions);
    
        DWORD module_handles_size_needed = 0;
        HMODULE module_handles[MAX_MODULES] = {0};
        TCHAR module_filename[MAX_NAME_LEN] = {0};
        TCHAR module_name[MAX_NAME_LEN] = {0};
        EnumProcessModules(GetCurrentProcess(), module_handles, sizeof(module_handles), &module_handles_size_needed);
    
        DWORD module_count = module_handles_size_needed/sizeof(HMODULE);
        for(int64_t i = 0; i < module_count; i++)
        {
            HMODULE module_handle = module_handles[i];
            assert(module_handle != 0);
            MODULEINFO module_info = {0};
            GetModuleInformation(GetCurrentProcess(), module_handle, &module_info, sizeof(module_info));
            GetModuleFileNameExW(GetCurrentProcess(), module_handle, module_filename, sizeof(module_filename));
            GetModuleBaseNameW(GetCurrentProcess(), module_handle, module_name, sizeof(module_name));
        
            bool load_state = SymLoadModuleExW(GetCurrentProcess(), 0, module_filename, module_name, (DWORD64)module_info.lpBaseOfDll, (DWORD) module_info.SizeOfImage, 0, 0);
            if(load_state == false)
            {
                assert(false);
                stack_trace_state.error = GetLastError();
            }
        }
    }
    
    stack_trace_state.init = true;
    LeaveCriticalSection(&stack_trace_state.lock);
}

static void _platform_stack_trace_deinit()
{
    SymCleanup(GetCurrentProcess());
    DeleteCriticalSection(&stack_trace_state.lock);
}

void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, const void** stack, int64_t stack_size)
{
    if(stack_size == 0)
        return;

    _platform_stack_trace_init("");
    EnterCriticalSection(&stack_trace_state.lock);
    char symbol_info_data[sizeof(SYMBOL_INFO) + MAX_NAME_LEN + 1] = {0};

    DWORD offset_from_symbol = 0;
    IMAGEHLP_LINE64 line = {0};
    line.SizeOfStruct = sizeof line;

    memset(translated, 0, stack_size * sizeof *translated);
    for(int64_t i = 0; i < stack_size; i++)
    {
        Platform_Stack_Trace_Entry* entry = translated + i;
        DWORD64 address = (DWORD64) stack[i];
        entry->address = (void*) stack[i];

        if (address == 0)
            continue;

        memset(symbol_info_data, '\0', sizeof symbol_info_data);

        SYMBOL_INFO* symbol_info = (SYMBOL_INFO*) symbol_info_data;
        symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol_info->MaxNameLen = MAX_NAME_LEN;
        DWORD64 displacement = 0;
        SymFromAddr(GetCurrentProcess(), address, &displacement, symbol_info);
            
        if (symbol_info->Name[0] != '\0')
        {
            UnDecorateSymbolName(symbol_info->Name, entry->function, sizeof entry->function, UNDNAME_COMPLETE);
        }
           
        IMAGEHLP_MODULE module_info = {0};
        module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE);
        bool module_info_state = SymGetModuleInfo64(GetCurrentProcess(), address, &module_info);
        if(module_info_state)
        {
            int64_t copy_size = sizeof module_info.ImageName;
            if(copy_size > sizeof entry->module - 1)
                copy_size = sizeof entry->module - 1;

            memmove(entry->module, module_info.ImageName, copy_size);
        }
            
        if (SymGetLineFromAddr64(GetCurrentProcess(), address, &offset_from_symbol, &line)) 
        {
            entry->line = line.LineNumber;
            
            int64_t copy_size = strlen(line.FileName);
            if(copy_size > sizeof entry->file - 1)
                copy_size = sizeof entry->file - 1;

            memmove(entry->file, line.FileName, copy_size);
        }
        
        //null terminate everything just in case
        entry->module[sizeof entry->module - 1] = '\0';
        entry->file[sizeof entry->file - 1] = '\0';
        entry->function[sizeof entry->function - 1] = '\0';
    }
    LeaveCriticalSection(&stack_trace_state.lock);
}

static int64_t _platform_stack_trace_walk(CONTEXT context, HANDLE process, HANDLE thread, DWORD image_type, void** frames, int64_t frame_count, int64_t skip_count)
{
    //Should have probably be init by this point but whatever...
    _platform_stack_trace_init("");

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
    
    EnterCriticalSection(&stack_trace_state.lock);
    (void) process;
    int64_t i = 0;
    for(; i < frame_count; i++)
    {
        CONTEXT* escaped_context = native_image == IMAGE_FILE_MACHINE_I386 
            ? NULL
            : &context;
        bool ok = StackWalk64(native_image, process, thread, &frame, escaped_context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
        if (ok == false)
            break;

        if(skip_count > 0)
        {
            skip_count--;
            i --;
            continue;
        }
        
        if (frame.AddrPC.Offset != 0)
            frames[i] = (void*) frame.AddrPC.Offset;
        else
            break;
    }
    LeaveCriticalSection(&stack_trace_state.lock);
    
    return i;
}

typedef void (*Sandbox_Error_Func)(void* error_context, Platform_Sandbox_Error error);

#define SANDBOX_MAX_STACK 256
#define SANDBOX_JUMP_VALUE 123

#include <setjmp.h>
#include <signal.h>

#pragma warning(disable:4324) //Dissable "structure was padded due to alignment specifier"
typedef struct Platform_Sandbox_State {
    Platform_Exception exception;
    void* stack[SANDBOX_MAX_STACK];
    int64_t stack_size;
    int64_t epoch_time;

    int32_t signal_handler_depth;
    jmp_buf jump_buffer;
    CONTEXT context;
} Platform_Sandbox_State;

__declspec(thread) Platform_Sandbox_State sandbox_state = {PLATFORM_EXCEPTION_NONE};

void _sandbox_abort_filter(int signal)
{
    int64_t epoch_time = platform_epoch_time();
    if(sandbox_state.signal_handler_depth <= 0)
        return;
    
    Platform_Sandbox_State* curr_state = &sandbox_state;
    if(signal == SIGABRT)
        curr_state->exception = PLATFORM_EXCEPTION_ABORT;
    else if(signal == SIGTERM)
        curr_state->exception = PLATFORM_EXCEPTION_TERMINATE;
    else
    {
        assert(false && "badly registred signal handler");
        curr_state->exception = PLATFORM_EXCEPTION_OTHER;
    }
    curr_state->stack_size = platform_capture_call_stack(curr_state->stack, SANDBOX_MAX_STACK, 1);
    curr_state->epoch_time = epoch_time;

    longjmp(curr_state->jump_buffer, SANDBOX_JUMP_VALUE);
}

LONG WINAPI _sandbox_exception_filter(EXCEPTION_POINTERS * ExceptionInfo)
{
    int64_t epoch_time = platform_epoch_time();

    if(sandbox_state.signal_handler_depth <= 0)
        return EXCEPTION_CONTINUE_SEARCH;

    Platform_Exception exception = PLATFORM_EXCEPTION_OTHER;
    switch(ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
        //Non errors:
        case CONTROL_C_EXIT: return EXCEPTION_CONTINUE_SEARCH;
        case STILL_ACTIVE: return EXCEPTION_CONTINUE_SEARCH;

        //Errors:
        case EXCEPTION_ACCESS_VIOLATION:
            exception = PLATFORM_EXCEPTION_ACCESS_VIOLATION;
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            exception = PLATFORM_EXCEPTION_ACCESS_VIOLATION;
            break;
        case EXCEPTION_BREAKPOINT:
            exception = PLATFORM_EXCEPTION_BREAKPOINT;
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            exception = PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT;
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            exception = PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND;
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            exception = PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO;
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            exception = PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT;
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            exception = PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION;
            break;
        case EXCEPTION_FLT_OVERFLOW:
            exception = PLATFORM_EXCEPTION_FLOAT_OVERFLOW;
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            exception = PLATFORM_EXCEPTION_STACK_OVERFLOW;
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            exception = PLATFORM_EXCEPTION_FLOAT_UNDERFLOW;
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            exception = PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION;
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            exception = PLATFORM_EXCEPTION_PAGE_ERROR;
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            exception = PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO;
            break;
        case EXCEPTION_INT_OVERFLOW:
            exception = PLATFORM_EXCEPTION_INT_OVERFLOW;
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            exception = PLATFORM_EXCEPTION_OTHER;
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            exception = PLATFORM_EXCEPTION_OTHER;
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            exception = PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION;
            break;
        case EXCEPTION_SINGLE_STEP:
            exception = PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP;
            break;
        case EXCEPTION_STACK_OVERFLOW:
            exception = PLATFORM_EXCEPTION_STACK_OVERFLOW;
            break;
        case PLATFORM_EXCEPTION_ABORT:
            exception = PLATFORM_EXCEPTION_ABORT;
            break;
        case PLATFORM_EXCEPTION_TERMINATE:
            exception = PLATFORM_EXCEPTION_TERMINATE;
            break;
        default:
            exception = PLATFORM_EXCEPTION_OTHER;
            break;
    }
    
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    Platform_Sandbox_State* curr_state = &sandbox_state;
    curr_state->epoch_time = epoch_time;
    curr_state->exception = exception;
    curr_state->context = *ExceptionInfo->ContextRecord;
    curr_state->stack_size = _platform_stack_trace_walk(curr_state->context, process, thread, 0, (void**) &curr_state->stack, SANDBOX_MAX_STACK, 0);

    return EXCEPTION_EXECUTE_HANDLER;
}

Platform_Exception platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context,
    void (*error_func)(void* error_context, Platform_Sandbox_Error error),
    void* error_context)
{
    //LPTOP_LEVEL_EXCEPTION_FILTER prev_exception_filter = SetUnhandledExceptionFilter(_sandbox_exception_filter);
    void* vector_exception_handler = AddVectoredExceptionHandler(1, _sandbox_exception_filter);
    int prev_error_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    
    _crt_signal_t prev_abrt = signal(SIGABRT, _sandbox_abort_filter);
    _crt_signal_t prev_term = signal(SIGTERM, _sandbox_abort_filter);
        
    Platform_Exception exception = PLATFORM_EXCEPTION_NONE;
    Platform_Sandbox_State prev_state = sandbox_state;
    memset(&sandbox_state, 0, sizeof sandbox_state);

    sandbox_state.signal_handler_depth += 1;

    bool had_exception = false;
    switch(setjmp(sandbox_state.jump_buffer))
    {
        default: {
            __try { 
                sandboxed_func(sandbox_context);
            } 
            __except(_sandbox_exception_filter(GetExceptionInformation())) { 
                had_exception = true;
            }
            break;
        }

        case SANDBOX_JUMP_VALUE:
            had_exception = true;
            break;
    }

    if(had_exception)
    {
        //just in case we repeatedly exception or something like that
        Platform_Sandbox_State error_state = sandbox_state;
        exception = error_state.exception;

        Platform_Sandbox_Error error = {exception};
        error.call_stack = error_state.stack;
        error.call_stack_size = error_state.stack_size;
        error.execution_context = &error_state.context;
        error.execution_context_size = (int64_t) sizeof error_state.context;
        error.epoch_time = error_state.epoch_time;

        if(error_func)
            error_func(error_context, error);
    }

    sandbox_state.signal_handler_depth -= 1;
    if(sandbox_state.signal_handler_depth < 0)
        sandbox_state.signal_handler_depth = 0;

    sandbox_state = prev_state;
    signal(SIGABRT, prev_abrt);
    signal(SIGTERM, prev_term);

    SetErrorMode(prev_error_mode);
    if(vector_exception_handler != NULL)
        RemoveVectoredExceptionHandler(vector_exception_handler);
    //SetUnhandledExceptionFilter(prev_exception_filter);
    return exception;
}

const char* platform_exception_to_string(Platform_Exception error)
{
    switch(error)
    {
        case PLATFORM_EXCEPTION_NONE: return "PLATFORM_EXCEPTION_NONE";
        case PLATFORM_EXCEPTION_ACCESS_VIOLATION: return "PLATFORM_EXCEPTION_ACCESS_VIOLATION";
        case PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT: return "PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT";
        case PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND: return "PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND";
        case PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO: return "PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO";
        case PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT: return "PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT";
        case PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION: return "PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION";
        case PLATFORM_EXCEPTION_FLOAT_OVERFLOW: return "PLATFORM_EXCEPTION_FLOAT_OVERFLOW";
        case PLATFORM_EXCEPTION_FLOAT_UNDERFLOW: return "PLATFORM_EXCEPTION_FLOAT_UNDERFLOW";
        case PLATFORM_EXCEPTION_FLOAT_OTHER: return "PLATFORM_EXCEPTION_FLOAT_OTHER";
        case PLATFORM_EXCEPTION_PAGE_ERROR: return "PLATFORM_EXCEPTION_PAGE_ERROR";
        case PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO: return "PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO";
        case PLATFORM_EXCEPTION_INT_OVERFLOW: return "PLATFORM_EXCEPTION_INT_OVERFLOW";
        case PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION: return "PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION";
        case PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION: return "PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION";
        case PLATFORM_EXCEPTION_BREAKPOINT: return "PLATFORM_EXCEPTION_BREAKPOINT";
        case PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP: return "PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP";
        case PLATFORM_EXCEPTION_STACK_OVERFLOW: return "PLATFORM_EXCEPTION_STACK_OVERFLOW";
        case PLATFORM_EXCEPTION_ABORT: return "PLATFORM_EXCEPTION_ABORT";
        case PLATFORM_EXCEPTION_TERMINATE: return "PLATFORM_EXCEPTION_TERMINATE";
        case PLATFORM_EXCEPTION_OTHER: return "PLATFORM_EXCEPTION_OTHER";
        default:
            return "PLATFORM_EXCEPTION_OTHER";
    }
}

bool _platform_set_console_output_escape_sequences()
{
    // Set output mode to handle virtual terminal sequences
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwOriginalOutMode = 0;
    if (!GetConsoleMode(hOut, &dwOriginalOutMode))
        return false;

    DWORD dwOutMode = dwOriginalOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(hOut, dwOutMode))
    {
        dwOutMode = dwOriginalOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hOut, dwOutMode))
            return false;
    }

    return true;
}

void _platform_set_console_utf8()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
}

void platform_init(Platform_Allocator* allocator)
{
    if(allocator)
        gp_state.allocator = *allocator;
    platform_deinit();

    platform_perf_counter();
    platform_startup_epoch_time();
    platform_perf_counter_startup();

    _platform_set_console_utf8();
    _platform_set_console_output_escape_sequences();
    _platform_stack_trace_init("");
}
void platform_deinit()
{
    _translated_deinit_all();
    _ephemeral_wstring_deinit_all();
    _platform_cached_directory_deinit();
    _platform_stack_trace_deinit();
    memset(&gp_state, 0, sizeof(gp_state));
}