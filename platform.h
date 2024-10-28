#ifndef JOT_PLATFORM
#define JOT_PLATFORM

#include <stdint.h>
#include <limits.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

//This is a complete operating system abstraction layer. Its implementation is as straight forward and light as possible.
//It uses sized strings on all inputs and returns null terminated strings for maximum compatibility and performance.
//It tries to minimize the need to track state user side instead tries to operate on fixed amount of mutable buffers.

//Why we need this:
//  1) Practical
//      The c standard library is extremely minimalistic so if we wish to list all files in a directory there is no other way.
// 
//  2) Ideological
//     Its necessary to understand the bedrock of any medium we are working with. Be it paper, oil & canvas or code, 
//     understanding the medium will help us define strong limitations on the final problem solutions. This is possible
//     and this isnt. Yes or no. This drastically shrinks the design space of any problem which allow for deeper exploration of it. 
//     
//     Interestingly it does not only shrink the design space it also makes it more defined. We see more opportunities that we 
//     wouldnt have seen if we just looked at some high level abstraction library. This can lead to development of better abstractions.
//  
//     Further having absolute control over the system is rewarding. Having the knowledge of every single operation that goes on is
//     immensely satisfying.

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

#ifndef PLATFORM_MAX_ALIGN
    //Maximum alignment of builtin data type.
    //If this is incorrect (either too much or too little) please correct it by defining it!
    #define PLATFORM_MAX_ALIGN 8
#endif

#ifndef PLATFORM_SIMD_ALIGN
    #define PLATFORM_SIMD_ALIGN 32
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

//Initializes the platform layer interface. 
//Should be called before calling any other function.
void platform_init();

//Deinitializes the platform layer, freeing all allocated resources back to os.
//platform_init() should be called before using any other function again!
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

Platform_Error platform_virtual_reallocate(void** output_adress_or_null, void* address, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection);
int64_t platform_page_size();
int64_t platform_allocation_granularity();

void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t align);
//Returns the size in bytes of an allocated block. 
//old_ptr needs to be value returned from platform_heap_reallocate. Align must be the one supplied to platform_heap_reallocate.
//If old_ptr is NULL returns 0.
int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align); 

//=========================================
// Threading
//=========================================

typedef int (*Platform_Thread_Func)(void* context);

typedef struct Platform_Thread {
    void* handle;
} Platform_Thread;

//A handle to fast (ie non kernel code) recursive mutex. (pthread_mutex_t on linux, CRITICAL_SECTION win32)
typedef struct Platform_Mutex {
    void* handle;
} Platform_Mutex;

//@TODO: thread processor affinity!

//@NOTE: We made pretty much all of the threaded functions (except init-like) into non failing
//       even though they CAN internally return error (we just assert). That is because:
// 1) One can generally do very little when a mutex fails.
// 2) All* error return values are due to a programmer mistake
// 3) All error values require no further action 
//   (ie if it failed then it failed and I cannot do anything about it apart from not calling this function)
// 4) On win32 these functions never fail.
//
// *pthread_mutex_lock has a fail state on too many recursive locks and insufficient privileges which are
// not programmer mistake. However in practice they will not happened and if they do we are doing something
// very specific and a custom implementation is preferred (or we can just change this).

//initializes a new thread and immediately starts it with the func function.
//Allocates and copies over context_size bytes from context (thus allowing to pass arbitrary large structures to the thread).
//The thread has stack_size_or_zero bytes of stack sizes rounded up to page size
//If stack_size_or_zero is zero or lower uses system default stack size.
//The thread automatically cleans itself up upon completion or termination.
Platform_Error platform_thread_launch(Platform_Thread* thread, int64_t stack_size_or_zero, int (*func)(void*), const void* context, int64_t context_size);

int64_t         platform_thread_get_proccessor_count();
Platform_Thread platform_thread_get_current(); //Returns handle to the calling thread
int32_t         platform_thread_get_current_id(); 
Platform_Thread platform_thread_get_main(); //Returns the handle to the thread which called platform_init(). If platform_init() was not called returns NULL.
bool            platform_thread_is_main();
void            platform_thread_sleep(int64_t ms); //Sleeps the calling thread for ms milliseconds
void            platform_thread_exit(int code); //Terminates a thread with an exit code
void            platform_thread_yield(); //Yields the remainder of this thread's time slice to the OS
void            platform_thread_detach(Platform_Thread* thread);
bool            platform_thread_join(const Platform_Thread* threads, int64_t count, int64_t ms_or_negative_if_infinite); //Blocks calling thread until all threads finish. Must not join the current calling thread!
int             platform_thread_get_exit_code(Platform_Thread finished_thread); //Returns the exit code of a terminated thread. If the thread is not terminated the result is undefined. 
bool            platform_thread_is_running(Platform_Thread thread, Platform_Error* error_or_null);
void            platform_thread_attach_deinit(void (*func)(void* context), void* context); //Registers a function to be called when the thread terminates


Platform_Error  platform_mutex_init(Platform_Mutex* mutex);
void            platform_mutex_deinit(Platform_Mutex* mutex);
void            platform_mutex_lock(Platform_Mutex* mutex);
void            platform_mutex_unlock(Platform_Mutex* mutex);
bool            platform_mutex_try_lock(Platform_Mutex* mutex); //Tries to lock a mutex. Returns true if mutex was locked successfully. If it was not returns false without waiting.

bool            platform_futex_wait(volatile void* futex, uint32_t value, int64_t ms_or_negative_if_infinite);
void            platform_futex_wake(volatile void* futex);
void            platform_futex_wake_all(volatile void* futex);

//calls the given func with context argument just once, even if racing with other threads.
//state should point to shared variable between racing threads (ie. global) initialized to 0.
//This function will set it to 1 while initilization is in progress and finally 2 once initialized.
//After initialization is complete this function costs just one load and thus is extremely cheap.
static void     platform_call_once(uint32_t* state, void (*func)(void* context), void* context);

//=========================================
// Atomics 
//=========================================
PLATFORM_INTRINSIC void platform_compiler_barrier();
PLATFORM_INTRINSIC void platform_memory_barrier();
PLATFORM_INTRINSIC void platform_processor_pause();

//Returns the first/last set (1) bit position. If num is zero result is undefined.
//The following invariants hold (analogous for 64 bit)
// (num & (1 << platform_find_first_set_bit32(num)) != 0
// (num & (1 << (32 - platform_find_last_set_bit32(num))) != 0
PLATFORM_INTRINSIC int32_t platform_find_first_set_bit32(uint32_t num);
PLATFORM_INTRINSIC int32_t platform_find_first_set_bit64(uint64_t num);
PLATFORM_INTRINSIC int32_t platform_find_last_set_bit32(uint32_t num); 
PLATFORM_INTRINSIC int32_t platform_find_last_set_bit64(uint64_t num);

