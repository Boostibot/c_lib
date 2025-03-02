#ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef APIENTRY
    #undef APIENTRY
#endif

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef VC_EXTRALEAN
    #define VC_EXTRALEAN
#endif

#ifndef UNICODE
    #define UNICODE
#endif 

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include "platform.h"

#pragma comment(lib, "dwmapi.lib")

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

#pragma warning(disable:4255) //Disable "no function prototype given: converting '()' to '(void)"  
#pragma warning(disable:5045) //Disable "Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified"  

#undef near
#undef far

//=========================================
// Virtual memory
//=========================================

Platform_Error _platform_error_code(bool state)
{
    Platform_Error err = PLATFORM_ERROR_OK;
    if(!state)
    {
        err = (Platform_Error) GetLastError();
        //If we failed yet there is no error 
        // set to custom error.
        if(err == 0)
            err = PLATFORM_ERROR_OTHER;
    }
    return err;
}

Platform_Error _platform_error_code_posix(bool state)
{
    Platform_Error err = PLATFORM_ERROR_OK;
    if(!state)
    {
        err = (Platform_Error) errno;
        //If we failed yet there is no error 
        // set to custom error.
        if(err == 0)
            err = PLATFORM_ERROR_OTHER;
        else
            err = err | (1 << 29);
    }
    return err;
}

Platform_Error platform_virtual_reallocate(void** output_adress_or_null, void* address, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection)
{
    void* out_addr = NULL;
    Platform_Error out = PLATFORM_ERROR_OK;
    
    if(action == PLATFORM_VIRTUAL_ALLOC_RELEASE)
        out = _platform_error_code(!!VirtualFree(address, 0, MEM_RELEASE));  

    else if(action == PLATFORM_VIRTUAL_ALLOC_DECOMMIT)
    {
        out_addr = address;
        //Disable warning about MEM_DECOMMIT without MEM_RELEASE because thats the whole point of this operation we are doing here.
        #pragma warning(disable:6250)
        out = _platform_error_code(!!VirtualFree(address, bytes, MEM_DECOMMIT));  
        #pragma warning(default:6250)
    }
    else 
    {
        int prot = 0;
        if(protection == PLATFORM_MEMORY_PROT_READ)
            prot = PAGE_READONLY;
        if(protection & PLATFORM_MEMORY_PROT_WRITE)
            prot = PAGE_READWRITE;
        if(protection == PLATFORM_MEMORY_PROT_EXECUTE)
            prot = PAGE_EXECUTE;
        if(protection == (PLATFORM_MEMORY_PROT_READ | PLATFORM_MEMORY_PROT_EXECUTE))
            prot = PAGE_EXECUTE_READ;
        if(protection & (PLATFORM_MEMORY_PROT_WRITE | PLATFORM_MEMORY_PROT_EXECUTE))
            prot = PAGE_EXECUTE_READWRITE;
        else
            prot = PAGE_NOACCESS;

        if(bytes > 0)
        {
            int action_code = action == PLATFORM_VIRTUAL_ALLOC_RESERVE ? MEM_RESERVE : MEM_COMMIT;
            out_addr = VirtualAlloc(address, bytes, action_code, prot);
            out = _platform_error_code(out_addr != NULL);
        }
    }

    if(output_adress_or_null)
        *output_adress_or_null = out_addr;

    return out;
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

int64_t platform_page_size()
{
    static int64_t page_size = -1;
    if(page_size == -1)
    {
        SYSTEM_INFO info = {0};
        GetSystemInfo(&info);
        page_size = info.dwPageSize;
    }

    return page_size;
}

int64_t platform_allocation_granularity()
{
    static int64_t page_size = -1;
    if(page_size == -1)
    {
        SYSTEM_INFO info = {0};
        GetSystemInfo(&info);
        page_size = info.dwAllocationGranularity;
    }

    return page_size;
}

int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align)
{
    int64_t size = 0;
    if(old_ptr)
        size = _aligned_msize((void*) old_ptr, (size_t) align, 0);
    return size;
}

//=========================================
// Threading
//=========================================
#include <process.h>

Platform_Error  platform_thread_launch(int64_t stack_size_or_zero, void (*func)(void*), void* context, const char* name_fmt, ...);
int32_t         platform_thread_get_processor_count();
int32_t         platform_thread_get_current_id(); 
int32_t         platform_thread_get_main_id(); //Returns the handle to the thread which called platform_init(). If platform_init() was not called returns -1.
const char*     platform_thread_get_current_name(); 
void            platform_thread_sleep(double seconds); //Sleeps the calling thread for specified number of seconds. The accuracy is platform and scheduler dependent
void            platform_thread_exit(int code); //Terminates a thread with an exit code
void            platform_thread_yield(); //Yields the remainder of this thread's time slice to another thread

typedef struct _Platform_Thread_Context {
    void (*func)(void* context);
    void* context;
    char* name;
    size_t name_size;
} _Platform_Thread_Context;

_Thread_local _Platform_Thread_Context* g_thread_context = NULL;
unsigned _thread_func(void* void_context)
{
    _Platform_Thread_Context* context = (_Platform_Thread_Context*) void_context;
    g_thread_context = context;

    //set name
    {
        int utf16len = MultiByteToWideChar(CP_UTF8, 0, context->name, (int) context->name_size, NULL, 0);
        wchar_t* wide_name = (wchar_t*) calloc(sizeof(wchar_t), utf16len + 1);
        MultiByteToWideChar(CP_UTF8, 0, context->name, (int) context->name_size, wide_name, (int) utf16len);
        SetThreadDescription(GetCurrentThread(), wide_name);
        free(wide_name);
    }

    context->func(context->context);

    free(context->name);
    free(context);
    return 0;
}

Platform_Error platform_thread_launch(isize stack_size_or_zero, void (*func)(void*), void* context, const char* name_fmt, ...)
{
    if(stack_size_or_zero <= 0)
        stack_size_or_zero = 0;
    if(stack_size_or_zero > UINT_MAX)
        stack_size_or_zero = UINT_MAX;

    _Platform_Thread_Context* thread_context = calloc(1, sizeof(_Platform_Thread_Context));
    if(thread_context) {
        thread_context->func = func;
        thread_context->context = context;
        
        if(name_fmt == NULL)
            name_fmt = "";

        va_list args;
        va_list copy;
        va_start(args, name_fmt);
        va_copy(copy, args);
        thread_context->name_size = vsnprintf(NULL, 0, name_fmt, copy);
        thread_context->name = calloc(1, thread_context->name_size + 1);
        vsnprintf(thread_context->name, thread_context->name_size + 1, name_fmt, args);
        va_end(args);

        HANDLE handle = (HANDLE) _beginthreadex(NULL, (unsigned int) stack_size_or_zero, _thread_func, thread_context, 0, NULL);
        if(handle) 
            return PLATFORM_ERROR_OK;

        free(thread_context->name);
    }

    free(thread_context);
    return (Platform_Error) GetLastError();
}

