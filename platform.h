#ifndef MODULE_PLATFORM
#define MODULE_PLATFORM

//Because I cant be bothered making sure nothing has included
// these before we did
#define _GNU_SOURCE
#define _GNU_SOURCE_
#define __USE_GNU
#define __USE_LARGEFILE64
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64 

#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

typedef int64_t isize;

//=========================================
// Define flags
//=========================================

//A non exhaustive list of operating systems
#define PLATFORM_OS_UNKNOWN     0 
#define PLATFORM_OS_WINDOWS     1
#define PLATFORM_OS_ANDROID     2
#define PLATFORM_OS_UNIX        3 // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
#define PLATFORM_OS_BSD         4 // FreeBSD, NetBSD, OpenBSD, DragonFly BSD
#define PLATFORM_OS_APPLE_IOS   5
#define PLATFORM_OS_APPLE_OSX   6
#define PLATFORM_OS_SOLARIS     7 // Oracle Solaris, Open Indiana
#define PLATFORM_OS_HP_UX       8
#define PLATFORM_OS_IBM_AIX     9

#define PLATFORM_COMPILER_UNKNOWN 0
#define PLATFORM_COMPILER_MSVC  1   
#define PLATFORM_COMPILER_GCC   2
#define PLATFORM_COMPILER_CLANG 3
#define PLATFORM_COMPILER_MINGW 4
#define PLATFORM_COMPILER_NVCC  5  //Cuda compiler
#define PLATFORM_COMPILER_NVCC_DEVICE 6  //Cuda compiler device code (kernels)

#define PLATFORM_ENDIAN_LITTLE  0
#define PLATFORM_ENDIAN_BIG     1
#define PLATFORM_ENDIAN_OTHER   2 //We will never use this. But just for completion.

#ifndef PLATFORM_OS
    //Becomes one of the PLATFORM_OS_XXXX based on the detected OS. (see below)
    //One possible use of this is to select the appropiate .c file for platform.h and include it
    // after main making the whole build unity build, greatly simplifying the build procedure.
    //Can be user overriden by defining it before including platform.h
    #define PLATFORM_OS          PLATFORM_OS_UNKNOWN                 
#endif

#ifndef PLATFORM_COMPILER
    //Becomes one of the PLATFORM_COMPILER_XXX based on the detected compiler. (see below). Can be overriden.
    #define PLATFORM_COMPILER          PLATFORM_COMPILER_UNKNOWN                 
#endif

#ifndef PLATFORM_SYSTEM_BITS
    //The address space size of the system. Ie either 64 or 32 bit.
    //Can be user overriden by defining it before including platform.h
    #define PLATFORM_SYSTEM_BITS ((UINTPTR_MAX == 0xffffffff) ? 32 : 64)
#endif

#ifndef PLATFORM_ENDIAN
    //The endianness of the system. Is by default PLATFORM_ENDIAN_LITTLE.
    //Can be user overriden by defining it before including platform.h
    #define PLATFORM_ENDIAN      PLATFORM_ENDIAN_LITTLE
#endif

//Can be used in files without including platform.h but still becomes
// valid if platform.h is included
#if PLATFORM_ENDIAN == PLATFORM_ENDIAN_LITTLE
    #define PLATFORM_HAS_ENDIAN_LITTLE PLATFORM_ENDIAN_LITTLE
#elif PLATFORM_ENDIAN == PLATFORM_ENDIAN_BIG
    #define PLATFORM_HAS_ENDIAN_BIG    PLATFORM_ENDIAN_BIG
#endif

#if defined(_MSC_VER)
    #define PLATFORM_INTRINSIC   __forceinline static
#elif defined(__GNUC__) || defined(__clang__)
    #define PLATFORM_INTRINSIC   __attribute__((always_inline)) inline static
#else
    #define PLATFORM_INTRINSIC   inline static
#endif


//=========================================
// Platform layer setup
//=========================================
void platform_init();
void platform_deinit();

//=========================================
// Errors 
//=========================================
typedef uint32_t Platform_Error;
enum {
    PLATFORM_ERROR_OK = 0, 
    //... errno codes
    PLATFORM_ERROR_OTHER = INT32_MAX, //Is used when the OS reports no error yet there was clearly an error.
};

