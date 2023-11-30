#ifndef JOT_PLATFORM
#define JOT_PLATFORM

#include <stdint.h>
#include <stdbool.h>

//=========================================
// Virtual memory
//=========================================

void platform_init();
void platform_deinit();

typedef enum Platform_Virtual_Allocation
{
    PLATFORM_VIRTUAL_ALLOC_RESERVE, //Reserves adress space so that no other allocation can be made there
    PLATFORM_VIRTUAL_ALLOC_COMMIT,  //Commits adress space causing operating system to suply physical memory or swap file
    PLATFORM_VIRTUAL_ALLOC_DECOMMIT,//Removes adress space from commited freeing physical memory
    PLATFORM_VIRTUAL_ALLOC_RELEASE, //Free adress space
} Platform_Virtual_Allocation;

typedef enum Platform_Memory_Protection
{
    PLATFORM_MEMORY_PROT_NO_ACCESS,
    PLATFORM_MEMORY_PROT_READ,
    PLATFORM_MEMORY_PROT_WRITE,
    PLATFORM_MEMORY_PROT_READ_WRITE
} Platform_Memory_Protection;

void* platform_virtual_reallocate(void* allocate_at, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection);
void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t old_size, int64_t align);
int64_t platform_heap_get_block_size(void* old_ptr, int64_t align); //returns the size in bytes of the allocated block. Useful for compatibility with APIs that expect malloc/free type allocation functions without explicit size

//=========================================
// Threading
//=========================================

typedef uint32_t Platform_Error;
enum {PLATFORM_ERROR_OK = 0};

//@TODO: CHANGE!! Alongside all other allocation fucntions. Make this file include its own simple allocator interface perfectly comaptible
// with the outside one except not needing get stats and not requiring the allocator data to subclassing thing

//Returns a translated error message. The returned pointer is not static and shall NOT be stored as further calls to this functions will invalidate it. 
//Thus the returned string should be immedietelly printed or copied into a different buffer
const char* platform_translate_error(Platform_Error error);

//=========================================
// Threading
//=========================================

typedef struct Platform_Thread {
    void* handle;
    int32_t id;
} Platform_Thread;

typedef struct Platform_Mutex {
    void* handle;
} Platform_Mutex;

//@TODO: remove the need to destroy thread handles. Make exit and abort sensitive to this 
int64_t         platform_thread_get_proccessor_count();
Platform_Thread platform_thread_create(int (*func)(void*), void* context, int64_t stack_size); //CreateThread
void            platform_thread_destroy(Platform_Thread* thread); //CloseHandle 

int32_t         platform_thread_get_id();
void            platform_thread_yield();
void            platform_thread_sleep(int64_t ms);
void            platform_thread_exit(int code);
int             platform_thread_join(Platform_Thread thread);

//@TODO: make the only function!
int             platform_threads_join(const Platform_Thread* threads, int64_t count);

Platform_Error  platform_mutex_create(Platform_Mutex* mutex);
void            platform_mutex_destroy(Platform_Mutex* mutex);
Platform_Error  platform_mutex_acquire(Platform_Mutex* mutex);
void            platform_mutex_release(Platform_Mutex* mutex);

//=========================================
// Atomics 
//=========================================
inline static void platform_compiler_memory_fence();
inline static void platform_memory_fence();
inline static void platform_processor_pause();


//Returns the first/last set bit position. If num is zero result is undefined
inline static int32_t platform_find_last_set_bit32(uint32_t num); 
inline static int32_t platform_find_last_set_bit64(uint64_t num);
inline static int32_t platform_find_first_set_bit32(uint32_t num);
inline static int32_t platform_find_first_set_bit64(uint64_t num);

//Returns the number of on bits 
inline static int32_t platform_pop_count32(uint32_t num);
inline static int32_t platform_pop_count64(uint64_t num);

inline static bool platform_interlocked_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value);
inline static bool platform_interlocked_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value);
inline static int64_t platform_interlocked_excahnge64(volatile int64_t* target, int64_t value);
inline static int32_t platform_interlocked_excahnge32(volatile int32_t* target, int32_t value);
inline static int32_t platform_interlocked_add32(volatile int32_t* target, int32_t value);
inline static int64_t platform_interlocked_add64(volatile int64_t* target, int64_t value);
inline static int32_t platform_interlocked_increment32(volatile int32_t* target);
inline static int64_t platform_interlocked_increment64(volatile int64_t* target);
inline static int32_t platform_interlocked_decrement32(volatile int32_t* target);
inline static int64_t platform_interlocked_decrement64(volatile int64_t* target);

//=========================================
// Timings
//=========================================