//Returns the number of set (1) bits 
PLATFORM_INTRINSIC int32_t platform_pop_count32(uint32_t num);
PLATFORM_INTRINSIC int32_t platform_pop_count64(uint64_t num);

//Standard Compare and Set (CAS) semantics.
//Performs atomically: {
//   if(*target != old_value)
//      return false;
// 
//   *target = new_value;
//   return true;
// }
PLATFORM_INTRINSIC bool platform_atomic_cas128(volatile void* target, uint64_t old_value_lo, uint64_t old_value_hi, uint64_t new_value_lo, uint64_t new_value_hi);
PLATFORM_INTRINSIC bool platform_atomic_cas64(volatile void* target, uint64_t old_value, uint64_t new_value);
PLATFORM_INTRINSIC bool platform_atomic_cas32(volatile void* target, uint32_t old_value, uint32_t new_value);

PLATFORM_INTRINSIC bool platform_atomic_cas_weak128(volatile void* target, uint64_t old_value_lo, uint64_t old_value_hi, uint64_t new_value_lo, uint64_t new_value_hi);
PLATFORM_INTRINSIC bool platform_atomic_cas_weak64(volatile void* target, uint64_t old_value, uint64_t new_value);
PLATFORM_INTRINSIC bool platform_atomic_cas_weak32(volatile void* target, uint32_t old_value, uint32_t new_value);

//Performs atomically: { return *target; }
PLATFORM_INTRINSIC uint64_t platform_atomic_load64(const volatile void* target);
PLATFORM_INTRINSIC uint32_t platform_atomic_load32(const volatile void* target);

//Performs atomically: { *target = value; }
PLATFORM_INTRINSIC void platform_atomic_store64(volatile void* target, uint64_t value);
PLATFORM_INTRINSIC void platform_atomic_store32(volatile void* target, uint32_t value);

//Performs atomically: { uint64_t copy = *target; *target = value; return copy; }
PLATFORM_INTRINSIC uint64_t platform_atomic_exchange64(volatile void* target, uint64_t value);
PLATFORM_INTRINSIC uint32_t platform_atomic_exchange32(volatile void* target, uint32_t value);

//Performs atomically: { int64_t copy = *target; *target += value; return copy; }
PLATFORM_INTRINSIC uint32_t platform_atomic_add32(volatile void* target, uint32_t value);
PLATFORM_INTRINSIC uint64_t platform_atomic_add64(volatile void* target, uint64_t value);

//Performs atomically: { uint64_t copy = *target; *target -= value; return copy; }
PLATFORM_INTRINSIC uint32_t platform_atomic_sub32(volatile void* target, uint32_t value);
PLATFORM_INTRINSIC uint64_t platform_atomic_sub64(volatile void* target, uint64_t value);


PLATFORM_INTRINSIC uint64_t platform_atomic_or64(volatile void* target, uint64_t value);
PLATFORM_INTRINSIC uint64_t platform_atomic_and64(volatile void* target, uint64_t value);

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

//Functions which deal with __rdtsc or equivalent on ARM
PLATFORM_INTRINSIC int64_t platform_rdtsc();
PLATFORM_INTRINSIC int64_t platform_rdtsc_fence();
int64_t platform_rdtsc_frequency();
int64_t platform_rdtsc_startup();

//=========================================
// Filesystem
//=========================================
typedef struct Platform_String {
    const char* data;
    int64_t len;
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

#ifdef linux
    #undef linux //Genuinely guys wtf 
#endif

typedef struct Platform_File {
    union {
        void* windows;
        int linux;
    } handle;
    bool is_open;
    bool _[7];
} Platform_File;

typedef enum Platform_File_Open_Flags {
    PLATFORM_FILE_MODE_READ = 1,                    //Read privilege
    PLATFORM_FILE_MODE_WRITE = 2,                   //Write privilege
    PLATFORM_FILE_MODE_APPEND = 4,                  //Append privileges (no effect on linux). 
                                                    //Has no effect on the file write position unlike fopen(path, "a"). 
                                                    //If you wish to start at the end of a file use platform_file_seek(file, 0, PLATFORM_FILE_SEEK_FROM_END)

    PLATFORM_FILE_MODE_CREATE = 8,                  //Creates the file, if it already exists does nothing.
    PLATFORM_FILE_MODE_CREATE_MUST_NOT_EXIST = 16,  //Creates the file, if it already exists fails. When supplied alongside PLATFORM_FILE_MODE_CREATE overrides it.
    PLATFORM_FILE_MODE_REMOVE_CONTENT = 32,         //If opening a file that has content, remove it.
    PLATFORM_FILE_MODE_READ_WRITE_APPEND = PLATFORM_FILE_MODE_READ | PLATFORM_FILE_MODE_WRITE | PLATFORM_FILE_MODE_APPEND,
} Platform_File_Open_Flags;

typedef enum Platform_File_Seek {
    PLATFORM_FILE_SEEK_FROM_START = 0,
    PLATFORM_FILE_SEEK_FROM_CURRENT = 1,
    PLATFORM_FILE_SEEK_FROM_END = 2,
} Platform_File_Seek;

//Opens the file in the specified combination of Platform_File_Open_Flags. 
Platform_Error platform_file_open(Platform_File* file, Platform_String path, int open_flags);
//Closes already opened file. If file was not open does nothing.
Platform_Error platform_file_close(Platform_File* file);
//Reads size bytes into the provided buffer. Sets read_bytes_because_eof to the number of bytes actually read.
//Does nothing when file is not open/invalid state. Only performs partial reads when eof is encountered. 
//Specifcally this means: (*read_bytes_because_eof != size) <=> end of file reached
Platform_Error platform_file_read(Platform_File* file, void* buffer, int64_t size, int64_t* read_bytes_because_eof);
//Writes size bytes from the provided buffer, extending the file if necessary
//Does nothing when file is not open/invalid state. Does not perform partial writes (the write either fails or succeeds nothing in between).
Platform_Error platform_file_write(Platform_File* file, const void* buffer, int64_t size);
//Obtains the current offset from the start of the file and saves it into offset. Does not modify the file 
Platform_Error platform_file_tell(Platform_File file, int64_t* offset);
//Offset the current file position relative to: start of the file (0 value), current position, end of the file
Platform_Error platform_file_seek(Platform_File* file, int64_t offset, Platform_File_Seek from);
//Flushes all cached contents of the file to disk.
Platform_Error platform_file_flush(Platform_File* file);

//retrieves info about the specified file or directory
Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null);
//Creates an empty file at the specified path. Succeeds if the file exists after the call.
//Saves to was_just_created_or_null whether the file was just now created. If is null doesnt save anything.
Platform_Error platform_file_create(Platform_String file_path, bool fail_if_already_existing);
//Removes a file at the specified path. If fail_if_not_found is true succeeds only if the file was removed.
// else succeeds if the file does not exists after the call.
Platform_Error platform_file_remove(Platform_String file_path, bool fail_if_not_found);
//Moves or renames a file. If the file cannot be found or renamed to file that already exists, fails.
Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path, bool replace_existing);
//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path, bool replace_existing);
//Sets the size of the file to given size. On extending the value of added bytes are undefined (though most often 0)
Platform_Error platform_file_resize(Platform_String file_path, int64_t size);