const char* platform_thread_get_current_name()
{
    if(g_thread_context)
        return g_thread_context->name;
    else
    {
        if(platform_thread_get_current_id() == platform_thread_get_main_id())
            return "main";
        else
            return "<unassigned>";
    }
}

int32_t platform_thread_get_current_id()
{
    return (int32_t) GetCurrentThreadId();
}

int32_t platform_thread_get_main_id()
{
    static int32_t _main_thread_handle = -1;
    if(_main_thread_handle == -1)
        _main_thread_handle = (int32_t) GetCurrentThreadId();

    return _main_thread_handle;
}

int32_t platform_thread_get_processor_count()
{
    return GetCurrentProcessorNumber();
}

void platform_thread_yield()
{
    SwitchToThread();
}

void platform_thread_sleep(double seconds)
{
    if(seconds > 0)
        Sleep((DWORD) (seconds * 1000));
}

void platform_thread_exit(int code)
{
    _endthreadex((unsigned int) code);
}

Platform_Error  platform_cond_var_init(Platform_Cond_Var* cond_var)
{
    platform_cond_var_deinit(cond_var);
    CONDITION_VARIABLE* conditional = (CONDITION_VARIABLE*) calloc(1, sizeof *conditional);
    if(conditional != 0)
        InitializeConditionVariable(conditional);

    cond_var->handle = conditional;
    return _platform_error_code(conditional != NULL);
}

void platform_cond_var_deinit(Platform_Cond_Var* cond_var)
{
    if(cond_var->handle)
    {
        DeleteCriticalSection((CRITICAL_SECTION*) cond_var->handle);
        free(cond_var->handle);
        memset(cond_var, 0, sizeof cond_var);
    }
}

void platform_cond_var_wake_single(Platform_Cond_Var* cond_var)
{
    assert(cond_var && cond_var->handle != NULL);
    WakeConditionVariable((CONDITION_VARIABLE*) cond_var->handle);
}
void platform_cond_var_wake_all(Platform_Cond_Var* cond_var)
{
    assert(cond_var && cond_var->handle != NULL);
    WakeAllConditionVariable((CONDITION_VARIABLE*) cond_var->handle);
}
bool platform_cond_var_wait_mutex(Platform_Cond_Var* cond_var, Platform_Mutex* mutex, double seconds_or_negative_if_infinite)
{
    assert(mutex && mutex->handle != NULL);
    assert(cond_var && cond_var->handle != NULL);
    DWORD wait_ms = INFINITE;
    if(seconds_or_negative_if_infinite > 0)
        wait_ms = (DWORD) (seconds_or_negative_if_infinite*1000  + 0.5);
    return !!SleepConditionVariableCS((CONDITION_VARIABLE*) cond_var->handle, (CRITICAL_SECTION*) mutex->handle, wait_ms);
}

static bool _platform_cond_var_wait_rwlock(Platform_Cond_Var* cond_var, Platform_RW_Lock* mutex, double seconds_or_negative_if_infinite, bool is_reader)
{
    assert(mutex && mutex->handle != NULL);
    assert(cond_var && cond_var->handle != NULL);
    DWORD wait_ms = INFINITE;
    if(seconds_or_negative_if_infinite > 0)
        wait_ms = (DWORD) (seconds_or_negative_if_infinite*1000 + 0.5);
    return !!SleepConditionVariableSRW((CONDITION_VARIABLE*) cond_var->handle, (SRWLOCK*) &mutex->handle, wait_ms, is_reader ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0);
}

bool platform_cond_var_wait_rwlock_reader(Platform_Cond_Var* cond_var, Platform_RW_Lock* mutex, double seconds_or_negative_if_infinite)
{
    return _platform_cond_var_wait_rwlock(cond_var, mutex, seconds_or_negative_if_infinite, true);
}

bool platform_cond_var_wait_rwlock_writer(Platform_Cond_Var* cond_var, Platform_RW_Lock* mutex, double seconds_or_negative_if_infinite)
{
    return _platform_cond_var_wait_rwlock(cond_var, mutex, seconds_or_negative_if_infinite, false);
}


Platform_Error platform_mutex_init(Platform_Mutex* mutex)
{
    assert(mutex);
    platform_mutex_deinit(mutex);
    CRITICAL_SECTION* section = (CRITICAL_SECTION*) calloc(1, sizeof *section);
    if(section != 0)
        InitializeCriticalSection(section);

    mutex->handle = section;
    return _platform_error_code(section != NULL);
}

void platform_mutex_deinit(Platform_Mutex* mutex)
{
    if(mutex->handle)
    {
        DeleteCriticalSection((CRITICAL_SECTION*) mutex->handle);
        free(mutex->handle);
        memset(mutex, 0, sizeof mutex);
    }
}

void platform_mutex_lock(Platform_Mutex* mutex)
{
    assert(mutex && mutex->handle != NULL);
    EnterCriticalSection((CRITICAL_SECTION*) mutex->handle);
}

void platform_mutex_unlock(Platform_Mutex* mutex)
{
    assert(mutex && mutex->handle != NULL);
    LeaveCriticalSection((CRITICAL_SECTION*) mutex->handle);
}

bool platform_mutex_try_lock(Platform_Mutex* mutex)
{
    assert(mutex && mutex->handle != NULL);
    return !!TryEnterCriticalSection((CRITICAL_SECTION*) mutex->handle);
}   

Platform_Error platform_rwlock_init(Platform_RW_Lock* mutex)
{
    InitializeSRWLock((SRWLOCK*) &mutex->handle);
    return PLATFORM_ERROR_OK;
}
void platform_rwlock_deinit(Platform_RW_Lock* mutex)
{
    mutex->handle = NULL;
}
void platform_rwlock_reader_lock(Platform_RW_Lock* mutex)
{
    AcquireSRWLockShared((SRWLOCK*) &mutex->handle);
}
void platform_rwlock_reader_unlock(Platform_RW_Lock* mutex)
{
    ReleaseSRWLockShared((SRWLOCK*) &mutex->handle);
}
void platform_rwlock_writer_lock(Platform_RW_Lock* mutex)
{
    AcquireSRWLockExclusive((SRWLOCK*) &mutex->handle);
}
void platform_rwlock_writer_unlock(Platform_RW_Lock* mutex)
{
    ReleaseSRWLockExclusive((SRWLOCK*) &mutex->handle);
}

bool platform_rwlock_reader_try_lock(Platform_RW_Lock* mutex)
{
    return !!TryAcquireSRWLockShared((SRWLOCK*) &mutex->handle);
}
bool platform_rwlock_writer_try_lock(Platform_RW_Lock* mutex)
{
    return !!TryAcquireSRWLockExclusive((SRWLOCK*) &mutex->handle);
}