typedef struct Platform_Calendar_Time
{
    int32_t year;       // any
    int8_t month;       // [0, 12)
    int8_t day_of_week; // [0, 7) where 0 is sunday
    int8_t day;         // [0, 31] !note the end bracket!
    
    int8_t hour;        // [0, 24)
    int8_t minute;      // [0, 60)
    int8_t second;      // [0, 60)
    
    int16_t millisecond; // [0, 1000)
    int16_t microsecond; // [0, 1000)
    //int16_t day_of_year; // [0, 365]
} Platform_Calendar_Time;

//returns the number of micro-seconds since the start of the epoch
int64_t platform_universal_epoch_time(); 
//returns the number of micro-seconds since the start of the epoch
// with respect to local timezones/daylight saving times and other
int64_t platform_local_epoch_time();     
//returns the number of micro-seconds between the epoch and the call to platform_init
int64_t platform_startup_epoch_time(); 

Platform_Calendar_Time platform_epoch_time_to_calendar_time(int64_t epoch_time_usec);
int64_t platform_calendar_time_to_epoch_time(Platform_Calendar_Time calendar_time);

int64_t platform_perf_counter();            //returns the current value of performance counter
int64_t platform_perf_counter_startup();    //returns the value of performence conuter at the first time this function was called which is taken as the startup time
int64_t platform_perf_counter_frequency();  //returns the frequency of the performance counter


//=========================================
// Filesystem
//=========================================

typedef enum Platform_File_Type
{
    PLATFORM_FILE_TYPE_NOT_FOUND = 0,
    PLATFORM_FILE_TYPE_FILE = 1,
    PLATFORM_FILE_TYPE_DIRECTORY = 4,
    PLATFORM_FILE_TYPE_CHARACTER_DEVICE = 2,
    PLATFORM_FILE_TYPE_PIPE = 3,
    PLATFORM_FILE_TYPE_OTHER = 5,
} Platform_File_Type;

typedef struct Platform_File_Info
{
    int64_t size;
    Platform_File_Type type;
    int64_t created_epoch_time;
    int64_t last_write_epoch_time;  
    int64_t last_access_epoch_time; //The last time file was either read or written
    bool is_link; //if file/dictionary is actually just a link (hardlink or softlink or symlink)
} Platform_File_Info;
    
typedef struct Platform_Directory_Entry
{
    char* path;
    int64_t path_size;
    int64_t index_within_directory;
    int64_t directory_depth;
    Platform_File_Info info;
} Platform_Directory_Entry;

typedef struct Platform_Memory_Mapping
{
    void* address;
    int64_t size;
    uint64_t state[8];
} Platform_Memory_Mapping;

//retrieves info about the specified file or directory
Platform_Error platform_file_info(const char* file_path, Platform_File_Info* info);
//Creates an empty file at the specified path. Succeeds if the file exists after the call.
//Saves to was_just_created wheter the file was just now created. If is null doesnt save anything.
Platform_Error platform_file_create(const char* file_path, bool* was_just_created);
//Removes a file at the specified path. Succeeds if the file exists after the call
//Saves to was_just_deleted wheter the file was just now deleted. If is null doesnt save anything.
Platform_Error platform_file_remove(const char* file_path, bool* was_just_deleted);
//Moves or renames a file. If the file cannot be found or renamed to file that already exists, fails.
Platform_Error platform_file_move(const char* new_path, const char* old_path);
//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(const char* copy_to_path, const char* copy_from_path);
//Resizes a file. The file must exist.
Platform_Error platform_file_resize(const char* file_path, int64_t size);

//Makes an empty directory
Platform_Error platform_directory_create(const char* dir_path);
//Removes an empty directory
Platform_Error platform_directory_remove(const char* dir_path);

//changes the current working directory to the new_working_dir.  
Platform_Error platform_directory_set_current_working(const char* new_working_dir);    
//Retrieves the current working directory
const char* platform_directory_get_current_working();    
const char* platform_get_executable_path();    

//Gathers and allocates list of files in the specified directory. Saves a pointer to array of entries to entries and its size to entries_count. 
//Needs to be freed using directory_list_contents_free()
Platform_Error platform_directory_list_contents_alloc(const char* directory_path, Platform_Directory_Entry** entries, int64_t* entries_count, int64_t max_depth);
//Frees previously allocated file list
void platform_directory_list_contents_free(Platform_Directory_Entry* entries);

