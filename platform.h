#ifndef JOT_PLATFORM
#define JOT_PLATFORM

#include <stdint.h>
#include <stdbool.h>

//This is a complete operating system abstarction layer. Its implementation is as stright forward and light as possible.
//It uses sized strings on all imputs and returns null terminated strings for maximum compatibility and performance.
//It tries to minimize the need to track state user side instead tries to operate on fixed ammount of mutable buffers.

//Why we need this:
//  1) Practical
//      The c standard library is extremely minimalistic so if we wish to list all files in a directory there is no other way.
// 
//  2) Idealogical
//     Its necessary to udnerstand the bedrock of any medium we are working with. Be it paper, oil & canvas or code, 
//     understanding the medium will help us define strong limitations on the final problem solutions. This is possible
//     and this isnt. Yes or no. This drastically shrinks the design space of any problem which allow for deeper exploration of it. 
//     
//     Interestingly it does not only shrink the design space it also makes it more defined. We see more oportunities that we 
//     wouldnt have seen if we just looked at some high level abstarction library. This can lead to development of better abstractions.
//  
//     Further having absolute control over the system is rewarding. Having the knowledge of every single operation that goes on is
//     immensely satisfying.


//=========================================
// Platform layer setup
//=========================================

//Initializes the platform layer interface. 
//Should be called before calling any other function.
void platform_init();

//Deinitializes the platform layer, freeing all allocated resources back to os.
//platform_init() should be called before using any other fucntion again!
void platform_deinit();

typedef struct Platform_Allocator {
    void* (*reallocate)(void* context, int64_t new_size, void* old_ptr, int64_t old_size);
    void* context;
} Platform_Allocator;

//Sets a different allocator used for internal allocations. This allocator must never fail (for the moment).
//The semantics must be quivalent to:
//   if(new_size == 0) return free(old_ptr);
//   else              return realloc(old_ptr, new_size);
//context is user defined argument and old_size is purely informative (can be used for tracking purposes).
//The value pointed to by context is not copied and needs to remain valid untill call to platform_deinit()!
void platform_set_internal_allocator(Platform_Allocator allocator);


//=========================================
// Virtual memory
//=========================================

typedef enum Platform_Virtual_Allocation {
    PLATFORM_VIRTUAL_ALLOC_RESERVE  = 0, //Reserves adress space so that no other allocation can be made there
    PLATFORM_VIRTUAL_ALLOC_COMMIT   = 1, //Commits adress space causing operating system to suply physical memory or swap file
    PLATFORM_VIRTUAL_ALLOC_DECOMMIT = 2, //Removes adress space from commited freeing physical memory
    PLATFORM_VIRTUAL_ALLOC_RELEASE  = 3, //Free adress space
} Platform_Virtual_Allocation;

typedef enum Platform_Memory_Protection {
    PLATFORM_MEMORY_PROT_NO_ACCESS  = 0,
    PLATFORM_MEMORY_PROT_READ       = 1,
    PLATFORM_MEMORY_PROT_WRITE      = 2,
    PLATFORM_MEMORY_PROT_READ_WRITE = 3,
} Platform_Memory_Protection;

void* platform_virtual_reallocate(void* allocate_at, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection);
void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t old_size, int64_t align);
int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align); //returns the size in bytes of the allocated block. Useful for compatibility with APIs that expect malloc/free type allocation functions without explicit size

//=========================================
// Errors 
//=========================================

typedef uint32_t Platform_Error;
enum {PLATFORM_ERROR_OK = 0};

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

//initializes a new thread and immedietely starts it with the func function.
//The thread has stack_size_or_zero bytes of stack sizes rounded up to page size
//If stack_size_or_zero is zero or lower uses system default stack size.
Platform_Error  platform_thread_init(Platform_Thread* thread, int (*func)(void*), void* context, int64_t stack_size_or_zero); 
//Deinits a thread. If the thread is still running it is killed! Call platform_thread_join before to ensure it has finished
void            platform_thread_deinit(Platform_Thread* thread); 

