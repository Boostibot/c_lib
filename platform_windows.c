
#define _CRT_SECURE_NO_WARNINGS

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

typedef struct Platform_Thread_Cleanup {
    void (*func)(void* context);
    void* context;
} Platform_Thread_Cleanup;

#define _INLINE_CLEANUPS 8
typedef struct Platform_Thread_State {
    int (*func)(void*);
    Platform_Thread_Cleanup inline_cleanups[_INLINE_CLEANUPS];
    Platform_Thread_Cleanup* extended_cleanups;
    int cleanup_count;
    int cleanup_capacity;
} Platform_Thread_State;

static _declspec(thread) Platform_Thread_State* _current_thread_state = NULL;
Platform_Thread_State* _platform_thread_state()
{
    if(_current_thread_state == NULL)
        _current_thread_state = calloc(1, sizeof(Platform_Thread_State));
    
    return _current_thread_state;
}

void _platform_thread_cleanup()
{
    Platform_Thread_State* state = _platform_thread_state();
    for(int i = 0; i < state->cleanup_count; i++)
    {
        Platform_Thread_Cleanup* cleanup = &state->inline_cleanups[i];
        if(i >= _INLINE_CLEANUPS)
            cleanup = &state->extended_cleanups[i - _INLINE_CLEANUPS];

        cleanup->func(cleanup->context);
    }

    free(state->extended_cleanups);
    free(state);

    _current_thread_state = NULL;
}

unsigned _thread_func(void* ptr)
{
    Platform_Thread_State* state = (Platform_Thread_State*) ptr;
    _current_thread_state = state;
    void* user_context = state + 1;
    int result = state->func(user_context);

    _platform_thread_cleanup();
    return (unsigned) result;
}



int64_t         platform_thread_get_proccessor_count();
Platform_Thread platform_thread_get_current(); //Returns handle to the calling thread
int32_t         platform_thread_get_current_id(); 
Platform_Thread platform_thread_get_main(); //Returns the handle to the thread which called platform_init(). If platform_init() was not called returns NULL.
bool            platform_thread_is_main();
void            platform_thread_sleep(double seconds); //Sleeps the calling thread for specified number of seconds. The accuracy is platform and sheduler dependent
void            platform_thread_exit(int code); //Terminates a thread with an exit code
void            platform_thread_yield(); //Yields the remainder of this thread's time slice to another thread
void            platform_thread_detach(Platform_Thread* thread);
bool            platform_thread_join(const Platform_Thread* threads, int64_t count, double seconds_or_negative_if_infinite); //Blocks calling thread until all threads finish. Must not join the current calling thread!

Platform_Error platform_thread_launch(Platform_Thread* thread_or_null, int64_t stack_size_or_zero, int (*func)(void*), void* context)
{
    Platform_Thread dummy = {0};
    Platform_Thread* thread = thread_or_null ? thread_or_null : &dummy;
    assert(stack_size_or_zero >= 0);

    if(stack_size_or_zero <= 0)
        stack_size_or_zero = 0;

    thread->handle = NULL;
    Platform_Thread_State* thread_state = calloc(1, sizeof(Platform_Thread_State) + sizeof(void*));
    if(thread_state)
    {
        thread_state->func = func;
        memcpy(thread_state + 1, &context, sizeof(void*));
        thread->handle = (void*) _beginthreadex(NULL, (unsigned int) stack_size_or_zero, _thread_func, thread_state, 0, NULL);
    }

    if(thread->handle)
        return PLATFORM_ERROR_OK;
    else
    {
        free(thread_state);
        return (Platform_Error) GetLastError();
    }
}

const char* platform_thread_get_current_name()
{
    return "main";
}
void platform_thread_set_current_name(const char* name, bool dealloc_on_exit)
{
    (void) name;
    (void) dealloc_on_exit;
}
void platform_thread_attach_deinit(void (*func)(void* context), void* context)
{
    Platform_Thread_State* state = _platform_thread_state();
    Platform_Thread_Cleanup cleanup = {func, context};
    int i = state->cleanup_count++;
    if(i < _INLINE_CLEANUPS)
        state->inline_cleanups[i] = cleanup;
    else
    {
        i -= _INLINE_CLEANUPS;
        if(i >= state->cleanup_capacity)
        {
            int new_capacity = state->cleanup_capacity*2 + _INLINE_CLEANUPS;
            void* new_ptr = realloc(state->extended_cleanups, new_capacity * sizeof(Platform_Thread_Cleanup));
            assert(new_ptr);
            
            state->extended_cleanups = new_ptr;
            state->cleanup_capacity = new_capacity;
        }

        state->extended_cleanups[i] = cleanup;
    }
}