#pragma comment(lib, "synchronization.lib")
#include <process.h>
bool platform_futex_wait(volatile void* futex, uint32_t value, double seconds_or_negative_if_infinite)
{
    DWORD wait = INFINITE;
    if(seconds_or_negative_if_infinite > 0)
        wait = (DWORD) (seconds_or_negative_if_infinite*1000 + 0.5);
    return !!WaitOnAddress(futex, &value, sizeof value, wait);
}
void platform_futex_wake_single(volatile void* futex)
{
    WakeByAddressSingle((void*) futex);
}
void platform_futex_wake_all(volatile void* futex)
{
    WakeByAddressAll((void*) futex);
}

//=========================================
// Timings
//=========================================
static int64_t g_startup_perf_counter = 0;
static int64_t g_startup_epoch_time = 0;
static int64_t g_perf_counter_freq = 0;
void _platform_deinit_timings()
{
    g_startup_perf_counter = 0;
    g_perf_counter_freq = 0;
    g_startup_epoch_time = 0;
}

int64_t platform_perf_counter()
{
    LARGE_INTEGER ticks;
    ticks.QuadPart = 0;
    (void) QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
}

int64_t platform_perf_counter_startup()
{
    if(g_startup_perf_counter == 0)
        g_startup_perf_counter = platform_perf_counter();
    return g_startup_perf_counter;
}

int64_t platform_perf_counter_frequency()
{
    if(g_perf_counter_freq == 0)
    {
        LARGE_INTEGER ticks;
        ticks.QuadPart = 0;
        (void) QueryPerformanceFrequency(&ticks);
        g_perf_counter_freq = ticks.QuadPart;
    }
    return g_perf_counter_freq;
}

static int64_t _filetime_to_epoch_time(FILETIME t)  
{    
    ULARGE_INTEGER ull;    
    ull.LowPart = t.dwLowDateTime;    
    ull.HighPart = t.dwHighDateTime;
    int64_t tu = ull.QuadPart / 10 - 11644473600000000LL;
    return tu;
}

int64_t platform_epoch_time()
{
    FILETIME filetime;
    GetSystemTimeAsFileTime(&filetime);
    int64_t epoch_time = _filetime_to_epoch_time(filetime);
    return epoch_time;
}

int64_t platform_epoch_time_startup()
{
    if(g_startup_epoch_time == 0)
        g_startup_epoch_time = platform_epoch_time();

    return g_startup_epoch_time;
}



//=========================================
// Filesystem
//=========================================
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

#define _LOCAL_BUFFER_SIZE (MAX_PATH + 32)
#define _NORMALIZE_LINUX 0
#define _NORMALIZE_DIRECTORY 0
#define _NORMALIZE_FILE 0

#define _CONCAT(a, b) a ## b
#define CONCAT(a, b) _CONCAT(a, b)

#define buffer_init_backed(buff, backing_size) do { \
        char CONCAT(__backing, __LINE__)[(backing_size)* sizeof *(buff)->data]; \
        _buffer_init_backed((Buffer_Base*) (void*) (buff), sizeof *(buff)->data, CONCAT(__backing, __LINE__), (backing_size)); \
    } while(0)

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

static void _buffer_deinit(Buffer_Base* buffer)
{
    if(buffer->is_alloced)
        (void) free(buffer->data);
    
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
            new_data = realloc(buffer->data, new_capaity * item_size);
        else
        {
            new_data = malloc(new_capaity * item_size);
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
    assert(item_size == data_size); (void) data_size;
    _buffer_reserve(buffer, item_size, buffer->size + data_count);
    memcpy((char*) buffer->data + buffer->size*item_size, data, data_count*item_size);
    buffer->size += data_count;
    memset((char*) buffer->data + buffer->size*item_size, 0, item_size);
}

//Nasty conversions
static char* _utf16_to_utf8(String_Buffer* append_to_or_null, const wchar_t* string, int64_t string_size) 
{
    String_Buffer local = {0};
    String_Buffer* append_to = append_to_or_null ? append_to_or_null : &local;

    int utf8len = WideCharToMultiByte(CP_UTF8, 0, string, (int) string_size, NULL, 0, NULL, NULL);
    buffer_resize(append_to, utf8len);
    WideCharToMultiByte(CP_UTF8, 0, string, (int) string_size, append_to->data, (int) utf8len, 0, 0);
    append_to->data[utf8len] = '\0';
    return append_to->data;
}

static wchar_t* _utf8_to_utf16(WString_Buffer* append_to_or_null, const char* string, int64_t string_size) 
{
    WString_Buffer local = {0};
    WString_Buffer* append_to = append_to_or_null ? append_to_or_null : &local;

    int utf16len = MultiByteToWideChar(CP_UTF8, 0, string, (int) string_size, NULL, 0);
    buffer_resize(append_to, utf16len);
    MultiByteToWideChar(CP_UTF8, 0, string, (int) string_size, append_to->data, (int) utf16len);
    append_to->data[utf16len] = '\0';
    return append_to->data;
}

static wchar_t* _wstring_path(WString_Buffer* append_to_or_null, Platform_String path)
{
    WString_Buffer local = {0};
    WString_Buffer* append_to = append_to_or_null ? append_to_or_null : &local;
    wchar_t* str = _utf8_to_utf16(append_to, path.data, path.count);
    for(int64_t i = 0; i < append_to->size; i++)
    {
        if(str[i] == '\\')
            str[i] = '/';
    }

    return str;
}

static char* _string_path(String_Buffer* append_to_or_null, const wchar_t* string, int64_t string_size)
{
    String_Buffer local = {0};
    String_Buffer* append_to = append_to_or_null ? append_to_or_null : &local;
    char* str = _utf16_to_utf8(append_to, string, string_size);
    for(int64_t i = 0; i < append_to->size; i++)
    {
        if(str[i] == '\\')
            str[i] = '/';
    }

    return str;
}

int64_t platform_translate_error(Platform_Error error, char* translated, int64_t translated_size)
{
    char buffer[1024];
    const char* source = NULL;
    bool was_allocated = false;

    if(error == PLATFORM_ERROR_OTHER)
        source = "Other platform specific error occurred";
    //If posix error code return that format
    else if(error & (1 << 29))
        source = strerror(error & ~(1 << 29));
    //If win32 error code return that
    else
    {
        //Try to format into a stack buffer
        source = buffer;
        int64_t size = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            (DWORD) error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR) buffer,
            sizeof(buffer), NULL);

        //if too big format into an allocated buffer (unlikely)
        if(size == sizeof(buffer))
        {
            was_allocated = true;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                (DWORD) error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR) (void*) &source,
                0, NULL );
        }
    }
    int64_t needed_size = strlen(source);
    if(translated) {
        //Strips annoying trailing whitespace and null termination
        for(; needed_size > 0; needed_size --)
        {
            char c = source[needed_size - 1];
            if(!isspace(c) && c != '\0')
                break;
        }

        int64_t min_size = needed_size < translated_size ? needed_size : translated_size;
        memcpy(translated, source, min_size);
    
        //Null terminate right after
        if(needed_size < translated_size) 
            translated[needed_size] = '\0';
        //Null terminate the whole buffer 
        else if(translated_size > 0) 
            translated[translated_size - 1] = '\0';
    }

    if(was_allocated)
        LocalFree((void*) source);
    return needed_size + 1;
}