Platform_Thread platform_thread_get_current(); //Returns handle to the calling thread
void            platform_thread_sleep(int64_t ms); //Sleeps the calling thread for ms milliseconds
int             platform_thread_join(Platform_Thread thread); //Blocks calling thread until the thread finishes and returns it state. Must not join the current calling thread!
void            platform_thread_exit(int code); //Terminates a thread with an exit code
void            platform_thread_yield(); //Yields the remainder of this thread's time slice to the OS

//@TODO: make the only function!
int             platform_threads_join(const Platform_Thread* threads, int64_t count);

Platform_Error  platform_mutex_init(Platform_Mutex* mutex);
void            platform_mutex_deinit(Platform_Mutex* mutex);
Platform_Error  platform_mutex_acquire(Platform_Mutex* mutex);
void            platform_mutex_release(Platform_Mutex* mutex);


//=========================================
// Atomics 
//=========================================
inline static void platform_compiler_memory_fence();
inline static void platform_memory_fence();
inline static void platform_processor_pause();

//Returns the first/last set (1) bit position. If num is zero result is undefined.
//The follwing invarints hold (analogous for 64 bit)
// (num & (1 << platform_find_first_set_bit32(num)) != 0
// (num & (1 << (32 - platform_find_last_set_bit32(num))) != 0
inline static int32_t platform_find_first_set_bit32(uint32_t num);
inline static int32_t platform_find_first_set_bit64(uint64_t num);
inline static int32_t platform_find_last_set_bit32(uint32_t num); 
inline static int32_t platform_find_last_set_bit64(uint64_t num);

//Returns the number of set (1) bits 
inline static int32_t platform_pop_count32(uint32_t num);
inline static int32_t platform_pop_count64(uint64_t num);

//Standard Compare and Swap (CAS) semantics.
//Performs atomically: {
//   if(*target != old_value)
//      return false;
// 
//   *target = new_value;
//   return true;
// }
inline static bool platform_interlocked_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value);
inline static bool platform_interlocked_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value);

//Performs atomically: { int64_t copy = *target; *target = value; return copy; }
inline static int64_t platform_interlocked_excahnge64(volatile int64_t* target, int64_t value);
inline static int32_t platform_interlocked_excahnge32(volatile int32_t* target, int32_t value);

//Performs atomically: { int64_t copy = *target; *target += value; return copy; }
inline static int32_t platform_interlocked_add32(volatile int32_t* target, int32_t value);
inline static int64_t platform_interlocked_add64(volatile int64_t* target, int64_t value);

//@TODO: we dont need atomic increment if the compiler is smart enough to change: 
// platform_interlocked_add32(target, 1) ~~~> platform_interlocked_increment32(target)
// Test it on MSVC!

//Performs atomically: { target += 1; return target}
inline static int32_t platform_interlocked_increment32(volatile int32_t* target);
inline static int64_t platform_interlocked_increment64(volatile int64_t* target);

//Performs atomically: { target -= 1; return target}
inline static int32_t platform_interlocked_decrement32(volatile int32_t* target);
inline static int64_t platform_interlocked_decrement64(volatile int64_t* target);


//=========================================
// Timings
//=========================================