//Makes an empty directory
//Saves to was_just_created_or_null whether the file was just now created. If is null doesnt save anything.
Platform_Error platform_directory_create(Platform_String dir_path, bool fail_if_already_existing);
//Removes an empty directory
//Saves to was_just_deleted_or_null whether the file was just now deleted. If is null doesnt save anything.
Platform_Error platform_directory_remove(Platform_String dir_path, bool fail_if_not_found);

//changes the current working directory to the new_working_dir.  
Platform_Error platform_directory_set_current_working(Platform_String new_working_dir);    
//Retrieves the absolute path current working directory
Platform_Error platform_directory_get_current_working(void* buffer, int64_t buffer_size, bool* needs_bigger_buffer_or_null);
//Retrieves the absolute path working directory at the time of platform_init
const char* platform_directory_get_startup_working();


//Retrieves the absolute path of the executable / dll
const char* platform_get_executable_path();    

//Gathers and allocates list of files in the specified directory. Saves a pointer to array of entries to entries and its size to entries_count. 
//Needs to be freed using directory_list_contents_free(). If max_depth == -1 max depth is unlimited
Platform_Error platform_directory_list_contents_alloc(Platform_String directory_path, Platform_Directory_Entry** entries, int64_t* entries_count, int64_t max_depth);
//Frees previously allocated file list
void platform_directory_list_contents_free(Platform_Directory_Entry* entries);

//Memory maps the file pointed to by file_path and saves the address and size of the mapped block into mapping. 
//If the desired_size_or_zero == 0 maps the entire file. 
//  if the file doesnt exist the function fails.
//If the desired_size_or_zero > 0 maps only up to desired_size_or_zero bytes from the file.
//  The file is resized so that it is exactly desired_size_or_zero bytes (filling empty space with 0)
//  if the file doesnt exist the function creates a new file.
//If the desired_size_or_zero < 0 maps additional desired_size_or_zero bytes from the file 
//    (for appending) extending it by that amount and filling the space with 0.
//  if the file doesnt exist the function creates a new file.
Platform_Error platform_file_memory_map(Platform_String file_path, int64_t desired_size_or_zero, Platform_Memory_Mapping* mapping);
//Unmpas the previously mapped file. If mapping is a result of failed platform_file_memory_map does nothing.
void platform_file_memory_unmap(Platform_Memory_Mapping* mapping);

//=========================================
// File Watch
//=========================================
typedef enum Platform_File_Watch_Flag {
    PLATFORM_FILE_WATCH_CREATED      = 1,
    PLATFORM_FILE_WATCH_DELETED      = 2,
    PLATFORM_FILE_WATCH_MODIFIED     = 4,
    PLATFORM_FILE_WATCH_RENAMED      = 8,
    PLATFORM_FILE_WATCH_DIRECTORY    = 16,
    PLATFORM_FILE_WATCH_SUBDIRECTORIES = 32,

    PLATFORM_FILE_WATCH_ALL_FILES = PLATFORM_FILE_WATCH_CREATED 
        | PLATFORM_FILE_WATCH_DELETED 
        | PLATFORM_FILE_WATCH_MODIFIED 
        | PLATFORM_FILE_WATCH_RENAMED,

    PLATFORM_FILE_WATCH_ALL = PLATFORM_FILE_WATCH_ALL_FILES
        | PLATFORM_FILE_WATCH_DIRECTORY,
} Platform_File_Watch_Flag;
 
typedef struct Platform_File_Watch {
    void* handle;
} Platform_File_Watch;

typedef struct Platform_File_Watch_Event {
    Platform_File_Watch_Flag action;
    int32_t _;
    const char* watched_path;
    const char* path;
    const char* old_path; //only used in case of PLATFORM_FILE_WATCH_RENAMED to store the previous path.
} Platform_File_Watch_Event;

//Creates a watch of changes on a directory or a single file. These changes can be polled with platform_file_watch_poll. 
//Returns Platform_Error indicating whether the operation was successful. 
//file_watch_flags is a bitwise combination of members of Platform_File_Watch_Flag specifying which events we to be reported.
// 
//If file file_watch is not null, saves this watch to the given pointer.
//If signal_func_or_null is not null, calls it immediately after a change has occurred. This can be used to give some more general signals about which watches have events.
//
//Note that the directory in which the watched file resides (or the watched directory itself) must exist else returns error.
Platform_Error platform_file_watch(Platform_File_Watch* file_watch, Platform_String file_path, int32_t file_watch_flags, void (*signal_func_or_null)(Platform_File_Watch watch, void* context), void* context);
//Deinits a given watch represented by file_watch_or_null. 
Platform_Error platform_file_unwatch(Platform_File_Watch* file_watch);
//Polls watch events from the given file watch. 
//Returns true if event was polled and false if there are no events in the queue.
//Note that once event is polled it is removed from the queue.
//Note that this functions is implemented efficiently and in case of no events is practically free.
bool platform_file_watch_poll(Platform_File_Watch file_watch, Platform_File_Watch_Event* event);
//Returns the watched path from give filewatch. Also saves the used flags if to flags_or_null if not null.
const char* platform_file_watch_get_info(Platform_File_Watch file_watch, int32_t* flags_or_null);

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
// Window managmenet
//=========================================

typedef enum Platform_Window_Popup_Style {
    PLATFORM_POPUP_STYLE_OK = 0,
    PLATFORM_POPUP_STYLE_ERROR,
    PLATFORM_POPUP_STYLE_WARNING,
    PLATFORM_POPUP_STYLE_INFO,
    PLATFORM_POPUP_STYLE_RETRY_ABORT,
    PLATFORM_POPUP_STYLE_YES_NO,
    PLATFORM_POPUP_STYLE_YES_NO_CANCEL,
} Platform_Window_Popup_Style;

typedef enum Platform_Window_Popup_Controls {
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
Platform_Window_Popup_Controls platform_window_make_popup(Platform_Window_Popup_Style desired_style, Platform_String message, Platform_String title);

//=========================================
// Debug
//=========================================
//Could be separate file or project from here on...

typedef struct {
    char function[256]; //mangled or unmangled function name
    char module[256];   //mangled or unmangled module name ie. name of dll/executable
    char file[256];     //file or empty if not supported
    int64_t line;       //0 if not supported;
    void* address;
} Platform_Stack_Trace_Entry;

//Stops the debugger at the call site
#define platform_debug_break() (*(char*)0 = 0)
//Marks a piece of code as unreachable for the compiler
#define platform_assume_unreachable() (*(char*)0 = 0)

//Captures the current stack frame pointers. 
//Saves up to stack_size pointers into the stack array and returns the number of
//stack frames captures. If the returned number is exactly stack_size a bigger buffer MIGHT be required.
//Skips first skip_count stack pointers from the position of the called. 
//(even with skip_count = 0 platform_capture_call_stack() will not be included within the stack)
int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count);