//we really want zero to be the default invalid value. Because of this we do this weird xor-by-INVALID_HANDLE_VALUE trick. 
void* _platform_flip_handle(void* platform_handle)
{
    return (void*) ((uintptr_t) platform_handle ^ (uintptr_t) INVALID_HANDLE_VALUE);
}

Platform_Error platform_file_open(Platform_File* file, Platform_String file_path, int open_flags)
{
    platform_file_close(file);

    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);

    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD access = 0;
    if(open_flags & PLATFORM_FILE_OPEN_READ)
        access |= GENERIC_READ;
    if(open_flags & PLATFORM_FILE_OPEN_WRITE)
        access |= GENERIC_WRITE;

    LPSECURITY_ATTRIBUTES security = NULL;

    DWORD creation = OPEN_EXISTING; 
    if(open_flags & PLATFORM_FILE_OPEN_REMOVE_CONTENT)
    {
        if(open_flags & PLATFORM_FILE_OPEN_CREATE_MUST_NOT_EXIST)
            creation = CREATE_NEW;
        else if(open_flags & PLATFORM_FILE_OPEN_CREATE)
            creation = CREATE_ALWAYS;
    }
    else
    {
        if(open_flags & PLATFORM_FILE_OPEN_CREATE_MUST_NOT_EXIST)
            creation = CREATE_NEW;
        else if(open_flags & PLATFORM_FILE_OPEN_CREATE)
            creation = OPEN_ALWAYS;
    }
    
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if(open_flags & PLATFORM_FILE_OPEN_TEMPORARY)
        flags |= FILE_FLAG_DELETE_ON_CLOSE;
    
    if(open_flags & PLATFORM_FILE_OPEN_HINT_UNBUFFERED)
        flags |= 0; //imposes too many restrictions on windows to be usable like this
    if(open_flags & PLATFORM_FILE_OPEN_HINT_FRONT_TO_BACK_ACCESS)
        flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    if(open_flags & PLATFORM_FILE_OPEN_HINT_BACK_TO_FRONT_ACCESS)
        flags |= 0; //does not exist on windows
    if(open_flags & PLATFORM_FILE_OPEN_HINT_RANDOM_ACCESS)
        flags |= FILE_FLAG_RANDOM_ACCESS;

    HANDLE template_handle = NULL;
    HANDLE handle = CreateFileW(path, access, share, security, creation, flags, template_handle);
    file->handle = _platform_flip_handle(handle);

    buffer_deinit(&buffer);
    return _platform_error_code(file->handle != NULL);
}

bool platform_file_is_open(const Platform_File* file)
{
    return file->handle != NULL;
}

Platform_Error platform_file_close(Platform_File* file)
{
    bool state = true;
    if(file->handle)
        state = !!CloseHandle(_platform_flip_handle(file->handle));

    memset(file, 0, sizeof *file);
    return _platform_error_code(state);
}

Platform_Error platform_file_read(Platform_File* file, void* buffer, isize size, isize offset, isize* read_bytes_because_eof)
{
    bool state = true;
    isize total_read = 0;

    // BOOL ReadFile(
    //     [in]                HANDLE       hFile,
    //     [out]               LPVOID       lpBuffer,
    //     [in]                DWORD        nNumberOfBytesToRead,
    //     [out, optional]     LPDWORD      lpNumberOfBytesRead,
    //     [in, out, optional] LPOVERLAPPED lpOverlapped
    // );
    for(; file->handle && total_read < size;)  {
        isize to_read = size - total_read;
        if(to_read > (DWORD) -1)
            to_read = (DWORD) -1;
        
        OVERLAPPED overlapped = {0};
        overlapped.Pointer = (void*) (offset + total_read);

        DWORD bytes_read = 0;
        state = !!ReadFile(_platform_flip_handle(file->handle), (unsigned char*) buffer + total_read, (DWORD) to_read, &bytes_read, &overlapped);

        //Eof found!
        if(state && bytes_read < to_read)
            break;

        //Error
        if(state == false)
            break;

        total_read += bytes_read;
    }

    if(read_bytes_because_eof)
        *read_bytes_because_eof = total_read;

    return _platform_error_code(state);
}

Platform_Error _platform_file_write(Platform_File* file, const void* buffer, isize size, isize offset, bool at_end)
{
    bool state = true;
    // BOOL WriteFile(
    //     [in]                HANDLE       hFile,
    //     [in]                LPCVOID      lpBuffer,
    //     [in]                DWORD        nNumberOfBytesToWrite,
    //     [out, optional]     LPDWORD      lpNumberOfBytesWritten,
    //     [in, out, optional] LPOVERLAPPED lpOverlapped
    // );

    for(isize total_written = 0; file->handle && total_written < size;) {
        isize to_write = size - total_written;
        if(to_write > (DWORD) -1)
            to_write = (DWORD) -1;

        OVERLAPPED overlapped = {0};
        if(at_end)
            overlapped.Pointer = (void*) -1;
        else
            overlapped.Pointer = (void*) (offset + total_written);
        
        DWORD bytes_written = 0;
        state = !!WriteFile(_platform_flip_handle(file->handle), (unsigned char*) buffer + total_written, (DWORD) to_write, &bytes_written, NULL);
        if(state == false)
            break;

        total_written += bytes_written;
    }

    return _platform_error_code(state);
}

Platform_Error platform_file_write(Platform_File* file, const void* buffer, isize size, isize offset)
{
    return _platform_file_write(file, buffer, size, offset, false);
}

Platform_Error platform_file_flush(Platform_File* file)
{
    bool state = file->handle && FlushFileBuffers(_platform_flip_handle(file->handle));
    return _platform_error_code(state);
}

Platform_Error platform_file_size(const Platform_File* file, isize* size)
{
    bool state = file->handle && GetFileSizeEx(_platform_flip_handle(file->handle), (LARGE_INTEGER*) (void*) size);
    return _platform_error_code(state);
}

Platform_Error platform_file_read_entire(Platform_String file_path, void* buffer, isize buffer_size)
{
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_OPEN_READ | PLATFORM_FILE_OPEN_HINT_FRONT_TO_BACK_ACCESS);
    isize read = 0;
    if(error == 0)
        error = platform_file_read(&file, buffer, buffer_size, 0, &read);
    if(error == 0 && read != buffer_size)
        error = PLATFORM_ERROR_OTHER;
    platform_file_close(&file);
    return error;
}

Platform_Error platform_file_write_entire(Platform_String file_path, const void* buffer, isize buffer_size, bool fail_if_not_found)
{
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, 
            PLATFORM_FILE_OPEN_WRITE | PLATFORM_FILE_OPEN_REMOVE_CONTENT | (fail_if_not_found ? 0 : PLATFORM_FILE_OPEN_CREATE) | PLATFORM_FILE_OPEN_HINT_FRONT_TO_BACK_ACCESS);
    if(error == 0)
        error = platform_file_write(&file, buffer, buffer_size, 0);
    platform_file_close(&file);
    return error;
}