typedef struct Platform_Calendar_Time {
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

//returns the number of micro-seconds since the start of the epoch.
//This functions is very fast and suitable for fast profiling
int64_t platform_epoch_time(); 
//returns the number of micro-seconds since the start of the epoch
// with respect to local timezones/daylight saving times and other
int64_t platform_local_epoch_time();     
//returns the number of micro-seconds between the epoch and the call to platform_init()
int64_t platform_startup_epoch_time(); 

//converts the epoch time (micro second time since unix epoch) to calendar representation
Platform_Calendar_Time platform_epoch_time_to_calendar_time(int64_t epoch_time_usec);
//Converts calendar time to the precise epoch time (micro second time since unix epoch)
int64_t platform_calendar_time_to_epoch_time(Platform_Calendar_Time calendar_time);

//Returns the current value of monotonic lowlevel performance counter. Is ideal for benchamrks.
//Generally is with nanosecond precisions.
int64_t platform_perf_counter();         
//returns the frequency of the performance counter (that is counter ticks per second)
int64_t platform_perf_counter_frequency();  
//returns platform_perf_counter() take at time of platform_init()
int64_t platform_perf_counter_startup();    

//=========================================
// Filesystem
//=========================================
typedef struct Platform_String {
    const char* data;
    int64_t size;
} Platform_String;

typedef enum Platform_File_Type {
    PLATFORM_FILE_TYPE_NOT_FOUND = 0,
    PLATFORM_FILE_TYPE_FILE = 1,
    PLATFORM_FILE_TYPE_DIRECTORY = 4,
    PLATFORM_FILE_TYPE_CHARACTER_DEVICE = 2,
    PLATFORM_FILE_TYPE_PIPE = 3,
    PLATFORM_FILE_TYPE_SOCKET = 5,
    PLATFORM_FILE_TYPE_OTHER = 6,
} Platform_File_Type;

typedef enum Platform_Link_Type {
    PLATFORM_LINK_TYPE_NOT_LINK = 0,
    PLATFORM_LINK_TYPE_HARD = 1,
    PLATFORM_LINK_TYPE_SOFT = 2,
    PLATFORM_LINK_TYPE_SYM = 3,
    PLATFORM_LINK_TYPE_OTHER = 4,
} Platform_Link_Type;

typedef struct Platform_File_Info {
    int64_t size;
    Platform_File_Type type;
    Platform_Link_Type link_type;
    int64_t created_epoch_time;
    int64_t last_write_epoch_time;  
    int64_t last_access_epoch_time; //The last time file was either read or written
} Platform_File_Info;
    
typedef struct Platform_Directory_Entry {
    char* path;
    int64_t index_within_directory;
    int64_t directory_depth;
    Platform_File_Info info;
} Platform_Directory_Entry;

typedef struct Platform_Memory_Mapping {
    void* address;
    int64_t size;
    uint64_t state[8];
} Platform_Memory_Mapping;

//retrieves info about the specified file or directory
Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null);
//Creates an empty file at the specified path. Succeeds if the file exists after the call.
//Saves to was_just_created wheter the file was just now created. If is null doesnt save anything.
Platform_Error platform_file_create(Platform_String file_path, bool* was_just_created);
//Removes a file at the specified path. Succeeds if the file exists after the call the file does not exist.
//Saves to was_just_deleted wheter the file was just now deleted. If is null doesnt save anything.
Platform_Error platform_file_remove(Platform_String file_path, bool* was_just_deleted);
//Moves or renames a file. If the file cannot be found or renamed to file that already exists, fails.
Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path);
//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path);
//Resizes a file. The file must exist.
Platform_Error platform_file_resize(Platform_String file_path, int64_t size);

//@TODO: Implement was_just_created for platform_directory_create, platform_directory_remove

//Makes an empty directory
Platform_Error platform_directory_create(Platform_String dir_path);
//Removes an empty directory
Platform_Error platform_directory_remove(Platform_String dir_path);

//changes the current working directory to the new_working_dir.  
Platform_Error platform_directory_set_current_working(Platform_String new_working_dir);    
//Retrieves the absolute path current working directory
const char* platform_directory_get_current_working();    
//Retrieves the absolute path of the executable / dll
const char* platform_get_executable_path();    

//Gathers and allocates list of files in the specified directory. Saves a pointer to array of entries to entries and its size to entries_count. 
//Needs to be freed using directory_list_contents_free()
Platform_Error platform_directory_list_contents_alloc(Platform_String directory_path, Platform_Directory_Entry** entries, int64_t* entries_count, int64_t max_depth);
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

//Creates a watch of a diretcory monitoring for events described in the file_watch_flags. 
//The async_func get called on another thread every time the appropriate action happens. This thread is in blocked state otherwise.
//If async_func returns false the file watch is closed and no further actions are reported.
Platform_Error platform_file_watch(Platform_File_Watch* file_watch, Platform_String dir_path, int32_t file_watch_flags, bool (*async_func)(void* context), void* context);
//Deinits the file watch stopping the monitoring thread.
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
Platform_Error platform_file_memory_map(Platform_String file_path, int64_t desired_size_or_zero, Platform_Memory_Mapping* mapping);
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

//Makes default shell popup with a custom message and style
Platform_Window_Popup_Controls  platform_window_make_popup(Platform_Window_Popup_Style desired_style, Platform_String message, Platform_String title);

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
#define platform_trap() (*(char*)0 = 0)
//Aborts the current thread. Identical to abort() from stdlib except can get intercepted by exception handler.
void platform_abort();
//Identical to platform_abort except termination is treated as proper, correct exit (aborting is treated as panicking)
void platform_terminate();