//Translates error into a textual description stored in translated. Does not write more than translated_size chars.
//translated will always be null terminated, unless translated_size == 0 in which case nothing is written.
//Returns the needed buffer size for the full message.
int64_t platform_translate_error(Platform_Error error, char* translated, int64_t translated_size);

//=========================================
// Virtual memory
//=========================================
typedef enum Platform_Virtual_Allocation {
    PLATFORM_VIRTUAL_ALLOC_RESERVE  = 1, //Reserves address space so that no other allocation can be made there
    PLATFORM_VIRTUAL_ALLOC_COMMIT   = 2, //Commits address space causing operating system to supply physical memory or swap file
    PLATFORM_VIRTUAL_ALLOC_DECOMMIT = 4, //Removes address space from commited freeing physical memory
    PLATFORM_VIRTUAL_ALLOC_RELEASE  = 8, //Free address space
} Platform_Virtual_Allocation;

typedef enum Platform_Memory_Protection {
    PLATFORM_MEMORY_PROT_NO_ACCESS  = 0,
    PLATFORM_MEMORY_PROT_READ       = 1,
    PLATFORM_MEMORY_PROT_WRITE      = 2,
    PLATFORM_MEMORY_PROT_EXECUTE    = 4,
    PLATFORM_MEMORY_PROT_READ_WRITE = PLATFORM_MEMORY_PROT_READ | PLATFORM_MEMORY_PROT_WRITE,
    PLATFORM_MEMORY_PROT_READ_WRITE_EXECUTE = PLATFORM_MEMORY_PROT_READ_WRITE | PLATFORM_MEMORY_PROT_EXECUTE,
} Platform_Memory_Protection;

Platform_Error platform_virtual_reallocate(void** output_adress_or_null, void* address, isize bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection);
isize platform_page_size();
isize platform_allocation_granularity();

//=========================================
// Threading
//=========================================
//We made pretty much all of the threaded functions (except init-like) into non failing
//even though they CAN internally return error (we just assert). That is because:
// 1) One can generally do very little when a mutex (or similar) fails.
// 2) All* error return values are due to a programmer mistake
// 3) On win32 these functions never fail.
//
// *pthread_mutex_lock has a fail state on too many recursive locks and insufficient privileges which are
// not programmer mistake. However in practice they will not happened and if they do we are doing something
// very specific and a custom implementation is preferred (or we can just change this).

Platform_Error  platform_thread_launch(isize stack_size_or_zero, void (*func)(void*), void* context, const char* name_fmt, ...);
int32_t         platform_thread_get_processor_count();
int32_t         platform_thread_get_current_id(); 
int32_t         platform_thread_get_main_id(); //Returns the handle to the thread which called platform_init(). If platform_init() was not called returns -1.
const char*     platform_thread_get_current_name(); 
void            platform_thread_sleep(double seconds); //Sleeps the calling thread for specified number of seconds. The accuracy is platform and scheduler dependent
void            platform_thread_exit(int code); //Terminates a thread with an exit code
void            platform_thread_yield(); //Yields the remainder of this thread's time slice to another thread

//Fast recursive mutex. (pthread_mutex_t on linux, CRITICAL_SECTION win32)
typedef struct Platform_Mutex {
    void* handle;
} Platform_Mutex;

//Small non-recursive mutex which allows multiple readers to hold the mutex at once.
//Only a single writer may hold the mutex at once.
typedef struct Platform_RW_Lock {
    void* handle;
} Platform_RW_Lock;

typedef struct Platform_Cond_Var {
    void* handle;
} Platform_Cond_Var;

Platform_Error  platform_cond_var_init(Platform_Cond_Var* cond_var);
void            platform_cond_var_deinit(Platform_Cond_Var* cond_var);
void            platform_cond_var_wake_single(Platform_Cond_Var* cond_var);
void            platform_cond_var_wake_all(Platform_Cond_Var* cond_var);
bool            platform_cond_var_wait_mutex(Platform_Cond_Var* cond_var, Platform_Mutex* mutex, double seconds_or_negative_if_infinite);
bool            platform_cond_var_wait_rwlock_reader(Platform_Cond_Var* cond_var, Platform_RW_Lock* mutex, double seconds_or_negative_if_infinite);
bool            platform_cond_var_wait_rwlock_writer(Platform_Cond_Var* cond_var, Platform_RW_Lock* mutex, double seconds_or_negative_if_infinite);