Platform_Error platform_file_append_entire(Platform_String file_path, const void* buffer, isize buffer_size, bool fail_if_not_found)
{
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, 
            PLATFORM_FILE_OPEN_WRITE | (fail_if_not_found ? 0 : PLATFORM_FILE_OPEN_CREATE) | PLATFORM_FILE_OPEN_HINT_FRONT_TO_BACK_ACCESS);
    if(error == 0)
        error = _platform_file_write(&file, buffer, buffer_size, 0, true);
    platform_file_close(&file);
    return error;
}

Platform_Error platform_file_create(Platform_String file_path, bool fail_if_exists)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);

    HANDLE handle = CreateFileW(path, 0, 0, NULL, OPEN_ALWAYS, 0, NULL);
    bool state = handle != INVALID_HANDLE_VALUE;

    if(state == false && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if(fail_if_exists == false)
            state = true;
    }

    Platform_Error error = _platform_error_code(state);
    CloseHandle(handle);
    buffer_deinit(&buffer);
    return error;
}
Platform_Error platform_file_remove(Platform_String file_path, bool fail_if_does_not_exist)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);

    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    bool state = !!DeleteFileW(path);

    if(state == false && GetLastError() == ERROR_FILE_NOT_FOUND)
    {
        if(fail_if_does_not_exist == false)
            state = true;
    }

    buffer_deinit(&buffer);
    return _platform_error_code(state);
}

Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path, bool override_if_used)
{       
    WString_Buffer new_backed = {0}; buffer_init_backed(&new_backed, _LOCAL_BUFFER_SIZE);
    WString_Buffer old_backed = {0}; buffer_init_backed(&old_backed, _LOCAL_BUFFER_SIZE);
    const wchar_t* new_path_norm = _wstring_path(&new_backed, new_path);
    const wchar_t* old_path_norm = _wstring_path(&old_backed, old_path);

    DWORD flags = MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH;
    if(override_if_used)
        flags |= MOVEFILE_REPLACE_EXISTING;

    bool state = !!MoveFileExW(old_path_norm, new_path_norm, flags);
    
    buffer_deinit(&new_backed);
    buffer_deinit(&old_backed);
    return _platform_error_code(state);
}

Platform_Error platform_file_copy(Platform_String new_path, Platform_String old_path, bool override_if_used)
{
    WString_Buffer new_backed = {0}; buffer_init_backed(&new_backed, _LOCAL_BUFFER_SIZE);
    WString_Buffer old_backed = {0}; buffer_init_backed(&old_backed, _LOCAL_BUFFER_SIZE);
    const wchar_t* new_path_norm = _wstring_path(&new_backed, new_path);
    const wchar_t* old_path_norm = _wstring_path(&old_backed, old_path);
    //BOOL CopyFileExA(
    //    [in]           LPCSTR             lpExistingFileName,
    //    [in]           LPCSTR             lpNewFileName,
    //    [in, optional] LPPROGRESS_ROUTINE lpProgressRoutine,
    //    [in, optional] LPVOID             lpData,
    //    [in, optional] LPBOOL             pbCancel,
    //    [in]           DWORD              dwCopyFlags
    //);
    DWORD flags = COPY_FILE_NO_BUFFERING;
    if(override_if_used == false)
        flags |= COPY_FILE_FAIL_IF_EXISTS;
    bool state = !!CopyFileExW(old_path_norm, new_path_norm, NULL, NULL, FALSE, flags);
    
    buffer_deinit(&new_backed);
    buffer_deinit(&old_backed);
    return _platform_error_code(state);
}

Platform_Error platform_file_resize(Platform_String file_path, isize size)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);

    HANDLE handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    bool state = handle != INVALID_HANDLE_VALUE; 
    if(state)
    { 
        //@NOTE: In win7 new_offset_win argument is required else the function crashes
        LARGE_INTEGER new_offset_win = {0}; 
        LARGE_INTEGER offset_win = {0};
        offset_win.QuadPart = size;
        state = !!SetFilePointerEx(handle, offset_win, &new_offset_win, FILE_BEGIN);

        if(state)
            state = !!SetEndOfFile(handle);
    }

    Platform_Error error = _platform_error_code(state);
    CloseHandle(handle);
    buffer_deinit(&buffer);

    return error;
}

Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null)
{    
    Platform_File_Info info = {0};
    WIN32_FILE_ATTRIBUTE_DATA native_info = {0};
    
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);
    bool state = !!GetFileAttributesExW(path, GetFileExInfoStandard, &native_info);
    
    //is *maybe* link. On win32 its quite difficult (and quite slow) to actually check this
    // so we are gonna suffice with probably link.
    if(native_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) 
        info.link_type = PLATFORM_LINK_TYPE_PROBABLY_LINK;
    if(!state)
        return _platform_error_code(state);
            
    info.created_epoch_time = _filetime_to_epoch_time(native_info.ftCreationTime);
    info.last_access_epoch_time = _filetime_to_epoch_time(native_info.ftLastAccessTime);
    info.last_write_epoch_time = _filetime_to_epoch_time(native_info.ftLastWriteTime);
    info.size = ((int64_t) native_info.nFileSizeHigh << 32) | ((int64_t) native_info.nFileSizeLow);
        
    if(native_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        info.last_access_epoch_time = info.created_epoch_time;
        info.last_write_epoch_time = info.created_epoch_time;
        info.type = PLATFORM_FILE_TYPE_DIRECTORY;
    }
    else
        info.type = PLATFORM_FILE_TYPE_FILE;

    if(info_or_null)
        *info_or_null = info;

    buffer_deinit(&buffer);
    return _platform_error_code(state);
}

Platform_Error platform_directory_create(Platform_String dir_path, bool fail_if_already_existing)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, dir_path);
    bool state = !!CreateDirectoryW(path, NULL);
    if(state == false && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if(fail_if_already_existing == false)
            state = true;
    }

    buffer_deinit(&buffer);
    return _platform_error_code(state);
}
    
Platform_Error platform_directory_remove(Platform_String dir_path, bool fail_if_not_found)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, dir_path);
    bool state = !!RemoveDirectoryW(path);
    if(state == false && GetLastError() == ERROR_PATH_NOT_FOUND)
    {
        if(fail_if_not_found == false)
            state = true;
    }

    buffer_deinit(&buffer);
    return _platform_error_code(state);
}

typedef struct _Platform_Dir_Iter {
    WIN32_FIND_DATAW current_entry;
    HANDLE first_found;
    int64_t called_times;


    //the WIN32_FIND_DATAW has MAX_PATH wchars size so this should be enough
    char path[MAX_PATH*2 + 3];
} _Platform_Dir_Iter;