//Translates captured stack into helpful entries. Operates on fixed width strings to guarantee this function
//will never fail yet translate all needed stack frames. 
void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, const void* const* stack, int64_t stack_size);

typedef enum Platform_Exception {
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
    PLATFORM_EXCEPTION_STACK_OVERFLOW,
    PLATFORM_EXCEPTION_ABORT,
    PLATFORM_EXCEPTION_TERMINATE = 0x0001000,
    PLATFORM_EXCEPTION_OTHER = 0x0001001,
} Platform_Exception; 

typedef struct Platform_Sandbox_Error {
    //The exception that occurred
    Platform_Exception exception;
    
    //A translated stack trace and its size
    int32_t call_stack_size;
    void** call_stack; 

    //Platform specific data containing the cpu state and its size (so that it can be copied and saved)
    const void* execution_context;
    int64_t execution_context_size;

    //The epoch time of the exception
    int64_t epoch_time;
} Platform_Sandbox_Error;

//Launches the sandboxed_func inside a sandbox protecting the outside environment 
// from any exceptions, including hardware exceptions that might occur inside sandboxed_func.
//If an exception occurs collects execution context including stack pointers and calls
// 'error_func_or_null' if not null. Gracefully recovers from all errors. 
//Returns the error that occurred or PLATFORM_EXCEPTION_NONE = 0 on success.
Platform_Exception platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context,
    void (*error_func_or_null)(void* error_context, Platform_Sandbox_Error error),
    void* error_context);

//Converts the sandbox error to string. The string value is the name of the enum
// (PLATFORM_EXCEPTION_ACCESS_VIOLATION -> "PLATFORM_EXCEPTION_ACCESS_VIOLATION")
const char* platform_exception_to_string(Platform_Exception error);

#if !defined(PLATFORM_OS) || PLATFORM_OS == PLATFORM_OS_UNKNOWN
    #undef PLATFORM_OS
    #if defined(_WIN32)
        #define PLATFORM_OS PLATFORM_OS_WINDOWS // Windows
    #elif defined(_WIN64)
        #define PLATFORM_OS PLATFORM_OS_WINDOWS // Windows
    #elif defined(__CYGWIN__) && !defined(_WIN32)
        #define PLATFORM_OS PLATFORM_OS_WINDOWS // Windows (Cygwin POSIX under Microsoft Window)
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