Platform_Error  platform_mutex_init(Platform_Mutex* mutex);
void            platform_mutex_deinit(Platform_Mutex* mutex);
void            platform_mutex_lock(Platform_Mutex* mutex);
void            platform_mutex_unlock(Platform_Mutex* mutex);
bool            platform_mutex_try_lock(Platform_Mutex* mutex); 

Platform_Error  platform_rwlock_init(Platform_RW_Lock* mutex);
void            platform_rwlock_deinit(Platform_RW_Lock* mutex);
void            platform_rwlock_reader_lock(Platform_RW_Lock* mutex);
void            platform_rwlock_reader_unlock(Platform_RW_Lock* mutex);
void            platform_rwlock_writer_lock(Platform_RW_Lock* mutex);
void            platform_rwlock_writer_unlock(Platform_RW_Lock* mutex);
bool            platform_rwlock_reader_try_lock(Platform_RW_Lock* mutex);
bool            platform_rwlock_writer_try_lock(Platform_RW_Lock* mutex);

bool            platform_futex_wait(volatile void* futex, uint32_t value, double seconds_or_negative_if_infinite);
void            platform_futex_wake_single(volatile void* futex);
void            platform_futex_wake_all(volatile void* futex);

//Allows a resource to be initialized exactly once even in the case of raacing threads.
//The first thread that reaches this point attomically sets state to initializing value and return true.
//  This thread must eventually call platform_once_end() once its done initializing said resource.
//All other threads are blocked untill platform_once_end() is called.
//After the resource has been initialized these calls exit imemdiately and are evry cheap (just one load).
static bool     platform_once_begin(volatile uint32_t* state);
static void     platform_once_end(volatile uint32_t* state);

//=========================================
// Timings
//=========================================
//returns the number of micro-seconds since the start of the epoch.
//This functions is very fast and suitable for fast profiling
int64_t platform_epoch_time();   
//returns the number of micro-seconds between the epoch and the call to platform_init()
int64_t platform_epoch_time_startup(); 

//Returns the current value of monotonic lowlevel performance counter. Is ideal for benchmarks.
//Generally is with around 1-100 nanosecond precision.
int64_t platform_perf_counter();         
//returns the frequency of the performance counter (that is counter ticks per second)
int64_t platform_perf_counter_frequency();  
//returns platform_perf_counter() take at time of platform_init()
int64_t platform_perf_counter_startup();    

//=========================================
// Files
//=========================================
typedef struct Platform_String {
    const char* data;
    int64_t count;
} Platform_String;

//(file is open) iff (handle != NULL)
typedef union Platform_File {
    void* handle;
} Platform_File;

typedef enum Platform_File_Open_Flags {
    PLATFORM_FILE_OPEN_READ = 1,                    //Read privilege
    PLATFORM_FILE_OPEN_WRITE = 2,                   //Write privilege
    PLATFORM_FILE_OPEN_CREATE = 8,                  //Creates the file, if it already exists does nothing.
    PLATFORM_FILE_OPEN_CREATE_MUST_NOT_EXIST = 16,  //Creates the file, if it already exists fails. When supplied alongside PLATFORM_FILE_OPEN_CREATE overrides it.
    PLATFORM_FILE_OPEN_REMOVE_CONTENT = 32,         //If opening a file that has content, remove it.
    PLATFORM_FILE_OPEN_READ_WRITE = PLATFORM_FILE_OPEN_READ | PLATFORM_FILE_OPEN_WRITE,
    PLATFORM_FILE_OPEN_TEMPORARY = 64,              //Deletes the file on close

    //Hints to the usage of the file. 
    //Have no effect on the semantics but can improve performance in certain cases.
    //Dont even have to be implemented.
    PLATFORM_FILE_OPEN_HINT_UNBUFFERED = 128,        
    PLATFORM_FILE_OPEN_HINT_FRONT_TO_BACK_ACCESS = 256, //Do not use with PLATFORM_FILE_OPEN_HINT_UNBUFFERED
    PLATFORM_FILE_OPEN_HINT_BACK_TO_FRONT_ACCESS = 512, //Do not use with PLATFORM_FILE_OPEN_HINT_UNBUFFERED
    PLATFORM_FILE_OPEN_HINT_RANDOM_ACCESS = 1024, //Do not use with PLATFORM_FILE_OPEN_HINT_UNBUFFERED
} Platform_File_Open_Flags;


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
    PLATFORM_LINK_TYPE_PROBABLY_LINK = 4,
} Platform_Link_Type;