Platform_Error platform_directory_iter_init(Platform_Directory_Iter* iter, Platform_String directory_path)
{
    platform_directory_iter_deinit(iter);
    iter->internal = calloc(1, sizeof(_Platform_Dir_Iter));
    iter->index = -1;

    WString_Buffer wide = {0};
    buffer_init_backed(&wide, _LOCAL_BUFFER_SIZE);
    _wstring_path(&wide, directory_path);
    buffer_append(&wide, L"\\*.*", 4);

    _Platform_Dir_Iter* it = (_Platform_Dir_Iter*) iter->internal;
    it->first_found = FindFirstFileW(wide.data, &it->current_entry);

    Platform_Error error = 0;
    if(it->first_found == INVALID_HANDLE_VALUE)
        error = GetLastError();
    
    buffer_deinit(&wide);
    return error;
}

bool platform_directory_iter_next(Platform_Directory_Iter* iter)
{
    bool ok = false;
    if(iter->internal) {
        _Platform_Dir_Iter* it = (_Platform_Dir_Iter*) iter->internal;
        for(;;) {
            if(it->called_times != 0)
                if(FindNextFileW(it->first_found, &it->current_entry) == false)
                    break;

            it->called_times += 1;
            wchar_t* filename = it->current_entry.cFileName;
            if(wcscmp(filename, L".") != 0 && wcscmp(filename, L"..") != 0) {
                int utf8len = WideCharToMultiByte(CP_UTF8, 0, filename, (int) wcslen(filename), it->path, (int) sizeof(it->path) - 1, 0, 0);
                it->path[utf8len] = '\0';
                iter->index += 1;
                iter->path.data = it->path;
                iter->path.count = utf8len;
                ok = true;
                break;
            }
            
        }
    }
        
    return ok;
}
void platform_directory_iter_deinit(Platform_Directory_Iter* iter)
{
    ASSERT(iter);
    if(iter->internal) {
        _Platform_Dir_Iter* it = (_Platform_Dir_Iter*) iter->internal;
        if(it->first_found != INVALID_HANDLE_VALUE && it->first_found != NULL)
            FindClose(it->first_found);
        free(it);
    } 
}

//CWD madness
Platform_Error platform_directory_set_current_working(Platform_String new_working_dir)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, new_working_dir);
    bool state = _wchdir(path) == 0;
    buffer_deinit(&buffer);
    return _platform_error_code_posix(state);
}

Platform_Error platform_directory_get_current_working(void* buffer, isize buffer_size, bool* needs_bigger_buffer_or_null)
{
    assert(buffer != NULL || (buffer == NULL && buffer_size == 0));

    bool ok = true;
    if(buffer_size > 0)
        ok = _getcwd(buffer, (int) buffer_size) != NULL;
    
    if(*needs_bigger_buffer_or_null)
        *needs_bigger_buffer_or_null = errno == ERANGE;
    return _platform_error_code_posix(ok);
}

const char* platform_directory_get_startup_working()
{
    static uint32_t init = 0;
    static const char* cwd = NULL;
    if(platform_once_begin(&init))
    {
        cwd = _getcwd(NULL, 0);
        platform_once_end(&init);
    }
    return cwd;
}

const char* platform_get_executable_path()
{
    static uint32_t init = 0;
    static const char* dir = {0};
    if(platform_once_begin(&init))
    {
        WString_Buffer wide = {0};
        WString_Buffer full_path = {0};
        buffer_init_backed(&wide, _LOCAL_BUFFER_SIZE);
        buffer_init_backed(&full_path, _LOCAL_BUFFER_SIZE);

        buffer_resize(&wide, MAX_PATH);
        for(int64_t i = 0; i < 16; i++)
        {
            buffer_resize(&wide, wide.size * 2);
            int64_t count = GetModuleFileNameW(NULL, wide.data, (DWORD) wide.size);
            if(count < wide.size)
                break;
        }

        int64_t needed_size = GetFullPathNameW(wide.data, (DWORD) full_path.size, full_path.data, NULL);
        if(needed_size > full_path.size)
        {
            buffer_resize(&full_path, needed_size);
            needed_size = GetFullPathNameW(wide.data, (DWORD) full_path.size, full_path.data, NULL);
        }
        
        dir = _string_path(NULL, full_path.data, full_path.size);
        buffer_deinit(&full_path);
        buffer_deinit(&wide);

        assert(dir != NULL);
        platform_once_end(&init);
    }
    return dir;
}

//=========================================
// File watch
//=========================================
typedef struct _Platform_File_Watch_Context {
    OVERLAPPED overlapped;
    HANDLE directory;
    DWORD win_flags;
    BOOL win_watch_subdir;

    int32_t flags;
    String_Buffer watched_path;
    String_Buffer change_path;
    String_Buffer change_new_path;
    
    uint8_t* buffer;
    isize buffer_size;
    isize buffer_capacity;
    isize buffer_offset;
} _Platform_File_Watch_Context;

void platform_file_watch_deinit(Platform_File_Watch* watched)
{
    if(watched && watched->handle) {
        _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) watched->handle;
        if(context->directory != INVALID_HANDLE_VALUE && context->directory != NULL)
            CloseHandle(context->directory);
            
        if(context->overlapped.hEvent != INVALID_HANDLE_VALUE && context->overlapped.hEvent != NULL)
            CloseHandle(context->overlapped.hEvent);

        buffer_deinit(&context->watched_path);
        buffer_deinit(&context->change_path);
        buffer_deinit(&context->change_new_path);

        free(context->buffer);
        free(context);
    }
    if(watched)
        memset(watched, 0, sizeof *watched);
}

Platform_Error platform_file_watch_init(Platform_File_Watch* file_watch, int32_t flags, Platform_String path, isize buffer_size)
{
    bool ok = false;
    platform_file_watch_deinit(file_watch);
    _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) calloc(1, sizeof(_Platform_File_Watch_Context));
    if(context) {
        file_watch->handle = context;

        context->flags = flags;
        context->win_flags = 0;
        if(flags & PLATFORM_FILE_WATCH_CREATED)
            context->win_flags |= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION;
        if(flags & PLATFORM_FILE_WATCH_DELETED)
            context->win_flags |= FILE_NOTIFY_CHANGE_FILE_NAME;
        if(flags & PLATFORM_FILE_WATCH_RENAMED)
            context->win_flags |= FILE_NOTIFY_CHANGE_FILE_NAME;
        if(flags & PLATFORM_FILE_WATCH_MODIFIED)
            context->win_flags |= FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES;
        if(flags & PLATFORM_FILE_WATCH_DIRECTORY)
            context->win_flags |= FILE_NOTIFY_CHANGE_DIR_NAME;

        context->win_watch_subdir = !!(flags & PLATFORM_FILE_WATCH_SUBDIRECTORIES);
        context->buffer_capacity = buffer_size <= 0 ? 64*1024 : buffer_size;
        context->buffer = (uint8_t*) malloc(context->buffer_capacity);
        buffer_reserve(&context->change_path, _LOCAL_BUFFER_SIZE);
        buffer_reserve(&context->change_new_path, _LOCAL_BUFFER_SIZE);
        buffer_append(&context->watched_path, path.data, path.count);

        WString_Buffer wpath_buffer = {0}; buffer_init_backed(&wpath_buffer, _LOCAL_BUFFER_SIZE);
        const wchar_t* wpath = _wstring_path(&wpath_buffer, path);
            context->directory = CreateFileW(wpath,
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                NULL);
        buffer_deinit(&wpath_buffer);

        if(context->directory != INVALID_HANDLE_VALUE) {
            //context->overlapped.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL); 
            //if(context->overlapped.hEvent != NULL)
                ok = ReadDirectoryChangesW(
                    context->directory, context->buffer, (DWORD) context->buffer_capacity, 
                    context->win_watch_subdir, context->win_flags, NULL, &context->overlapped, NULL);
        }

    }
    
    return _platform_error_code(ok);
}