// =================== INLINE IMPLEMENTATION ============================
#if defined(_MSC_VER)
    #include <stdio.h>
    #include <intrin.h>
    #include <assert.h>

    #pragma warning(push)
    #pragma warning(disable:4996) //disables deprecated/unsafe function warning ( _ReadWriteBarrier() )

    #if defined(_M_CEE_PURE) || defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC))
        #define _PLATFORM_MSVC_X86
    #elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC)
        #define _PLATFORM_MSVC_ARM
    #else
        #error Unsupported hardware!
    #endif

    #undef platform_debug_break
    #define platform_debug_break() __debugbreak() 

    PLATFORM_INTRINSIC void platform_compiler_barrier() 
    {
        _ReadWriteBarrier();
    }

    PLATFORM_INTRINSIC void platform_memory_barrier()
    {
        _ReadWriteBarrier(); 
        
        //I dont think this is needed. 
        //At least thats what MSVC std lib does.
        //__faststorebarrier();
        #ifdef _PLATFORM_MSVC_X86
            //nothing...
        #else
            //taken from xxtomic.h
            __dmb(0xB);
        #endif
    }

    PLATFORM_INTRINSIC void platform_processor_pause()
    {
        #ifdef _PLATFORM_MSVC_X86
            _mm_pause();
        #else
            __yield();
        #endif
    }
    
    PLATFORM_INTRINSIC int32_t platform_find_last_set_bit32(uint32_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanReverse(&out, (unsigned long) num);
        return (int32_t) out;
    }
    
    PLATFORM_INTRINSIC int32_t platform_find_last_set_bit64(uint64_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanReverse64(&out, (unsigned long long) num);
        return (int32_t) out;
    }

    PLATFORM_INTRINSIC int32_t platform_find_first_set_bit32(uint32_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanForward(&out, (unsigned long) num);
        return (int32_t) out;
    }
    PLATFORM_INTRINSIC int32_t platform_find_first_set_bit64(uint64_t num)
    {
        assert(num != 0);
        unsigned long out = 0;
        _BitScanForward64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
    
    PLATFORM_INTRINSIC int32_t platform_pop_count32(uint32_t num)
    {
        return (int32_t) __popcnt((unsigned int) num);
    }
    PLATFORM_INTRINSIC int32_t platform_pop_count64(uint64_t num)
    {
        return (int32_t) __popcnt64((unsigned long long)num);
    }
    
    //Load and store function are a bit weird under MSVC.
    //Since x86/64 has quite strong sequential consistency gurantees, 
    //MSVC for the longest time got away with using volatile for 
    // atomic load/store. However with the support for ARM they added 
    //were forced to add proper support. Now when acessing a volatile on arm they give warning
    // "warning C4746: volatile access of '<expression>' is subject to /volatile:<iso|ms> setting; consider using __iso_volatile_load/store intrinsic functions"
    //
    // Now even though __iso_volatile_load32 and similar are listed under ARM intrinsics they work just fine even on x86/64. 
    // All this to say that this is a kind of hacky solution but the best there is at the moment.
    PLATFORM_INTRINSIC uint64_t platform_atomic_load64(const volatile void* target)
    {
        return (uint64_t) __iso_volatile_load64((const volatile long long*) target);
    }
    PLATFORM_INTRINSIC uint32_t platform_atomic_load32(const volatile void* target)
    {
        return (uint32_t) __iso_volatile_load32((const volatile int*) target);
    }

    PLATFORM_INTRINSIC void platform_atomic_store64(volatile void* target, uint64_t value)
    {
        __iso_volatile_store64((volatile long long*) target, (long long) value);
    }
    PLATFORM_INTRINSIC void platform_atomic_store32(volatile void* target, uint32_t value)
    {
        __iso_volatile_store32((volatile int*) target, (int) value);
    }
    
    PLATFORM_INTRINSIC bool platform_atomic_cas128(volatile void* target, uint64_t old_value_lo, uint64_t old_value_hi, uint64_t new_value_lo, uint64_t new_value_hi)
    {
        long long new_val[] = {(long long) old_value_lo, (long long) old_value_hi};
        return (bool) _InterlockedCompareExchange128((volatile long long*) target, (long long) new_value_hi, (long long) new_value_lo, new_val);
    }
    PLATFORM_INTRINSIC bool platform_atomic_cas64(volatile void* target, uint64_t old_value, uint64_t new_value)
    {
        return _InterlockedCompareExchange64((volatile long long*) target, (long long) new_value, (long long) old_value) == (long long) old_value;
    }
    PLATFORM_INTRINSIC bool platform_atomic_cas32(volatile void* target, uint32_t old_value, uint32_t new_value)
    {
        return _InterlockedCompareExchange((volatile long*) target, (long) new_value, (long) old_value) == (long) old_value;
    }
    
    PLATFORM_INTRINSIC bool platform_atomic_cas_weak128(volatile void* target, uint64_t old_value_lo, uint64_t old_value_hi, uint64_t new_value_lo, uint64_t new_value_hi)
    {
        long long new_val[] = {(long long) old_value_lo, (long long) old_value_hi};
        return (bool) _InterlockedCompareExchange128((volatile long long*) target, (long long) new_value_hi, (long long) new_value_lo, new_val);
    }
    PLATFORM_INTRINSIC bool platform_atomic_cas_weak64(volatile void* target, uint64_t old_value, uint64_t new_value)
    {
        return _InterlockedCompareExchange64((volatile long long*) target, (long long) new_value, (long long) old_value) == (long long) old_value;
    }
    PLATFORM_INTRINSIC bool platform_atomic_cas_weak32(volatile void* target, uint32_t old_value, uint32_t new_value)
    {
        return _InterlockedCompareExchange((volatile long*) target, (long) new_value, (long) old_value) == (long) old_value;
    }

    PLATFORM_INTRINSIC uint64_t platform_atomic_exchange64(volatile void* target, uint64_t value)
    {
        return (uint64_t) _InterlockedExchange64((volatile long long*) target, (long long) value);
    }

    PLATFORM_INTRINSIC uint32_t platform_atomic_exchange32(volatile void* target, uint32_t value)
    {
        return (uint32_t) _InterlockedExchange((volatile long*) target, (long) value);
    }
    
    PLATFORM_INTRINSIC uint64_t platform_atomic_add64(volatile void* target, uint64_t value)
    {
        return (uint64_t) _InterlockedExchangeAdd64((volatile long long*) (void*) target, (long long) value);
    }

    PLATFORM_INTRINSIC uint32_t platform_atomic_add32(volatile void* target, uint32_t value)
    {
        return (uint32_t) _InterlockedExchangeAdd((volatile long*) (void*) target, (long) value);
    }

    PLATFORM_INTRINSIC uint64_t platform_atomic_sub64(volatile void* target, uint64_t value)
    {
        return platform_atomic_add64(target, (uint64_t) -(int64_t) value);
    }
   
    PLATFORM_INTRINSIC uint32_t platform_atomic_sub32(volatile void* target, uint32_t value)
    {
        return platform_atomic_add32(target, (uint32_t) -(int32_t) value);
    }
   
    PLATFORM_INTRINSIC uint64_t platform_atomic_or64(volatile void* target, uint64_t value)
    {
        return (uint64_t) _InterlockedOr64((volatile long long*) (void*) target, (long long) value);
    }
    PLATFORM_INTRINSIC uint32_t platform_atomic_or32(volatile void* target, uint32_t value)
    {
        return (uint32_t) _InterlockedOr((volatile long*) (void*) target, (long) value);
    }
    PLATFORM_INTRINSIC uint64_t platform_atomic_and64(volatile void* target, uint64_t value)
    {
        return (uint64_t) _InterlockedAnd64((volatile long long*) (void*) target, (long long) value);
    }
    PLATFORM_INTRINSIC uint32_t platform_atomic_and32(volatile void* target, uint32_t value)
    {
        return (uint32_t) _InterlockedAnd((volatile long*) (void*) target, (long) value);
    }

    PLATFORM_INTRINSIC int64_t platform_rdtsc()
    {
        _ReadWriteBarrier(); 
	    return (int64_t) __rdtsc();
    }

    PLATFORM_INTRINSIC int64_t platform_rdtsc_fence()
    {
        _ReadWriteBarrier(); 
        _mm_lfence();
    }

    #pragma warning(pop)

#elif defined(__GNUC__) || defined(__clang__)
    #define _GNU_SOURCE
    #include <signal.h>

    #undef platform_debug_break
    // #define platform_debug_break() __builtin_trap() /* bad looks like a fault in program! */
    #define platform_debug_break() raise(SIGTRAP)
    

    typedef char __MAX_ALIGN_TESTER__[
        __alignof__(long long int) == PLATFORM_MAX_ALIGN || 
        __alignof__(long double) == PLATFORM_MAX_ALIGN ? 1 : -1
    ];

    PLATFORM_INTRINSIC void platform_compiler_barrier() 
    {
        __asm__ __volatile__("":::"memory");
    }

    PLATFORM_INTRINSIC void platform_memory_barrier()
    {
        platform_compiler_barrier(); 
        __sync_synchronize();
    }

    #if defined(__x86_64__) || defined(__i386__)
        #include <immintrin.h> // For _mm_pause
        PLATFORM_INTRINSIC void platform_processor_pause()
        {
            _mm_pause();
        }
    #else
        #include <time.h>
        PLATFORM_INTRINSIC void platform_processor_pause()
        {
            struct timespec spec = {0};
            spec.tv_sec = 0;
            spec.tv_nsec = 1;
            nanosleep(spec, NULL);
        }
    #endif

    //for refernce see: https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
    PLATFORM_INTRINSIC int32_t platform_find_last_set_bit32(uint32_t num)
    {
        return 32 - __builtin_ctz((unsigned int) num) - 1;
    }
    PLATFORM_INTRINSIC int32_t platform_find_last_set_bit64(uint64_t num)
    {
        return 64 - __builtin_ctzll((unsigned long long) num) - 1;
    }

    PLATFORM_INTRINSIC int32_t platform_find_first_set_bit32(uint32_t num)
    {
        return __builtin_ffs((int) num) - 1;
    }
    PLATFORM_INTRINSIC int32_t platform_find_first_set_bit64(uint64_t num)
    {
        return __builtin_ffsll((long long) num) - 1;
    }

    PLATFORM_INTRINSIC int32_t platform_pop_count32(uint32_t num)
    {
        return __builtin_popcount((uint32_t) num);
    }
    PLATFORM_INTRINSIC int32_t platform_pop_count64(uint64_t num)
    {
        return __builtin_popcountll((uint64_t) num);
    }

    //for reference see: https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
    PLATFORM_INTRINSIC bool platform_atomic_cas64(volatile int64_t* target, int64_t old_value, int64_t new_value)
    {
        return __atomic_compare_exchange_n(target, &old_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }

    PLATFORM_INTRINSIC bool platform_atomic_cas32(volatile int32_t* target, int32_t old_value, int32_t new_value)
    {
        return __atomic_compare_exchange_n(target, &old_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }

    PLATFORM_INTRINSIC int64_t platform_atomic_load64(volatile const int64_t* target)
    {
        return (int64_t) __atomic_load_n(target, __ATOMIC_SEQ_CST);
    }
    PLATFORM_INTRINSIC int32_t platform_atomic_load32(volatile const int32_t* target)
    {
        return (int32_t) __atomic_load_n(target, __ATOMIC_SEQ_CST);
    }

    PLATFORM_INTRINSIC void platform_atomic_store64(volatile int64_t* target, int64_t value)
    {
        __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
    }
    PLATFORM_INTRINSIC void platform_atomic_store32(volatile int32_t* target, int32_t value)
    {
        __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
    }

    PLATFORM_INTRINSIC int64_t platform_atomic_exchange64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }
    PLATFORM_INTRINSIC int32_t platform_atomic_exchange32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }

    PLATFORM_INTRINSIC int32_t platform_atomic_add32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_add_fetch(target, value, __ATOMIC_SEQ_CST);
    }
    PLATFORM_INTRINSIC int64_t platform_atomic_add64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_add_fetch(target, value, __ATOMIC_SEQ_CST);
    }

    PLATFORM_INTRINSIC int32_t platform_atomic_sub32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_sub_fetch(target, value, __ATOMIC_SEQ_CST);
    }
    PLATFORM_INTRINSIC int64_t platform_atomic_sub64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_sub_fetch(target, value, __ATOMIC_SEQ_CST);
    }