typedef struct Platform_File_Info {
    int64_t size;
    Platform_File_Type type;
    Platform_Link_Type link_type;
    int64_t created_epoch_time;
    int64_t last_write_epoch_time;  
    int64_t last_access_epoch_time; //The last time file was either read or written
} Platform_File_Info;

Platform_Error platform_file_open(Platform_File* file, Platform_String path, int open_flags); //Opens the file in the specified combination of Platform_File_Open_Flags. 
bool           platform_file_is_open(const Platform_File* file);
Platform_Error platform_file_size(const Platform_File* file, isize* size); //obtains the size of already open file. Can be used to do seeking to end etc.
Platform_Error platform_file_close(Platform_File* file); //Closes already opened file. If file was not successfully opened does nothing. Return value can be ignored
//Reads size bytes into the provided buffer. Sets read_bytes_because_eof to the number of bytes actually read.
//Does nothing when file is not open/invalid state. Only performs partial reads when eof is encountered. 
//Specifically this means: (*read_bytes_because_eof != size) iff (end of file reached)
Platform_Error platform_file_read(Platform_File* file, void* buffer, isize size, isize offset, isize* read_bytes_because_eof);
//Writes size bytes from the provided buffer, extending the file if necessary
//Does nothing when file is not open/invalid state. Does not perform partial writes (the write either fails or succeeds nothing in between).
Platform_Error platform_file_write(Platform_File* file, const void* buffer, isize size, isize offset); //if offset is INT64_MAX writes at end
Platform_Error platform_file_flush(Platform_File* file);

//The fastest way to read/write/append a file. 
//@NOTE: Maybe in the future we will want some mechanism to read as fast as possible a collection of files in async way. 
//This could be useful for games with loose files
Platform_Error platform_file_read_entire(Platform_String file_path, void* buffer, isize buffer_size);
Platform_Error platform_file_write_entire(Platform_String file_path, const void* buffer, isize buffer_size, bool fail_if_not_found);
Platform_Error platform_file_append_entire(Platform_String file_path, const void* buffer, isize buffer_size, bool fail_if_not_found);

Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null);
Platform_Error platform_file_create(Platform_String file_path, bool fail_if_already_existing);
Platform_Error platform_file_remove(Platform_String file_path, bool fail_if_not_found);
Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path, bool replace_existing);
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path, bool replace_existing);
Platform_Error platform_file_resize(Platform_String file_path, isize size); //Sets the size of the file to given size. On extending the value of added bytes are undefined (though most often 0)

//=========================================
// Directories
//=========================================
typedef struct Platform_Directory_Iter {
    void* internal;
    isize index;
    Platform_String path;
} Platform_Directory_Iter;

Platform_Error platform_directory_iter_init(Platform_Directory_Iter* iter, Platform_String directory_path);
bool platform_directory_iter_next(Platform_Directory_Iter* iter);
void platform_directory_iter_deinit(Platform_Directory_Iter* iter);

Platform_Error platform_directory_create(Platform_String dir_path, bool fail_if_already_existing);
Platform_Error platform_directory_remove(Platform_String dir_path, bool fail_if_not_found);

Platform_Error platform_directory_set_current_working(Platform_String new_working_dir);    
Platform_Error platform_directory_get_current_working(void* buffer, isize buffer_size, bool* needs_bigger_buffer_or_null); //Retrieves the absolute path of current working directory
const char* platform_directory_get_startup_working(); //Retrieves the absolute path of current working directory at the time of platform_init
const char* platform_get_executable_path(); //Retrieves the absolute path of the executable / dll