bool platform_file_watch_poll(Platform_File_Watch* file_watch, Platform_File_Watch_Event* user_event, Platform_Error* error_or_null)
{   
    bool ret = false;
    memset(user_event, 0, sizeof* user_event);

    Platform_Error error = 0;
    if(file_watch && file_watch->handle)
    {
        _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) file_watch->handle;
        buffer_resize(&context->change_new_path, 0);
        buffer_resize(&context->change_path, 0);
        isize new_name_count = 0; 
        isize old_name_count = 0; 

        //Iterate changes until we find a change that matches the provided flags
        // + handle the rename events which are split between two calls
        for(;;) {
            //if we are at the end of the buffer try to get more changes from the os
            if(context->buffer_offset >= context->buffer_size) {
                context->buffer_offset = 0;
                context->buffer_size = 0;

                //Check the completion of previous ReadDirectoryChangesW
                DWORD bytes_transferred = 0;

                //the following two branches are equivalent except the second is a lot faster
                // see here https://pastebin.com/iEcfQK3C
                #if 1
                BOOL ok = GetOverlappedResult(context->directory, &context->overlapped, &bytes_transferred, FALSE);
                if(ok == false) {
                    //if last error is ERROR_IO_PENDING then is not really an error just nothing happened yet
                    DWORD maybe_error = GetLastError();
                    if(maybe_error != ERROR_IO_PENDING)
                        error = maybe_error;

                    break;
                }
                #else
                if(context->overlapped.Internal == 0x103 /* STATUS_IO_PENDING */) 
                    break;
                
                bytes_transferred = (DWORD) context->buffer_capacity;
                #endif
                
                context->buffer_size = bytes_transferred;
            }

            //If we succeded yet size is zero then we overflown
            if(context->buffer_size == 0) {
                user_event->action = PLATFORM_FILE_WATCH_OVERFLOW;
                user_event->watched_path.data = context->watched_path.data;
                user_event->watched_path.count = context->watched_path.size;
                ret = true;
                break;
            } 
                
            ASSERT(context->buffer_offset < context->buffer_size);
            FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION*) (context->buffer + context->buffer_offset);
            
            //Fill out info and convert paths
            int32_t action = 0;
            switch (event->Action) {
                case FILE_ACTION_ADDED: action = PLATFORM_FILE_WATCH_CREATED; break;
                case FILE_ACTION_REMOVED: action = PLATFORM_FILE_WATCH_DELETED; break;
                case FILE_ACTION_MODIFIED: action = PLATFORM_FILE_WATCH_MODIFIED; break;
                case FILE_ACTION_RENAMED_OLD_NAME: action = PLATFORM_FILE_WATCH_RENAMED; old_name_count += 1; break;
                case FILE_ACTION_RENAMED_NEW_NAME: action = PLATFORM_FILE_WATCH_RENAMED; new_name_count += 1; break;
                default: action = 0; break;
            }
            
            isize path_len = event->FileNameLength / sizeof(wchar_t);
            if(path_len > context->buffer_size - context->buffer_offset)
                path_len = context->buffer_size - context->buffer_offset;

            if(event->Action == FILE_ACTION_RENAMED_NEW_NAME)
                _string_path(&context->change_new_path, event->FileName, path_len);
            else
                _string_path(&context->change_path, event->FileName, path_len);

            //last entry has NextEntryOffset == 0
            if (event->NextEntryOffset) 
                context->buffer_offset += event->NextEntryOffset;
            else 
                context->buffer_offset = context->buffer_size;

            if(context->buffer_offset >= context->buffer_size) {
                context->buffer_offset = 0;
                context->buffer_size = 0;
                
                //queue more changes (maybe we dont need this?? test it without this?)
                ReadDirectoryChangesW(
                    context->directory, context->buffer, (DWORD) context->buffer_capacity, 
                    context->win_watch_subdir, context->win_flags,
                    NULL, &context->overlapped, NULL);
            }

            //if we have everything the user asked for and we read both of the connected 
            // rename events
            if((action & context->flags) && old_name_count == new_name_count) {
                user_event->action = (Platform_File_Watch_Flag) action;
                user_event->path.data = context->change_path.data;
                user_event->path.count = context->change_path.size;
                if(action == PLATFORM_FILE_WATCH_RENAMED) {
                    user_event->new_path.data = context->change_new_path.data;
                    user_event->new_path.count = context->change_new_path.size;
                }
                user_event->watched_path.data = context->watched_path.data;
                user_event->watched_path.count = context->watched_path.size;
                ret = true;
                break;
            }
        }
    }

    if(error_or_null)
        *error_or_null = error;
    return ret;
}

//=========================================
// DLL management
//=========================================

Platform_Error platform_dll_load(Platform_DLL* dll, Platform_String path)
{
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* wpath = _utf8_to_utf16(&buffer, path.data, path.count);
    HMODULE hmodule = LoadLibraryW(wpath);
    buffer_deinit(&buffer);
    
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
    buffer_append(&temp, name.data, name.count);
    HMODULE hmodule = (HMODULE)dll->handle;
    void* result = (void*) GetProcAddress(hmodule, temp.data);

    buffer_deinit(&temp);
    return result;
}


//=========================================
// CALLSTACK
//=========================================
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
        WCHAR module_filename[MAX_NAME_LEN] = {0};
        WCHAR module_name[MAX_NAME_LEN] = {0};
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

void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, void const * const * stack, int64_t stack_size)
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

int platform_is_debugger_attached()
{
    return IsDebuggerPresent() ? 1 : 0;
}


//=========================================
// SANDBOX
//=========================================
#define SANDBOX_MAX_STACK 256
#define SANDBOX_JUMP_VALUE 123

#include <setjmp.h>
#include <signal.h>

#pragma warning(disable:4324) // 'Platform_Sandbox_State': structure was padded due to alignment specifier
typedef struct Platform_Sandbox_State {
    void* stack[SANDBOX_MAX_STACK];
    int64_t stack_size;
    int64_t epoch_time;
    int32_t exception;
    int32_t signal_handler_depth;
    const char* exception_text;

    jmp_buf jump_buffer;
    CONTEXT context;
} Platform_Sandbox_State;
#pragma warning(default:4324)

void platform_sandbox_error_deinit(Platform_Sandbox_Error* error)
{
    free(error->call_stack);
    memset(error, 0, sizeof *error);
}