Platform_Thread platform_thread_get_current()
{
    Platform_Thread out = {0};
    out.handle = GetCurrentThread();
    return out;
}

int32_t platform_thread_get_current_id()
{
    return (int32_t) GetCurrentThreadId();
}

volatile void* _main_thread_handle = {0};
void _platform_thread_get_main_init()
{
    void* thread = GetCurrentThread();
    _main_thread_handle = thread;
}

Platform_Thread platform_thread_get_main()
{
    Platform_Thread out = {(void*) _main_thread_handle};
    return out;
}
bool platform_thread_is_main()
{
    return platform_thread_get_current().handle == platform_thread_get_main().handle;
}

int64_t platform_thread_get_proccessor_count()
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

int64_t platform_thread_get_exit_code(Platform_Thread finished_thread)
{
    DWORD out = 0;
    if(!!GetExitCodeThread((HANDLE) finished_thread.handle, &out))
        return INT64_MIN;
    else
        return out;
}

bool platform_thread_join(const Platform_Thread* threads, int64_t count, double seconds_or_negative_if_infinite)
{
    DWORD timeout = INFINITE;
    if(seconds_or_negative_if_infinite > 0)
        timeout = (DWORD) (seconds_or_negative_if_infinite * 1000);

    DWORD result = 0;
    if(count == 1)
        result = WaitForSingleObject((HANDLE) threads[0].handle, timeout);
    else
    {
        //@NOTE: In case of more then 256 handles we should wait differently but we dont because I am lazy
        bool wait_for_all = true;
        HANDLE handles[256] = {0};
        for(int64_t i = 0; i < count;)
        {
            int64_t handle_count = 0;
            for(; handle_count < 256; handle_count ++, i++)
                handles[handle_count] = (HANDLE) threads[i].handle;

            result = WaitForMultipleObjects((DWORD) handle_count, handles, wait_for_all, timeout);
        }
    }

    return result != WAIT_TIMEOUT;
}

void platform_thread_detach(Platform_Thread* thread)
{
    if(thread->handle != NULL)
    {
        bool state = CloseHandle(thread->handle);
        thread->handle = NULL;
        assert(state); 
    }
}