#endif

static void platform_call_once(uint32_t* state, void (*func)(void* context), void* context)
{
    enum {
        NOT_INIT,
        INITIALIZING,
        INIT,
    };

    //These 3 lines could be removed and the impleemntation would stay correct.
    //However most of the time the init code will already be init
    uint32_t before_value = platform_atomic_load32(state);
    if(before_value == INIT)
        return;

    if(platform_atomic_cas32(state, NOT_INIT, INITIALIZING))
    {
        func(context);
        platform_atomic_store32(state, INIT);
        platform_futex_wake_all(state);
    }
    else
    {
        while(true)
        {
            uint32_t curr_value = platform_atomic_load32(state);
            if(curr_value == INIT)
                break;
            
            platform_futex_wait(state, INITIALIZING, -1);
        }
    }
}

#endif

// ====================================================================================
//                               UNIT TESTS 
// ====================================================================================

#if (defined(JOT_ALL_TEST) || defined(JOT_PLATFORM_TEST)) && !defined(JOT_PLATFORM_HAS_TEST)
#define JOT_PLATFORM_HAS_TEST

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
static bool _platform_test_report(Platform_Error error, bool is_error, const char* expression, const char* file, const char* funcion, int line, const char* format, ...);

#ifndef _MSC_VER
    #define __FUNCTION__ __func__
#endif

#ifdef __cplusplus
    #define PBRACE_INIT(Struct_Type) Struct_Type
#else
    #include <stdbool.h>
    #define PBRACE_INIT(Struct_Type) (Struct_Type)
#endif 

#define PTEST(x, ...)           (_platform_test_report(0, !(x), #x, __FILE__, __FUNCTION__, __LINE__, "" __VA_ARGS__) ? abort() : (void) 0)
#define PTEST_ERROR(error, ...) (_platform_test_report((error), 0, #error, __FILE__, __FUNCTION__, __LINE__, "" __VA_ARGS__) ? abort() : (void) 0)
#define PSTRING(literal)        PBRACE_INIT(Platform_String){"" literal, sizeof(literal) - 1}

//String containing few problematic sequences: BOM, non ascii, non single UTF16 representable chars, \r\n and \n newlines.
// It should still be read in and out exactly the same!
#define PUTF8_BOM        "\xEF\xBB\xBF"
#define PUGLY_STR        PUTF8_BOM "Hello world!\r\n ěščřžýáéň,\n Φφ,Χχ,Ψψ,Ωω,\r\n あいうえお"
#define PUGLY_STRING     PSTRING(PUGLY_STR)

static void platform_test_file_content_equality(Platform_String path, Platform_String content);

static void platform_test_file_io() 
{
    #define TEST_DIR          "__platform_file_test_directory__"
    PTEST_ERROR(platform_directory_create(PSTRING(TEST_DIR), false));
    PTEST(platform_directory_create(PSTRING(TEST_DIR), true) != 0, "Creating already created directory should fail when fail_if_already_exists = true\n");
    {
        Platform_File_Info dir_info = {0};
        PTEST_ERROR(platform_file_info(PSTRING(TEST_DIR), &dir_info));
        PTEST(dir_info.type == PLATFORM_FILE_TYPE_DIRECTORY);
        PTEST(dir_info.link_type == PLATFORM_LINK_TYPE_NOT_LINK);

        Platform_String test_file_content = PSTRING(PUGLY_STR PUGLY_STR);
        Platform_String write_file_path = PSTRING(TEST_DIR "/write_file.txt");
        Platform_String read_file_path = PSTRING(TEST_DIR "/read_file.txt");
        Platform_String move_file_path = PSTRING(TEST_DIR "/move_file.txt");
        
        //Cleanup any possibly remaining files from previous (failed) tests
        PTEST_ERROR(platform_file_remove(write_file_path, false));
        PTEST_ERROR(platform_file_remove(read_file_path, false));
        PTEST_ERROR(platform_file_remove(move_file_path, false));

        //Write two PUGLY_STRING's into the file and flush it (no closing though!)
        Platform_File write_file = {0};
        PTEST_ERROR(platform_file_open(&write_file, write_file_path, PLATFORM_FILE_MODE_WRITE | PLATFORM_FILE_MODE_CREATE | PLATFORM_FILE_MODE_REMOVE_CONTENT));
        PTEST(write_file.is_open);
        PTEST_ERROR(platform_file_write(&write_file, PUGLY_STRING.data, PUGLY_STRING.len));
        PTEST_ERROR(platform_file_write(&write_file, PUGLY_STRING.data, PUGLY_STRING.len));
        PTEST_ERROR(platform_file_flush(&write_file));
        
        platform_test_file_content_equality(write_file_path, test_file_content);

        //Copy the file 
        PTEST_ERROR(platform_file_copy(read_file_path, write_file_path, false));
        platform_test_file_content_equality(read_file_path, test_file_content);
        PTEST_ERROR(platform_file_close(&write_file));

        //Move the file
        PTEST_ERROR(platform_file_move(move_file_path, write_file_path, false));
        PTEST(platform_file_info(write_file_path, NULL) != 0, "Opening of the moved from file should fail since its no longer there!\n");
        platform_test_file_content_equality(move_file_path, test_file_content);

        //Trim the file and 
        PTEST_ERROR(platform_file_resize(move_file_path, PUGLY_STRING.len));
        platform_test_file_content_equality(move_file_path, PUGLY_STRING);

        //Cleanup the directory so it can be deleted.
        PTEST_ERROR(platform_file_remove(write_file_path, false)); //Just in case
        PTEST_ERROR(platform_file_remove(read_file_path, true));
        PTEST_ERROR(platform_file_remove(move_file_path, true));
    }
    PTEST_ERROR(platform_directory_remove(PSTRING(TEST_DIR), true));
    PTEST(platform_directory_remove(PSTRING(TEST_DIR), true) != 0, "removing a missing directory should fail when fail_if_not_found = true\n");
}