//=========================================
// File Watch
//=========================================
typedef struct Platform_File_Watch {
    void* handle;
} Platform_File_Watch;

typedef enum Platform_File_Watch_Flag {
    PLATFORM_FILE_WATCH_CREATED      = 1,
    PLATFORM_FILE_WATCH_DELETED      = 2,
    PLATFORM_FILE_WATCH_MODIFIED     = 4,
    PLATFORM_FILE_WATCH_RENAMED      = 8,
    PLATFORM_FILE_WATCH_DIRECTORY    = 16,
    PLATFORM_FILE_WATCH_SUBDIRECTORIES = 32,
    PLATFORM_FILE_WATCH_OVERFLOW = 64, //internal OS specific buffer overflown

    PLATFORM_FILE_WATCH_ALL_FILES = 0
        | PLATFORM_FILE_WATCH_CREATED
        | PLATFORM_FILE_WATCH_DELETED 
        | PLATFORM_FILE_WATCH_MODIFIED 
        | PLATFORM_FILE_WATCH_RENAMED,

    PLATFORM_FILE_WATCH_ALL = 0
        | PLATFORM_FILE_WATCH_ALL_FILES
        | PLATFORM_FILE_WATCH_DIRECTORY,
} Platform_File_Watch_Flag;
 
typedef struct Platform_File_Watch_Event {
    int32_t _;
    Platform_File_Watch_Flag action;
    Platform_String watched_path;
    Platform_String path;
    Platform_String new_path;
} Platform_File_Watch_Event;

Platform_Error platform_file_watch_init(Platform_File_Watch* file_watch, int32_t flags, Platform_String path, isize buffer_size);
void           platform_file_watch_deinit(Platform_File_Watch* file_watch);
bool           platform_file_watch_poll(Platform_File_Watch* file_watch, Platform_File_Watch_Event* event, Platform_Error* error_or_null);

//=========================================
// DLL management
//=========================================
typedef struct Platform_DLL {
    void* handle;
} Platform_DLL;

Platform_Error platform_dll_load(Platform_DLL* dll, Platform_String path);
void platform_dll_unload(Platform_DLL* dll);
void* platform_dll_get_function(Platform_DLL* dll, Platform_String name);

//=========================================
// Debug
//=========================================
//Could be separate file or project from here on...

//checks whether the debugger is attached. Returns 0 if not 1 if yes, -1 if error
int platform_is_debugger_attached();

typedef struct {
    char function[256]; //mangled or unmangled function name
    char module[256];   //mangled or unmangled module name ie. name of dll/executable
    char file[256];     //file or empty if not supported
    int64_t line;       //0 if not supported
    void* address;
} Platform_Stack_Trace_Entry;

//Captures the current stack frame pointers. 
//Saves up to stack_size pointers into the stack array and returns the number of
//stack frames captures. If the returned number is exactly stack_size a bigger buffer MIGHT be required.
//Skips first skip_count stack pointers from the position of the called. 
//(even with skip_count = 0 platform_capture_call_stack() will not be included within the stack)
int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count);

//Translates captured stack into helpful entries. Operates on fixed width strings to guarantee this function
//will never fail yet translate all needed stack frames. 
void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, void** stack, int64_t stack_size);

typedef struct Platform_Sandbox_Error {
    const char* exception;
    int32_t call_stack_size; 
    void** call_stack; 
    int64_t epoch_time;
} Platform_Sandbox_Error;

void platform_sandbox_error_deinit(Platform_Sandbox_Error* error);

//Launches the sandboxed_func inside a sandbox protecting the outside environment 
// from any exceptions, including hardware exceptions that might occur inside sandboxed_func.
//If an exception occurs collects execution context including stack pointers and saves it into
// error_or_null if not null. After an error occured should call platform_sandbox_error_deinit to
// release the error memory
//Returns true if no error occurred, false if some error occurred.
bool platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context, Platform_Sandbox_Error* error_or_null);