enum {
    PLATFORM_FILE_WATCH_CHANGE      = 1,
    PLATFORM_FILE_WATCH_DIR_NAME    = 2,
    PLATFORM_FILE_WATCH_FILE_NAME   = 4,
    PLATFORM_FILE_WATCH_ATTRIBUTES  = 8,
    PLATFORM_FILE_WATCH_RECURSIVE   = 16,
    PLATFORM_FILE_WATCH_ALL         = 31,
};

typedef struct Platform_File_Watch {
    Platform_Thread thread;
    void* data;
} Platform_File_Watch;

Platform_Error platform_file_watch(Platform_File_Watch* file_watch, const char* file_or_dir_path, int32_t file_wacht_flags, bool (*async_func)(void* context), void* context);
void platform_file_unwatch(Platform_File_Watch* file_watch);

//Memory maps the file pointed to by file_path and saves the adress and size of the mapped block into mapping. 
//If the desired_size_or_zero == 0 maps the entire file. 
//  if the file doesnt exist the function fails.
//If the desired_size_or_zero > 0 maps only up to desired_size_or_zero bytes from the file.
//  The file is resized so that it is exactly desired_size_or_zero bytes (filling empty space with 0)
//  if the file doesnt exist the function creates a new file.
//If the desired_size_or_zero < 0 maps additional desired_size_or_zero bytes from the file 
//    (for appending) extending it by that ammount and filling the space with 0.
//  if the file doesnt exist the function creates a new file.
Platform_Error platform_file_memory_map(const char* file_path, int64_t desired_size_or_zero, Platform_Memory_Mapping* mapping);
//Unmpas the previously mapped file. If mapping is a result of failed platform_file_memory_map does nothing.
void platform_file_memory_unmap(Platform_Memory_Mapping* mapping);

//=========================================
// Window managmenet
//=========================================

typedef enum Platform_Window_Popup_Style
{
    PLATFORM_POPUP_STYLE_OK = 0,
    PLATFORM_POPUP_STYLE_ERROR,
    PLATFORM_POPUP_STYLE_WARNING,
    PLATFORM_POPUP_STYLE_INFO,
    PLATFORM_POPUP_STYLE_RETRY_ABORT,
    PLATFORM_POPUP_STYLE_YES_NO,
    PLATFORM_POPUP_STYLE_YES_NO_CANCEL,
} Platform_Window_Popup_Style;

typedef enum Platform_Window_Popup_Controls
{
    PLATFORM_POPUP_CONTROL_OK,
    PLATFORM_POPUP_CONTROL_CANCEL,
    PLATFORM_POPUP_CONTROL_CONTINUE,
    PLATFORM_POPUP_CONTROL_ABORT,
    PLATFORM_POPUP_CONTROL_RETRY,
    PLATFORM_POPUP_CONTROL_YES,
    PLATFORM_POPUP_CONTROL_NO,
    PLATFORM_POPUP_CONTROL_IGNORE,
} Platform_Window_Popup_Controls;

Platform_Window_Popup_Controls  platform_window_make_popup(Platform_Window_Popup_Style desired_style, const char* message, const char* title);

//=========================================
// Debug
//=========================================
//Could be separate file or project from here on...

typedef struct {
    char function[256]; //mangled or unmangled function name
    char module[256];   //mangled or unmangled module name ie. name of dll/executable
    char file[256];     //file or empty if not supported
    int64_t line;       //0 if not supported;
} Platform_Stack_Trace_Entry;

//Stops the debugger at the call site
#define platform_trap() 
void platform_abort();
void platform_terminate();

//Captures the current stack frame pointers. 
//Saves up to stack_size pointres into the stack array and returns the number of
//stack frames captures. If the returned number is exactly stack_size a bigger buffer
//MIGHT be reuqired.
//Skips first skip_count stack pointers. Even with skip_count = 0 this function should not be
//included within the stack
int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count);

//Translates captured stack into helpful entries. Operates on short fixed width strings to guarantee this function
//will never fail yet translate all needed stack frames. This function should never allocate anything.
void platform_translate_call_stack(Platform_Stack_Trace_Entry* tanslated, const void** stack, int64_t stack_size);

typedef enum Platform_Sandox_Error Platform_Sandox_Error;

//Launches the sandboxed_func inside a sendbox protecting the outside environment 
//from any exceptions that might occur inside sandboxed func this includes hardware
//exceptions. 
//If an exception occurs calls error_func (if not NULL) with the error_code signaling the exception.
//after error_func returns gracefully exits and returns the same error_code as passed into error_func.
//On no error returns 0
Platform_Sandox_Error platform_exception_sandbox(
    void (*sandboxed_func)(void* context),   
    void* sandbox_context,
    void (*error_func)(void* context, Platform_Sandox_Error error_code),   
    void* error_context
);