Platform_Error platform_mutex_init(Platform_Mutex* mutex)
{
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

#pragma comment(lib, "synchronization.lib")
#include <process.h>
bool platform_futex_wait(volatile void* futex, uint32_t value, double seconds_or_negative_if_infinite)
{
    DWORD wait = INFINITE;
    if(seconds_or_negative_if_infinite > 0)
        wait = (DWORD) (seconds_or_negative_if_infinite * 1000);
    bool state = (bool) WaitOnAddress(futex, &value, sizeof value, wait);
    if(state == false)
    {
        #ifndef NDEBUG
        DWORD err = GetLastError(); 
        assert(err == ERROR_TIMEOUT);
        #endif
        return false;
    }
    return true;
}
void platform_futex_wake(volatile void* futex)
{
    WakeByAddressSingle((void*) futex);
}
void platform_futex_wake_all(volatile void* futex)
{
    WakeByAddressAll((void*) futex);
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
    assert(item_size == data_size);
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
    return append_to->data;
}

static wchar_t* _utf8_to_utf16(WString_Buffer* append_to_or_null, const char* string, int64_t string_size) 
{
    WString_Buffer local = {0};
    WString_Buffer* append_to = append_to_or_null ? append_to_or_null : &local;

    int utf16len = MultiByteToWideChar(CP_UTF8, 0, string, (int) string_size, NULL, 0);
    buffer_resize(append_to, utf16len);
    MultiByteToWideChar(CP_UTF8, 0, string, (int) string_size, append_to->data, (int) utf16len);
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
            char c = translated[needed_size - 1];
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

//Opens the file in the specified combination of Platform_File_Open_Flags. 
Platform_Error platform_file_open(Platform_File* file, Platform_String file_path, int open_flags)
{
    platform_file_close(file);

    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);
    
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD access = 0;
    if(open_flags & PLATFORM_FILE_MODE_READ)
        access |= GENERIC_READ;
    if(open_flags & PLATFORM_FILE_MODE_WRITE)
        access |= GENERIC_WRITE;
    if(open_flags & PLATFORM_FILE_MODE_APPEND)
        access |= FILE_APPEND_DATA;

    LPSECURITY_ATTRIBUTES security = NULL;

    DWORD creation = OPEN_EXISTING; 
    if(open_flags & PLATFORM_FILE_MODE_REMOVE_CONTENT)
    {
        if(open_flags & PLATFORM_FILE_MODE_CREATE_MUST_NOT_EXIST)
            creation = CREATE_NEW;
        else if(open_flags & PLATFORM_FILE_MODE_CREATE)
            creation = CREATE_ALWAYS;
    }
    else
    {
        if(open_flags & PLATFORM_FILE_MODE_CREATE_MUST_NOT_EXIST)
            creation = CREATE_NEW;
        else if(open_flags & PLATFORM_FILE_MODE_CREATE)
            creation = OPEN_ALWAYS;
    }
    
    DWORD flags = FILE_ATTRIBUTE_NORMAL;

    HANDLE template_handle = NULL;
    HANDLE handle = CreateFileW(path, access, share, security, creation, flags, template_handle);
    bool state = handle != INVALID_HANDLE_VALUE; 
    if(state)
    { 
        file->handle.windows = handle;
        file->is_open = true;
    }

    buffer_deinit(&buffer);
    return _platform_error_code(state);
}

Platform_Error platform_file_close(Platform_File* file)
{
    bool state = true;
    if(file->is_open)
        state = !!CloseHandle((HANDLE) file->handle.windows);

    memset(file, 0, sizeof *file);
    return _platform_error_code(state);
}

Platform_Error platform_file_read(Platform_File* file, void* buffer, int64_t size, int64_t* read_bytes_because_eof)
{
    bool state = true;
    int64_t total_read = 0;
    if(file->is_open)
    {
        // BOOL ReadFile(
        //     [in]                HANDLE       hFile,
        //     [out]               LPVOID       lpBuffer,
        //     [in]                DWORD        nNumberOfBytesToRead,
        //     [out, optional]     LPDWORD      lpNumberOfBytesRead,
        //     [in, out, optional] LPOVERLAPPED lpOverlapped
        // );
        
        for(; total_read < size;)
        {
            int64_t _GB = 1 << 30;
            int64_t to_read = size - total_read;
            if(to_read > _GB)
                to_read = _GB;

            DWORD bytes_read = 0;
            state = !!ReadFile((HANDLE) file->handle.windows, (unsigned char*) buffer + total_read, (DWORD) to_read, &bytes_read, NULL);
            //Eof found!
            if(state && bytes_read <= 0)
                break;

            //Error
            if(state == false)
                break;

            total_read += bytes_read;
        }
    }

    if(read_bytes_because_eof)
        *read_bytes_because_eof = total_read;

    return _platform_error_code(state);
}

Platform_Error platform_file_write(Platform_File* file, const void* buffer, int64_t size)
{
    bool state = true;
    if(file->is_open)
    {
        // BOOL WriteFile(
        //     [in]                HANDLE       hFile,
        //     [in]                LPCVOID      lpBuffer,
        //     [in]                DWORD        nNumberOfBytesToWrite,
        //     [out, optional]     LPDWORD      lpNumberOfBytesWritten,
        //     [in, out, optional] LPOVERLAPPED lpOverlapped
        // );

        for(int64_t total_written = 0; total_written < size;)
        {
            int64_t _GB = 1 << 30;
            int64_t to_write = size - total_written;
            if(to_write > _GB)
                to_write = _GB;

            DWORD bytes_written = 0;
            state = !!WriteFile((HANDLE) file->handle.windows, (unsigned char*) buffer + total_written, (DWORD) to_write, &bytes_written, NULL);
            if(state == false || bytes_written <= 0)
            {
                state = false;
                break;
            }

            total_written += bytes_written;
        }
    }

    return _platform_error_code(state);
}

Platform_Error _platform_file_seek_tell(Platform_File* file, int64_t offset, int64_t* new_offset, Platform_File_Seek from)
{
    // BOOL SetFilePointerEx(
    //     [in]            HANDLE         hFile,
    //     [in]            LARGE_INTEGER  liDistanceToMove,
    //     [out, optional] PLARGE_INTEGER lpNewFilePointer,
    //     [in]            DWORD          dwMoveMethod
    // );

    bool state = true;
    LARGE_INTEGER new_offset_win = {0}; 
    if(file->is_open)
    {
        LARGE_INTEGER offset_win = {0};
        offset_win.QuadPart = offset;
        //@NOTE: Platform_File_Seek from has matching values to the windows API values
        state = !!SetFilePointerEx((HANDLE) file->handle.windows, offset_win, &new_offset_win, (DWORD) from);
    }

    if(new_offset)
        *new_offset = new_offset_win.QuadPart;

    return _platform_error_code(state);
}

Platform_Error platform_file_tell(Platform_File file, int64_t* offset)
{
    return _platform_file_seek_tell(&file, 0, offset, PLATFORM_FILE_SEEK_FROM_CURRENT);
}

Platform_Error platform_file_seek(Platform_File* file, int64_t offset, Platform_File_Seek from)
{
    return _platform_file_seek_tell(file, offset, NULL, from);
}

Platform_Error platform_file_flush(Platform_File* file)
{
    bool state = true;
    if(file->is_open)
        state = !!FlushFileBuffers((HANDLE) file->handle.windows);
    
    return _platform_error_code(state);
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

Platform_Error platform_file_resize(Platform_String file_path, int64_t size)
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
    Platform_File_Info info = {0};
    WIN32_FILE_ATTRIBUTE_DATA native_info = {0};
    
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);
    bool state = !!GetFileAttributesExW(path, GetFileExInfoStandard, &native_info);
    
    if(native_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        info.link_type = _get_link_type(path);
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

BOOL _platform_directory_exists(const wchar_t* szPath)
{
  DWORD dwAttrib = GetFileAttributesW(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
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

WString_Buffer _vwformat_malloc(WString_Buffer* into_or_null, const wchar_t* format, va_list args)
{
    if(format == NULL)
        format = L"";

    //gcc modifies va_list on use! make sure to copy it!
    va_list args_copy;
    va_copy(args_copy, args);
    int count = vswprintf(NULL, 0, format, args);

    WString_Buffer backup = {0};
    if(into_or_null == NULL)
        into_or_null = &backup;

    buffer_resize(into_or_null, count);
    vswprintf(into_or_null->data, (size_t) count + 1, format, args_copy);
    return *into_or_null;
}

WString_Buffer _wformat_malloc(WString_Buffer* into_or_null, const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    WString_Buffer out = _vwformat_malloc(into_or_null, format, args);
    va_end(args);
    return out;
}

Platform_Error platform_directory_list_contents_alloc(Platform_String path, Platform_Directory_Entry** _entries, int64_t* _entries_count, int64_t max_depth)
{
    if(max_depth == -1)
        max_depth = INT64_MAX;
    if(max_depth <= 0)
        return _platform_error_code(true);

    typedef struct Dir_Iterator {
        WIN32_FIND_DATAW current_entry;
        HANDLE first_found;
        bool failed;
        bool had_first;
        bool _pad[6];
        WString_Buffer path;    
        int64_t index;  
    } Dir_Iterator;

    DEFINE_BUFFER_TYPE(Dir_Iterator, Dir_Iterator_Buffer);
    DEFINE_BUFFER_TYPE(Platform_Directory_Entry, Platform_Directory_Entry_Buffer);
    
    Platform_Directory_Entry_Buffer entries = {0};
    Dir_Iterator_Buffer dir_iterators = {0};
    buffer_init_backed(&dir_iterators, 16);

    {
        Dir_Iterator first = {0};
        _wstring_path(&first.path, path);
        buffer_push(&dir_iterators, first);
    }
    
    WString_Buffer temp = {0};
    buffer_init_backed(&temp, _LOCAL_BUFFER_SIZE);

    Platform_Error error = PLATFORM_ERROR_OK;
    while(dir_iterators.size > 0)
    {
        Dir_Iterator* it = &dir_iterators.data[dir_iterators.size - 1];

        if(it->had_first)
            it->failed = !FindNextFileW(it->first_found, &it->current_entry);
        else
        {
            //int count = swprintf(NULL, 0, L"%s\\*.*", it->path.data);
            _wformat_malloc(&temp, L"%s\\*.*", it->path.data);
            it->first_found = FindFirstFileW(temp.data, &it->current_entry);
            it->had_first = true;
            if(it->first_found == INVALID_HANDLE_VALUE)
            {
                it->failed = true;
                if(dir_iterators.size == 1)
                    error = _platform_error_code(false);
            }
        }

        if(it->failed)
        {
            if(it->first_found != INVALID_HANDLE_VALUE && it->first_found != NULL)
                FindClose(it->first_found);

            buffer_deinit(&it->path);
            buffer_resize(&dir_iterators, dir_iterators.size - 1);
        }
        else if(wcscmp(it->current_entry.cFileName, L".") != 0 && wcscmp(it->current_entry.cFileName, L"..") != 0)
        {
            it->index += 1;
            _wformat_malloc(&temp, L"%s\\%s", it->path.data, it->current_entry.cFileName);
        
            Platform_File_Info info = {0};
            info.created_epoch_time = _filetime_to_epoch_time(it->current_entry.ftCreationTime);
            info.last_access_epoch_time = _filetime_to_epoch_time(it->current_entry.ftLastAccessTime);
            info.last_write_epoch_time = _filetime_to_epoch_time(it->current_entry.ftLastWriteTime);
            info.size = ((int64_t) it->current_entry.nFileSizeHigh << 32) | ((int64_t) it->current_entry.nFileSizeLow);
        
            info.type = PLATFORM_FILE_TYPE_FILE;
            if(it->current_entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                info.last_access_epoch_time = info.created_epoch_time;
                info.last_write_epoch_time = info.created_epoch_time;
                info.type = PLATFORM_FILE_TYPE_DIRECTORY;
            }
            else
                info.type = PLATFORM_FILE_TYPE_FILE;

            if(it->current_entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                info.link_type = _get_link_type(temp.data);  
            
            Platform_Directory_Entry entry = {0};
            entry.info = info; 
            entry.path = _string_path(NULL, temp.data, temp.size);
            entry.directory_depth = dir_iterators.size - 1;
            buffer_push(&entries, entry);

            assert(dir_iterators.size < 10000 && "must not get stuck in an infinite loop");
            if(info.type == PLATFORM_FILE_TYPE_DIRECTORY && dir_iterators.size < max_depth)
            {
                Dir_Iterator next = {0};
                buffer_append(&next.path, temp.data, temp.size);
                buffer_push(&dir_iterators, next);
            }
        }
    }
    
    
    if(error != 0)
        buffer_deinit(&entries);
    else
    {
        //Null terminate the entries
        Platform_Directory_Entry terminator = {0};
        buffer_push(&entries, terminator);
        entries.size -= 1; //is not really a valid entry 
    }

    buffer_deinit(&temp);
    buffer_deinit(&dir_iterators);
    
    if(_entries) *_entries = entries.data;
    if(_entries_count) *_entries_count = entries.size;

    return error;
}

void platform_directory_list_contents_free(Platform_Directory_Entry* entries)
{
    if(entries == NULL)
        return;

    int64_t i = 0;
    for(; entries[i].path != NULL; i++)
        free(entries[i].path);
          
    free(entries);
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

Platform_Error platform_directory_get_current_working(void* buffer, int64_t buffer_size, bool* needs_bigger_buffer_or_null)
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
        
    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);
    hFile = CreateFileW(
        path,                            // lpFileName
        GENERIC_READ | GENERIC_WRITE,          // dwDesiredAccess
        FILE_SHARE_READ | FILE_SHARE_WRITE,    // dwShareMode
        NULL,                                  // lpSecurityAttributes
        disposition,                           // dwCreationDisposition
        FILE_ATTRIBUTE_NORMAL,                 // dwFlagsAndAttributes
        0);                                    // hTemplateFile
    buffer_deinit(&buffer);

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


//=========================================
// File watch
//=========================================

enum {
    _FILE_WATCH_CHANGE_CALL = 1,
    _FILE_WATCH_CHANGE_HAS_BUFFER = 2,
};

typedef struct _Platform_File_Watch_Context {
    OVERLAPPED overlapped;
    HANDLE directory;
    HANDLE destroy_notification;
    HANDLE thread;
    DWORD win_flags;
    BOOL win_watch_subdir;
    Platform_Error error;
    CRITICAL_SECTION mutex;

    void (*user_func)(Platform_File_Watch watch, void* context);
    void* user_context;

    int32_t flags;

    String_Buffer watched_path;
    String_Buffer change_path;
    String_Buffer change_old_path;

    PLATFORM_ATOMIC(int32_t) changes;
    PLATFORM_ATOMIC(int32_t) changes_calls;

    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_offset;
} _Platform_File_Watch_Context;

void _platform_file_watch_context_deinit(_Platform_File_Watch_Context* context)
{
    if(context)
    {
        if(context->directory != INVALID_HANDLE_VALUE && context->directory != NULL)
            CloseHandle(context->directory);
        if(context->destroy_notification != INVALID_HANDLE_VALUE && context->destroy_notification != NULL)
            CloseHandle(context->destroy_notification);
        if(context->overlapped.hEvent != INVALID_HANDLE_VALUE && context->overlapped.hEvent != NULL)
            CloseHandle(context->overlapped.hEvent);

        DeleteCriticalSection(&context->mutex);
        buffer_deinit(&context->watched_path);
        buffer_deinit(&context->change_path);
        buffer_deinit(&context->change_old_path);

        free(context->buffer);
            
        context->directory = NULL;
        context->destroy_notification = NULL;
        context->overlapped.hEvent = NULL;
        context->buffer = NULL;
    }
}

void _platform_file_watch_function(void* _context)
{
    _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) _context;
    while (context->error == 0) 
    {
        HANDLE handles[2] = {context->overlapped.hEvent, context->destroy_notification};
        DWORD result = WaitForMultipleObjects((DWORD) 2, handles, false, INFINITE);
        
        //If got file change
        if (result == WAIT_OBJECT_0) {

            EnterCriticalSection(&context->mutex);
            Platform_File_Watch watch = {_context};
            if(context->user_func)
                context->user_func(watch, context->user_context);
            context->changes |= _FILE_WATCH_CHANGE_CALL;
            context->changes_calls += 1;
            LeaveCriticalSection(&context->mutex);
        }

        //If got destroy request exit
        if (result == WAIT_OBJECT_0 + 1) {
            break;
        }
    }

    context->thread = INVALID_HANDLE_VALUE;
    _platform_file_watch_context_deinit(context);
    free(context);
}

Platform_Error platform_file_unwatch(Platform_File_Watch* file_watch_or_null)
{
    Platform_Error out = 0;
    if(file_watch_or_null && file_watch_or_null->handle)
    {
        _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) file_watch_or_null->handle;
        out = context->error;
        if(context->thread != INVALID_HANDLE_VALUE)
            SetEvent(context->destroy_notification);

        file_watch_or_null->handle = NULL;
    }

    return out;
}

Platform_Error platform_file_watch(Platform_File_Watch* file_watch_or_null, Platform_String file_path, int32_t file_watch_flags, void (*signal_func_or_null)(Platform_File_Watch watch, void* context), void* user_context)
{
    (void) file_watch_flags;
    Platform_Error error = {0};
    if(file_watch_or_null)
        error = platform_file_unwatch(file_watch_or_null);

    _Platform_File_Watch_Context context = {0};
    _Platform_File_Watch_Context* context_alloced = NULL;
    BOOL success = TRUE;
    if(error != 0)
        goto fail;

    context.flags = file_watch_flags;
    context.win_watch_subdir = !!(context.flags & PLATFORM_FILE_WATCH_SUBDIRECTORIES);
    context.win_flags = 0;
    if(file_watch_flags & PLATFORM_FILE_WATCH_CREATED)
        context.win_flags |= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION;
    if(file_watch_flags & PLATFORM_FILE_WATCH_DELETED)
        context.win_flags |= FILE_NOTIFY_CHANGE_FILE_NAME;
    if(file_watch_flags & PLATFORM_FILE_WATCH_RENAMED)
        context.win_flags |= FILE_NOTIFY_CHANGE_FILE_NAME;
    if(file_watch_flags & PLATFORM_FILE_WATCH_MODIFIED)
        context.win_flags |= FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES;
    if(file_watch_flags & PLATFORM_FILE_WATCH_DIRECTORY)
        context.win_flags |= FILE_NOTIFY_CHANGE_DIR_NAME;

    context.user_context = user_context;
    context.user_func = signal_func_or_null;
    context.overlapped.hEvent = CreateEventW(NULL, FALSE, 0, NULL);
    context.destroy_notification = CreateEventW(NULL, FALSE, 0, NULL);
    context.buffer_size = 10*1024;
    context.buffer = (uint8_t*) malloc(context.buffer_size);
    buffer_reserve(&context.change_path, _LOCAL_BUFFER_SIZE);
    buffer_reserve(&context.change_old_path, _LOCAL_BUFFER_SIZE);
    buffer_append(&context.watched_path, file_path.data, file_path.count);

    WString_Buffer buffer = {0}; buffer_init_backed(&buffer, _LOCAL_BUFFER_SIZE);
    const wchar_t* path = _wstring_path(&buffer, file_path);
        context.directory = CreateFileW(path,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);
    buffer_deinit(&buffer);

    InitializeCriticalSection(&context.mutex);
    if(error = _platform_error_code(context.directory != INVALID_HANDLE_VALUE && context.overlapped.hEvent != NULL && context.buffer), error != 0)
        goto fail;
        
    context_alloced = (_Platform_File_Watch_Context*) malloc(sizeof *context_alloced);
    if(context_alloced == NULL)
        goto fail;

    success = ReadDirectoryChangesW(
        context.directory, context.buffer, (DWORD) context.buffer_size, 
        context.win_watch_subdir, context.win_flags, NULL, &context.overlapped, NULL);
    if(error = _platform_error_code(success && context_alloced != NULL), error != 0)
        goto fail;

    *context_alloced = context;
    context_alloced->thread = (HANDLE) _beginthread(_platform_file_watch_function, 0, context_alloced);
    if(error = _platform_error_code(context_alloced->thread != (HANDLE) -1), error != 0)
        goto fail;

    fail:
    if(error != 0)
    {
        _platform_file_watch_context_deinit(&context);
        free(context_alloced);
    }
    else
    {
        if(file_watch_or_null)
            file_watch_or_null->handle = context_alloced;
    }

    return error;
}

const char* platform_file_watch_get_info(Platform_File_Watch file_watch, int32_t* flags_or_null)
{
    _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) file_watch.handle;
    const char* path = "";
    if(context)
    {
        if(flags_or_null)  
            *flags_or_null = context->flags;

        path = context->watched_path.data;
    }

    return path;
}

bool platform_file_watch_poll(Platform_File_Watch file_watch, Platform_File_Watch_Event* user_event)
{   
    bool ret = false;
    (void) user_event;
    _Platform_File_Watch_Context* context = (_Platform_File_Watch_Context*) file_watch.handle;
    if(context)
    {
        //Atomic to not have to enter and leave critical section when its not needed.
        int32_t changes = atomic_load(&context->changes);
        if(changes != 0)
        {
            //... okay some changes *probably* occurred
            EnterCriticalSection(&context->mutex);

            //If there were change calls (ie we got a notification from the OS something changed)
            // but we havent yet polled the OS buffer
            //  - poll the buffer 
            //  - set the context->changes to signal we have polled the buffer
            if((context->changes & _FILE_WATCH_CHANGE_CALL) && (context->changes & _FILE_WATCH_CHANGE_HAS_BUFFER) == 0)
            {
                DWORD bytes_transferred = 0;
                GetOverlappedResult(context->directory, &context->overlapped, &bytes_transferred, FALSE);
                
                context->buffer_offset = 0;
                context->changes_calls = 0;
                context->changes = _FILE_WATCH_CHANGE_HAS_BUFFER;
            }

            //Iterate changes until we find a change that matches the user selection
            // + handle the rename events which are split between two calls
            int32_t modification = 0;
            while((context->changes & _FILE_WATCH_CHANGE_HAS_BUFFER) && modification == 0)
            {
                FILE_NOTIFY_INFORMATION *event = NULL;
                modification = 0;
                buffer_resize(&context->change_old_path, 0);
                buffer_resize(&context->change_path, 0);

                do {
                    event = (FILE_NOTIFY_INFORMATION*) (context->buffer + context->buffer_offset);
                    
                    //Fill out info and convert paths
                    switch (event->Action) {
                        case FILE_ACTION_ADDED: modification = PLATFORM_FILE_WATCH_CREATED; break;
                        case FILE_ACTION_REMOVED: modification = PLATFORM_FILE_WATCH_DELETED; break;
                        case FILE_ACTION_MODIFIED: modification = PLATFORM_FILE_WATCH_MODIFIED; break;
                        case FILE_ACTION_RENAMED_OLD_NAME: modification = PLATFORM_FILE_WATCH_RENAMED; break;
                        case FILE_ACTION_RENAMED_NEW_NAME: modification = PLATFORM_FILE_WATCH_RENAMED; break;
                    }
                    
                    DWORD path_len = event->FileNameLength / sizeof(wchar_t);
                    if(event->Action == FILE_ACTION_RENAMED_OLD_NAME)
                        _string_path(&context->change_old_path, event->FileName, path_len);
                    else
                        _string_path(&context->change_path, event->FileName, path_len);

                    if (event->NextEntryOffset) {
                        //If there is next entry iterate to it
                        context->buffer_offset += event->NextEntryOffset;
                        context->changes |= _FILE_WATCH_CHANGE_HAS_BUFFER;
                    } 
                    else {
                        //Else queue more changes
                        context->changes &= ~_FILE_WATCH_CHANGE_HAS_BUFFER;

                        BOOL success = ReadDirectoryChangesW(
                            context->directory, context->buffer, (DWORD) context->buffer_size, 
                            context->win_watch_subdir, context->win_flags,
                            NULL, &context->overlapped, NULL);

                        context->error = _platform_error_code(!!success);
                    }
                } while(event->Action == FILE_ACTION_RENAMED_OLD_NAME && event->NextEntryOffset);

                //If the flags dont match what the user asked for iterate again
                modification = modification & context->flags;
            }

            if(modification == 0)
                ret = false;
            else
            {
                ret = true;
                user_event->action = (Platform_File_Watch_Flag) modification;
                user_event->_ = 0;
                user_event->path = context->change_path.data;
                user_event->old_path = context->change_old_path.data;
                user_event->watched_path = context->watched_path.data;
            }
            
            LeaveCriticalSection(&context->mutex);
        }
    }

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
// Window management
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
    WString_Buffer title_backed = {0}; buffer_init_backed(&title_backed, _LOCAL_BUFFER_SIZE);
    WString_Buffer message_backed = {0}; buffer_init_backed(&message_backed, _LOCAL_BUFFER_SIZE);
    const wchar_t* title_wide =  _utf8_to_utf16(&title_backed, title.data, title.count);
    const wchar_t* message_wide = _utf8_to_utf16(&message_backed, message.data, message.count);
        value = MessageBoxW(0, message_wide, title_wide, style | icon);
    buffer_deinit(&title_backed);
    buffer_deinit(&message_wide);
    
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

int platform_is_debugger_atached()
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
    Platform_Exception exception;
    int32_t signal_handler_depth;

    jmp_buf jump_buffer;
    CONTEXT context;
} Platform_Sandbox_State;
#pragma warning(default:4324)

__declspec(thread) Platform_Sandbox_State sandbox_state = {0};
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
        error.call_stack_size = (int32_t) error_state.stack_size;
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

void platform_init()
{
    platform_deinit();
    _platform_thread_get_main_init();

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