#if !defined(PLATFORM_OS) || PLATFORM_OS == PLATFORM_OS_UNKNOWN
    #undef PLATFORM_OS
    #if defined(_WIN32) || defined(_WIN64)
        #define PLATFORM_OS PLATFORM_OS_WINDOWS // Windows
    #elif defined(__ANDROID__)
        #define PLATFORM_OS PLATFORM_OS_ANDROID // Android (implies Linux, so it must come first)
    #elif defined(__linux__)
        #define PLATFORM_OS PLATFORM_OS_UNIX // Debian, Ubuntu, Gentoo, Fedora, openSUSE, RedHat, Centos and other
    #elif defined(__unix__) || !defined(__APPLE__) && defined(__MACH__)
        #include <sys/param.h>
        #if defined(BSD)
            #define PLATFORM_OS PLATFORM_OS_BSD // FreeBSD, NetBSD, OpenBSD, DragonFly BSD
        #endif
    #elif defined(__hpux)
        #define PLATFORM_OS PLATFORM_OS_HP_UX // HP-UX
    #elif defined(_AIX)
        #define PLATFORM_OS PLATFORM_OS_IBM_AIX // IBM AIX
    #elif defined(__APPLE__) && defined(__MACH__) // Apple OSX and iOS (Darwin)
        #include <TargetConditionals.h>
        #if TARGET_IPHONE_SIMULATOR == 1
            #define PLATFORM_OS PLATFORM_OS_APPLE_IOS // Apple iOS
        #elif TARGET_OS_IPHONE == 1
            #define PLATFORM_OS PLATFORM_OS_APPLE_IOS // Apple iOS
        #elif TARGET_OS_MAC == 1
            #define PLATFORM_OS PLATFORM_OS_APPLE_OSX // Apple OSX
        #endif
    #elif defined(__sun) && defined(__SVR4)
        #define PLATFORM_OS PLATFORM_OS_SOLARIS // Oracle Solaris, Open Indiana
    #else
        #define PLATFORM_OS PLATFORM_OS_UNKNOWN
    #endif
#endif 


#if !defined(PLATFORM_COMPILER) || PLATFORM_COMPILER == PLATFORM_COMPILER_UNKNOWN
    #undef PLATFORM_COMPILER
    
    #if defined(_MSC_VER)
        #define PLATFORM_COMPILER PLATFORM_COMPILER_MSVC
    #elif defined(__GNUC__)
        #define PLATFORM_COMPILER PLATFORM_COMPILER_GCC
    #elif defined(__clang__)
        #define PLATFORM_COMPILER PLATFORM_COMPILER_CLANG
    #elif defined(__MINGW32__)
        #define PLATFORM_COMPILER PLATFORM_COMPILER_MINGW
    #elif defined(__CUDACC__)
        #define PLATFORM_COMPILER PLATFORM_COMPILER_NVCC
    #elif defined(__CUDA_ARCH__)
        #define PLATFORM_COMPILER PLATFORM_COMPILER_NVCC_DEVICE
    #else
        #define PLATFORM_COMPILER PLATFORM_COMPILER_UNKNOWN
    #endif
#endif

#ifdef __cplusplus
    #include <atomic>
    #define PLATFORM_USE_ATOMICS using namespace std
    #define PLATFORM_ATOMIC(T) std::atomic<T>
#else
    #include <stdatomic.h>
    #define PLATFORM_USE_ATOMICS 
    #define PLATFORM_ATOMIC(T) _Atomic(T) 
#endif

static bool platform_once_begin(volatile uint32_t* once)
{
    enum {
        NOT_INIT = 0,
        INITIALIZING = 1,
        INIT = 2,
    };

    PLATFORM_ATOMIC(uint32_t)* atomic_once = (PLATFORM_ATOMIC(uint32_t)*) (void*) once; 
    bool out = false;
    for(;;) {
        uint32_t current = atomic_load(atomic_once);
        if(current == INIT)
            break;
        
        if(current == NOT_INIT) {
            if(atomic_compare_exchange_strong(atomic_once, &current, INITIALIZING)) {
                out = true;
                break;
            }
        }
        
        platform_futex_wait((void*) atomic_once, current, -1);
    }
    return out;
}
static void platform_once_end(volatile uint32_t* once)
{
    atomic_store((PLATFORM_ATOMIC(uint32_t)*) (void*) once, 2);
    platform_futex_wake_all(once);
}

#endif