#pragma warning(disable:4996) //Dissable "This function or variable may be unsafe. Consider using localtime_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details."
#include <time.h>
typedef struct tm _PDate;
static _PDate _date_from_epoch_time(int64_t epoch_time)
{
    time_t copy = (time_t) (epoch_time / 1000 / 1000);
    return *localtime(&copy);
}

static void platform_test_dir_entry(Platform_Directory_Entry* entries, int64_t entries_count, Platform_String entry_path, Platform_File_Type type, int64_t directory_depth);

static void platform_test_directory_list() 
{
    #define TEST_DIR_LIST_DIR       "__platform_dir_list_test_directory__"
    #define TEST_DIR_DEEPER1        TEST_DIR_LIST_DIR "/deeper1"
    #define TEST_DIR_DEEPER2        TEST_DIR_LIST_DIR "/deeper2"
    #define TEST_DIR_DEEPER3        TEST_DIR_LIST_DIR "/deeper3"
    #define TEST_DIR_DEEPER3_INNER  TEST_DIR_DEEPER3 "/inner"

    PTEST_ERROR(platform_directory_create(PSTRING(TEST_DIR_LIST_DIR), false));
    {
        PTEST_ERROR(platform_directory_create(PSTRING(TEST_DIR_DEEPER1), false));
        PTEST_ERROR(platform_directory_create(PSTRING(TEST_DIR_DEEPER2), false));
        PTEST_ERROR(platform_directory_create(PSTRING(TEST_DIR_DEEPER3), false));
        PTEST_ERROR(platform_directory_create(PSTRING(TEST_DIR_DEEPER3_INNER), false));

        Platform_String temp_file1 = PSTRING(TEST_DIR_LIST_DIR "/temp_file1.txt");
        Platform_String temp_file2 = PSTRING(TEST_DIR_LIST_DIR "/temp_file2.txt");
        Platform_String temp_file3 = PSTRING(TEST_DIR_LIST_DIR "/temp_file3.txt");
        Platform_String temp_file_deep1_1 = PSTRING(TEST_DIR_DEEPER1 "/temp_file1.txt");
        Platform_String temp_file_deep1_2 = PSTRING(TEST_DIR_DEEPER1 "/temp_file2.txt");
        Platform_String temp_file_deep3_1 = PSTRING(TEST_DIR_DEEPER3_INNER "/temp_file1.txt");
        Platform_String temp_file_deep3_2 = PSTRING(TEST_DIR_DEEPER3_INNER "/temp_file2.txt");

        Platform_File first = {0};
        PTEST_ERROR(platform_file_open(&first, temp_file1, PLATFORM_FILE_MODE_WRITE | PLATFORM_FILE_MODE_CREATE | PLATFORM_FILE_MODE_REMOVE_CONTENT));
        PTEST_ERROR(platform_file_write(&first, PUGLY_STRING.data, PUGLY_STRING.len));
        PTEST_ERROR(platform_file_close(&first));

        PTEST_ERROR(platform_file_copy(temp_file2, temp_file1, true));
        PTEST_ERROR(platform_file_copy(temp_file3, temp_file1, true));

        PTEST_ERROR(platform_file_copy(temp_file_deep1_1, temp_file1, true));
        PTEST_ERROR(platform_file_copy(temp_file_deep1_2, temp_file1, true));
            
        PTEST_ERROR(platform_file_copy(temp_file_deep3_1, temp_file1, true));
        PTEST_ERROR(platform_file_copy(temp_file_deep3_2, temp_file1, true));

        //Now the dir should look like (inside TEST_DIR):
        // TEST_DIR:
        //    temp_file1.txt
        //    temp_file2.txt
        //    temp_file3.txt
        //    deeper1:
        //         temp_file1.txt
        //         temp_file2.txt
        //    deeper2:
        //    deeper3:
        //         inner:
        //             temp_file1.txt
        //             temp_file2.txt

        {
            Platform_Directory_Entry* entries = NULL;
            int64_t entries_count = 0;
            PTEST_ERROR(platform_directory_list_contents_alloc(PSTRING(TEST_DIR_LIST_DIR), &entries, &entries_count, 1)); //Only the immediate directory
            PTEST(entries_count == 6);

            platform_test_dir_entry(entries, entries_count, temp_file1, PLATFORM_FILE_TYPE_FILE, 0);
            platform_test_dir_entry(entries, entries_count, temp_file2, PLATFORM_FILE_TYPE_FILE, 0);
            platform_test_dir_entry(entries, entries_count, temp_file3, PLATFORM_FILE_TYPE_FILE, 0);

            platform_test_dir_entry(entries, entries_count, temp_file_deep1_1, PLATFORM_FILE_TYPE_NOT_FOUND, 0);
            platform_test_dir_entry(entries, entries_count, temp_file_deep3_2, PLATFORM_FILE_TYPE_NOT_FOUND, 0);

            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER1), PLATFORM_FILE_TYPE_DIRECTORY, 0);
            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER2), PLATFORM_FILE_TYPE_DIRECTORY, 0);
            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER3), PLATFORM_FILE_TYPE_DIRECTORY, 0);
            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER3_INNER), PLATFORM_FILE_TYPE_NOT_FOUND, 0);

            platform_directory_list_contents_free(entries);
        }
        
        {
            Platform_Directory_Entry* entries = NULL;
            int64_t entries_count = 0;
            PTEST_ERROR(platform_directory_list_contents_alloc(PSTRING(TEST_DIR_LIST_DIR), &entries, &entries_count, -1)); //All of the directories

            for(int i = 0; i < entries_count; i++)
            {
                _PDate created_time = _date_from_epoch_time(entries[i].info.created_epoch_time);
                _PDate write_time = _date_from_epoch_time(entries[i].info.last_write_epoch_time);
                _PDate access_time = _date_from_epoch_time(entries[i].info.last_access_epoch_time);

                #define PDATE_FMT "%04i/%02i/%02i %02i:%02i:%02i"
                #define PDATE_PRINT(time) (time).tm_year + 1900, (time).tm_mon, (time).tm_mday, (time).tm_hour, (time).tm_min, (time).tm_sec
                printf("'%s': created: " PDATE_FMT " write: " PDATE_FMT " access: " PDATE_FMT "\n", entries[i].path, PDATE_PRINT(created_time), PDATE_PRINT(write_time), PDATE_PRINT(access_time));
                assert(entries[i].info.type != PLATFORM_FILE_TYPE_NOT_FOUND);
            }

            PTEST(entries_count == 11);
            platform_test_dir_entry(entries, entries_count, temp_file1, PLATFORM_FILE_TYPE_FILE, 0);
            platform_test_dir_entry(entries, entries_count, temp_file2, PLATFORM_FILE_TYPE_FILE, 0);
            platform_test_dir_entry(entries, entries_count, temp_file3, PLATFORM_FILE_TYPE_FILE, 0);

            platform_test_dir_entry(entries, entries_count, temp_file_deep1_1, PLATFORM_FILE_TYPE_FILE, 1);
            platform_test_dir_entry(entries, entries_count, temp_file_deep3_2, PLATFORM_FILE_TYPE_FILE, 2);

            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER3_INNER), PLATFORM_FILE_TYPE_DIRECTORY, 1);
            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER1), PLATFORM_FILE_TYPE_DIRECTORY, 0);
            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER2), PLATFORM_FILE_TYPE_DIRECTORY, 0);
            platform_test_dir_entry(entries, entries_count, PSTRING(TEST_DIR_DEEPER3), PLATFORM_FILE_TYPE_DIRECTORY, 0);

            platform_directory_list_contents_free(entries);
        }

        PTEST_ERROR(platform_file_remove(temp_file1, true));
        PTEST_ERROR(platform_file_remove(temp_file2, true));
        PTEST_ERROR(platform_file_remove(temp_file3, true));

        PTEST_ERROR(platform_file_remove(temp_file_deep1_1, true));
        PTEST_ERROR(platform_file_remove(temp_file_deep1_2, true));
            
        PTEST_ERROR(platform_file_remove(temp_file_deep3_1, true));
        PTEST_ERROR(platform_file_remove(temp_file_deep3_2, true));

        PTEST_ERROR(platform_directory_remove(PSTRING(TEST_DIR_DEEPER3_INNER), true));
        PTEST_ERROR(platform_directory_remove(PSTRING(TEST_DIR_DEEPER1), true));
        PTEST_ERROR(platform_directory_remove(PSTRING(TEST_DIR_DEEPER2), true));
        PTEST_ERROR(platform_directory_remove(PSTRING(TEST_DIR_DEEPER3), true));
    }
    PTEST_ERROR(platform_directory_remove(PSTRING(TEST_DIR_LIST_DIR), true));
}