//Captures the current stack frame pointers. 
//Saves up to stack_size pointres into the stack array and returns the number of
//stack frames captures. If the returned number is exactly stack_size a bigger buffer MIGHT be reuqired.
//Skips first skip_count stack pointers from the position of the called. 
//Even with skip_count = 0 this will not be included within the stack
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

//Convertes the sandbox error to string. The string value is the name of the enum
// (PLATFORM_EXCEPTION_ACCESS_VIOLATION -> "PLATFORM_EXCEPTION_ACCESS_VIOLATION")
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
    
    inline static int32_t platform_find_last_set_bit32(uint32_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanReverse(&out, (unsigned long) num);
        return (int32_t) out;
    }
    
    inline static int32_t platform_find_last_set_bit64(uint64_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanReverse64(&out, (unsigned long long) num);
        return (int32_t) out;
    }

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
    
    inline static int32_t platform_pop_count32(uint32_t num)
    {
        return (int32_t) __popcnt((unsigned int) num);
    }
    inline static int32_t platform_pop_count64(uint64_t num)
    {
        return (int32_t) __popcnt64((unsigned __int64)num);
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
#elif defined(__GNUC__) || defined(__clang__)


    #include <signal.h>

    #undef platform_trap
    // #define platform_trap() __builtin_trap() /* bad looks like a fault in program! */
    #define platform_trap() raise(SIGTSTP)

    inline static void platform_compiler_memory_fence() 
    {
        __asm__ __volatile__("":::"memory");
    }

    inline static void platform_memory_fence()
    {
        platform_compiler_memory_fence(); 
        __sync_synchronize();
    }

#if defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h> // For _mm_pause
    inline static void platform_processor_pause()
    {
        _mm_pause();
    }
#else
    #include <time.h>
    inline static void platform_processor_pause()
    {
        struct timespec spec = {0};
        spec.tv_sec = 0;
        spec.tv_nsec = 1;
        nanosleep(spec, NULL);
    }
#endif

    //for refernce see: https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
    inline static int32_t platform_find_last_set_bit32(uint32_t num)
    {
        return __builtin_ffs((int) num) - 1;
    }
    
    inline static int32_t platform_find_last_set_bit64(uint64_t num)
    {
        return __builtin_ffsll((long long) num) - 1;
    }

    inline static int32_t platform_find_first_set_bit32(uint32_t num)
    {
        return 32 - __builtin_ctz((int) num) - 1;
    }
    inline static int32_t platform_find_first_set_bit64(uint64_t num)
    {
        return 64 - __builtin_ctzll((long long) num) - 1;
    }

    inline static int32_t platform_pop_count32(uint32_t num)
    {
        return __builtin_popcount((uint32_t) num);
    }
    inline static int32_t platform_pop_count64(uint64_t num)
    {
        return __builtin_popcountll((uint64_t) num);
    }

    //for reference see: https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
    inline static bool platform_interlocked_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value)
    {
        return __atomic_compare_exchange_n(target, &old_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    inline static bool platform_interlocked_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value)
    {
        return __atomic_compare_exchange_n(target, &old_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }

    inline static int64_t platform_interlocked_excahnge64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }
    inline static int32_t platform_interlocked_excahnge32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }

    inline static int32_t platform_interlocked_add32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_add_fetch(target, value, __ATOMIC_SEQ_CST);
    }
    inline static int64_t platform_interlocked_add64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_add_fetch(target, value, __ATOMIC_SEQ_CST);
    }

    inline static int32_t platform_interlocked_increment32(volatile int32_t* target)
    {
        return platform_interlocked_add32(target, 1) + 1;
    }
    inline static int64_t platform_interlocked_increment64(volatile int64_t* target)
    {
        return platform_interlocked_add64(target, 1) + 1;
    }

    inline static int32_t platform_interlocked_decrement32(volatile int32_t* target)
    {
        return platform_interlocked_add32(target, -1) - 1;
    }
    inline static int64_t platform_interlocked_decrement64(volatile int64_t* target)
    {
        return platform_interlocked_add64(target, -1) - 1;
    }

#endif

#endif