const char* platform_sandbox_error_to_string(Platform_Sandox_Error error);

typedef enum Platform_Sandox_Error {
    PLATFORM_EXCEPTION_NONE = 0,
    PLATFORM_EXCEPTION_ACCESS_VIOLATION,
    PLATFORM_EXCEPTION_DATATYPE_MISALIGNMENT,
    PLATFORM_EXCEPTION_FLOAT_DENORMAL_OPERAND,
    PLATFORM_EXCEPTION_FLOAT_DIVIDE_BY_ZERO,
    PLATFORM_EXCEPTION_FLOAT_INEXACT_RESULT,
    PLATFORM_EXCEPTION_FLOAT_INVALID_OPERATION,
    PLATFORM_EXCEPTION_FLOAT_OVERFLOW,
    PLATFORM_EXCEPTION_FLOAT_UNDERFLOW,
    PLATFORM_EXCEPTION_FLOAT_OTHER,
    PLATFORM_EXCEPTION_PAGE_ERROR,
    PLATFORM_EXCEPTION_INT_DIVIDE_BY_ZERO,
    PLATFORM_EXCEPTION_INT_OVERFLOW,
    PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION,
    PLATFORM_EXCEPTION_PRIVILAGED_INSTRUCTION,
    PLATFORM_EXCEPTION_BREAKPOINT,
    PLATFORM_EXCEPTION_BREAKPOINT_SINGLE_STEP,
    PLATFORM_EXCEPTION_STACK_OVERFLOW, //cannot be caught inside error_func because of obvious reasons
    PLATFORM_EXCEPTION_ABORT,
    PLATFORM_EXCEPTION_TERMINATE = 0x0001000,
    PLATFORM_EXCEPTION_OTHER = 0x0001001,
} Platform_Sandox_Error; 

// =================== INLINE IMPLEMENTATION ============================
#if defined(_MSC_VER)
    #include <stdio.h>
    #include <intrin.h>
    #include <assert.h>

    #undef platform_trap
    #define platform_trap() __debugbreak() 

    inline static void platform_compiler_memory_fence() 
    {
        _ReadWriteBarrier();
    }

    inline static void platform_memory_fence()
    {
        _ReadWriteBarrier(); 
        __faststorefence();
    }

    inline static void platform_processor_pause()
    {
        _mm_pause();
    }

    //inline static int64_t platform_find_first_set_bit(int64_t num)
    //{
    //    
    //}
    //
    //inline static int64_t platform_find_last_set_bit(int64_t num)
    //{
    //    
    //}
    //
    //inline static int64_t platform_pop_count(int64_t num)
    //{
    //
    //}
    
    
    inline static int32_t platform_find_last_set_bit32(uint32_t num); 
    inline static int32_t platform_find_last_set_bit64(uint64_t num);
    inline static int32_t platform_find_first_set_bit32(uint32_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanForward(&out, (unsigned long) num);
        return (int32_t) out;
    }
    inline static int32_t platform_find_first_set_bit64(uint64_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }

    inline static bool platform_interlocked_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value)
    {
        return _InterlockedCompareExchange64((volatile long long*) target, (long long) new_value, (long long) old_value) == (long long) old_value;
    }

    inline static bool platform_interlocked_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value)
    {
        return _InterlockedCompareExchange((volatile long*) target, (long) new_value, (long) old_value) == (long) old_value;
    }

    inline static int64_t platform_interlocked_excahnge64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) _InterlockedExchange64((volatile long long*) target, (long long) value);
    }

    inline static int32_t platform_interlocked_excahnge32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) _InterlockedExchange((volatile long*) target, (long) value);
    }
    
    inline static int64_t platform_interlocked_add64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) _InterlockedExchangeAdd64((volatile long long*) target, (long long) value);
    }

    inline static int32_t platform_interlocked_add32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) _InterlockedExchangeAdd((volatile long*) target, (long) value);
    }
    
    inline static int32_t platform_interlocked_increment32(volatile int32_t* target)
    {
        return (int32_t) _InterlockedIncrement((volatile long*) target);
    }
    inline static int64_t platform_interlocked_increment64(volatile int64_t* target)
    {
        return (int64_t) _InterlockedIncrement64((volatile long long*) target);
    }
    
    inline static int32_t platform_interlocked_decrement32(volatile int32_t* target)
    {
        return (int32_t) _InterlockedDecrement((volatile long*) target);
    }
    inline static int64_t platform_interlocked_decrement64(volatile int64_t* target)
    {
        return (int64_t) _InterlockedDecrement64((volatile long long*) target);
    }
#endif

#endif