static void platform_test_all() 
{   
    printf("platform_test_all() running at directory: '%s'\n", platform_directory_get_startup_working());
    PTEST(strlen(platform_directory_get_startup_working()) > 0);
    PTEST(strlen(platform_get_executable_path()) > 0);

    platform_test_file_io();
    platform_test_directory_list();
}

static void platform_test_file_content_equality(Platform_String path, Platform_String content)
{
    //Check file info for correctness
    Platform_File_Info info = {0};

    PTEST_ERROR(platform_file_info(path, &info)); 

    PTEST(info.type == PLATFORM_FILE_TYPE_FILE);
    PTEST(info.link_type == PLATFORM_LINK_TYPE_NOT_LINK);
    PTEST(info.size == content.len);
    
    //Read the entire file and check content for equality
    void* buffer = malloc((size_t) info.size + 10); 
    PTEST(buffer);

    int64_t bytes_read = 0;
    Platform_File file = {0};
    PTEST_ERROR(platform_file_open(&file, path, PLATFORM_FILE_MODE_READ));
    PTEST_ERROR(platform_file_read(&file, buffer, info.size, &bytes_read));
    PTEST(bytes_read == info.size);
    PTEST(memcmp(buffer, content.data, (size_t) content.len) == 0, "Content must match! Content: \n'%.*s' \nExpected: \n'%.*s'\n",
        (int) content.len, (char*) buffer, (int) content.len, content.data
    );

    //Also verify there really is nothing more
    PTEST_ERROR(platform_file_read(&file, buffer, 1, &bytes_read));
    PTEST(bytes_read == 0, "Eof must be found!");
    
    PTEST_ERROR(platform_file_close(&file));
    free(buffer);
}
static void platform_test_dir_entry(Platform_Directory_Entry* entries, int64_t entries_count, Platform_String entry_path, Platform_File_Type type, int64_t directory_depth)
{
    int64_t concatenated_size = entry_path.len;
    char* concatenated = (char*) calloc(1, (size_t) concatenated_size + 1);
    PTEST(concatenated);
    memcpy(concatenated, entry_path.data, (size_t) entry_path.len);

    Platform_Directory_Entry* entry = NULL;
    for(int64_t i = 0; i < entries_count; i++)
    {
        Platform_String curr_path = {entries[i].path, (int64_t) strlen(entries[i].path)};
        if(curr_path.len == concatenated_size && memcmp(curr_path.data, concatenated, (size_t) concatenated_size) == 0)
        {
            entry = &entries[i];
            break;
        }
    }

    if(type == PLATFORM_FILE_TYPE_NOT_FOUND)
        PTEST(entry == NULL, "Entry '%s' must not be found!", concatenated);
    else
    {
        Platform_File_Info info = {0};
        PTEST_ERROR(platform_file_info(entry_path, &info));

        PTEST(entry, "Entry '%s' must be found!", concatenated);
        PTEST(entry->directory_depth == directory_depth);
        PTEST(entry->info.type == type);

        //@NOTE: getting the info is an access so we skip this.
        //PTEST(info.created_epoch_time == entry->info.created_epoch_time);
        //PTEST(info.last_write_epoch_time == entry->info.last_write_epoch_time);
        //PTEST(info.last_access_epoch_time == entry->info.last_access_epoch_time); 
        PTEST(info.link_type == entry->info.link_type);
        PTEST(info.size == entry->info.size);
        PTEST(info.type == entry->info.type);
    }
}

static bool _platform_test_report(Platform_Error error, bool is_error, const char* expression, const char* file, const char* funcion, int line, const char* format, ...)
{
    is_error = error != 0 || is_error;
    if(is_error)
    {
        printf("TEST(%s) failed in %s %s:%i\n", expression, file, funcion, line);
        if(format && strlen(format) > 0)
        {
            va_list args;               
            va_start(args, format);     
            vprintf(format, args);
            va_end(args);   
        }
        
        if(error != 0)
        {
            int64_t size = platform_translate_error(error, NULL, 0);
            char* message = (char*) malloc(size);
            platform_translate_error(error, message, size);
            printf("Error: %s\n", message);
            free(message);
        }
        fflush(stdout);
    }

    return is_error; 
}
#endif