enum {
    PLATFORM_EXCEPTION_ABORT = 0x10001,
    PLATFORM_EXCEPTION_TERMINATE = 0x10002,
    PLATFORM_EXCEPTION_OTHER = 0x10003,
};

__declspec(thread) Platform_Sandbox_State t_sandbox_state = {0};
void _sandbox_abort_filter(int signal)
{
    int64_t epoch_time = platform_epoch_time();
    if(t_sandbox_state.signal_handler_depth <= 0)
        return;
    
    Platform_Sandbox_State* curr_state = &t_sandbox_state;
    if(signal == SIGABRT) {
        curr_state->exception = PLATFORM_EXCEPTION_ABORT;
        curr_state->exception_text = "abort";
    }
    else if(signal == SIGTERM) {
        curr_state->exception = PLATFORM_EXCEPTION_TERMINATE;
        curr_state->exception_text = "terminate";
    }
    else
    {
        assert(false && "badly registred signal handler");
        curr_state->exception = PLATFORM_EXCEPTION_OTHER;
        curr_state->exception_text = "unknow exception";
    }
    curr_state->stack_size = platform_capture_call_stack(curr_state->stack, SANDBOX_MAX_STACK, 1);
    curr_state->epoch_time = epoch_time;

    longjmp(curr_state->jump_buffer, SANDBOX_JUMP_VALUE);
}

LONG WINAPI _sandbox_exception_filter(EXCEPTION_POINTERS * ExceptionInfo)
{
    int64_t epoch_time = platform_epoch_time();

    if(t_sandbox_state.signal_handler_depth <= 0)
        return EXCEPTION_CONTINUE_SEARCH;

    const char* exception = "unknow exception";
    switch(ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
        //Non errors:
        case CONTROL_C_EXIT: return EXCEPTION_CONTINUE_SEARCH;
        case STILL_ACTIVE: return EXCEPTION_CONTINUE_SEARCH;

        //Errors: see https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record
        case EXCEPTION_ACCESS_VIOLATION:        exception = "access violation"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   exception = "array bounds check"; break;
        case EXCEPTION_BREAKPOINT:              exception = "breakpoint"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:   exception = "datatype misaligned"; break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:    exception = "floating point denormal operand"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:      exception = "floating point divide by zero"; break;
        case EXCEPTION_FLT_INEXACT_RESULT:      exception = "floating point inexact result"; break;
        case EXCEPTION_FLT_INVALID_OPERATION:   exception = "floating point invalid operation"; break;
        case EXCEPTION_FLT_OVERFLOW:            exception = "floating point overflow"; break;
        case EXCEPTION_FLT_STACK_CHECK:         exception = "floating point stack check"; break;
        case EXCEPTION_FLT_UNDERFLOW:           exception = "floating point underflow"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:     exception = "illegal instruction"; break;
        case EXCEPTION_IN_PAGE_ERROR:           exception = "in page error"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:      exception = "divide by zero"; break;
        case EXCEPTION_INT_OVERFLOW:            exception = "int overflow"; break;
        case EXCEPTION_INVALID_DISPOSITION:     exception = "invalid disposition in exception dispatcher"; break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:exception = "noncontinuable exception"; break;
        case EXCEPTION_PRIV_INSTRUCTION:        exception = "priv instruction"; break;
        case EXCEPTION_SINGLE_STEP:             exception = "debugger single step"; break;
        case EXCEPTION_STACK_OVERFLOW:          exception = "stack overflow"; break;
        case PLATFORM_EXCEPTION_ABORT:          exception = "abort"; break;
        case PLATFORM_EXCEPTION_TERMINATE:      exception = "terminate"; break;
        default:                                exception = "unknow exception"; break;
    }
    
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    Platform_Sandbox_State* curr_state = &t_sandbox_state;
    curr_state->epoch_time = epoch_time;
    curr_state->exception = ExceptionInfo->ExceptionRecord->ExceptionCode;
    curr_state->exception_text = exception;
    curr_state->context = *ExceptionInfo->ContextRecord;
    curr_state->stack_size = _platform_stack_trace_walk(curr_state->context, process, thread, 0, (void**) &curr_state->stack, SANDBOX_MAX_STACK, 0);

    return EXCEPTION_EXECUTE_HANDLER;
}

bool platform_exception_sandbox(void (*sandboxed_func)(void* sandbox_context), void* sandbox_context, Platform_Sandbox_Error* error_or_null)
{
    //LPTOP_LEVEL_EXCEPTION_FILTER prev_exception_filter = SetUnhandledExceptionFilter(_sandbox_exception_filter);
    void* vector_exception_handler = AddVectoredExceptionHandler(1, _sandbox_exception_filter);
    int prev_error_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    
    _crt_signal_t prev_abrt = signal(SIGABRT, _sandbox_abort_filter);
    _crt_signal_t prev_term = signal(SIGTERM, _sandbox_abort_filter);
        
    Platform_Sandbox_State prev_state = t_sandbox_state;
    memset(&t_sandbox_state, 0, sizeof t_sandbox_state);

    t_sandbox_state.signal_handler_depth += 1;

    bool run_okay = true;
    switch(setjmp(t_sandbox_state.jump_buffer))
    {
        default: {
            __try { 
                sandboxed_func(sandbox_context);
            } 
            __except(_sandbox_exception_filter(GetExceptionInformation())) { 
                run_okay = false;
            }
            break;
        }

        case SANDBOX_JUMP_VALUE: {
            run_okay = false;
        } break;
    }

    if(run_okay == false)
    {
        if(error_or_null) {
            //just in case we repeatedly exception
            Platform_Sandbox_State error_state = t_sandbox_state;

            Platform_Sandbox_Error error = {0};
            error.exception = error_state.exception_text;
            error.call_stack = calloc(error_state.stack_size, sizeof(void*));
            error.call_stack_size = (int32_t) error_state.stack_size;
            memcpy(error.call_stack, error_state.stack, error_state.stack_size*sizeof(void*));
            error.epoch_time = error_state.epoch_time;

            platform_sandbox_error_deinit(error_or_null);
            *error_or_null = error;
        }
    }

    t_sandbox_state.signal_handler_depth -= 1;
    if(t_sandbox_state.signal_handler_depth < 0)
        t_sandbox_state.signal_handler_depth = 0;

    t_sandbox_state = prev_state;
    signal(SIGABRT, prev_abrt);
    signal(SIGTERM, prev_term);

    SetErrorMode(prev_error_mode);
    if(vector_exception_handler != NULL)
        RemoveVectoredExceptionHandler(vector_exception_handler);
    //SetUnhandledExceptionFilter(prev_exception_filter);
    return run_okay;
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

void platform_init()
{
    platform_deinit();
    platform_thread_get_main_id();

    platform_perf_counter();
    platform_epoch_time_startup();
    platform_perf_counter_startup();

    _platform_set_console_utf8();
    _platform_set_console_output_escape_sequences();
    _platform_stack_trace_init("");
}
void platform_deinit()
{
    _platform_deinit_timings();
    _platform_stack_trace_deinit();
}