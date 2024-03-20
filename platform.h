#ifndef JOT_PLATFORM
#define JOT_PLATFORM

#undef _CRT_SECURE_NO_WARNINGS
#undef _GNU_SOURCE

#define _CRT_SECURE_NO_WARNINGS /* ... i hate msvc */
#define _GNU_SOURCE             /* and gcc as well! */

#include <stdint.h>
#include <limits.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

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
    // after main making the whole build unity build, greatly simpifying the build procedure.
    //Can be user overriden by defining it before including platform.h
    #define PLATFORM_OS          PLATFORM_OS_UNKNOWN                 
#endif

#ifndef PLATFORM_COMPILER
    //Becomes one of the PLATFORM_COMPILER_XXX based on the detected compiler. (see below). Can be overriden.
    #define PLATFORM_COMPILER          PLATFORM_COMPILER_UNKNOWN                 
#endif

#ifndef PLATFORM_SYSTEM_BITS
    //The adress space size of the system. Ie either 64 or 32 bit.
    //Can be user overriden by defining it before including platform.h
    #define PLATFORM_SYSTEM_BITS ((UINTPTR_MAX == 0xffffffff) ? 32 : 64)
#endif

#ifndef PLATFORM_ENDIAN
    //The endianness of the system. Is by default PLATFORM_ENDIAN_LITTLE.
    //Can be user overriden by defining it before including platform.h
    #define PLATFORM_ENDIAN      PLATFORM_ENDIAN_LITTLE
#endif

#ifndef PLATFORM_MAX_ALIGN
    //Maximum alignment of bultin data type.
    //If this is incorrect (either too much or too little) please correct it by defining it!
    #define PLATFORM_MAX_ALIGN 8
#endif

#ifndef PLATFORM_SIMD_ALIGN
    #define PLATFORM_SIMD_ALIGN 8
#endif
//Can be used in files without including platform.h but still becomes
// valid if platform.h is included
#if PLATFORM_ENDIAN == PLATFORM_ENDIAN_LITTLE
    #define PLATFORM_HAS_ENDIAN_LITTLE PLATFORM_ENDIAN_LITTLE
#elif PLATFORM_ENDIAN == PLATFORM_ENDIAN_BIG
    #define PLATFORM_HAS_ENDIAN_BIG    PLATFORM_ENDIAN_BIG
#endif


//=========================================
// Platform layer setup
//=========================================
// 
//The semantics must be quivalent to:
//   if(new_size == 0) {free(old_ptr); return NULL;}
//   else              return realloc(old_ptr, new_size);
//The value pointed to by context is not copied and needs to remain valid untill call to platform_deinit()!
typedef struct Platform_Allocator {
    void* (*reallocate)(void* context, int64_t new_size, void* old_ptr);
    void* context;
} Platform_Allocator;

//Initializes the platform layer interface. 
//Should be called before calling any other function.
void platform_init(Platform_Allocator* allocator_or_null);

//Deinitializes the platform layer, freeing all allocated resources back to os.
//platform_init() should be called before using any other fucntion again!
void platform_deinit();

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
void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t align);
//Returns the size in bytes of an allocated block. 
//old_ptr needs to be value returned from platform_heap_reallocate. Align must be the one supplied to platform_heap_reallocate.
//If old_ptr is NULL returns 0.
int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align); 


//=========================================
// Errors 
//=========================================

typedef uint32_t Platform_Error;
enum {
    PLATFORM_ERROR_OK = 0, 
    //... errno codes
    PLATFORM_ERROR_OTHER = INT32_MAX, //Is used when the OS reports no error yet there was clearly an error.
};

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

//A handle to fast (ie non kernel code) recursive mutex. (pthread_mutex_t on linux, CRITICAL_SECTION win32)
typedef struct Platform_Mutex {
    void* handle;
} Platform_Mutex;

//@TODO: thread processor afinity!

//@NOTE: We made pretty much all of the threaded functions (except init-like) into non failing
//       even though they CAN internally return error (we just assert). That is because:
// 1) One can generally do very little when a mutex fails.
// 2) All* error return values are due to a programmer mistake
// 3) All error values require no further action 
//   (ie if it failed then it failed and I cannot do anything about it apart from not calling this function)
// 4) On win32 these functions never fail.
//
// *pthread_mutex_lock has a fail state on too many recursive locks and insufficient privilages which are
// not programmer mistake. However in practice they will not happend and if they do we are doing something
// very specific and a custom impkementation is prefered (or we can just change this).

//initializes a new thread and immedietely starts it with the func function.
//The thread has stack_size_or_zero bytes of stack sizes rounded up to page size
//If stack_size_or_zero is zero or lower uses system default stack size.
//The thread automatically cleans itself up upon completion or termination.
//All threads 
Platform_Error  platform_thread_launch(Platform_Thread* thread, void (*func)(void*), void* context, int64_t stack_size_or_zero); 

int64_t         platform_thread_get_proccessor_count();
Platform_Thread platform_thread_get_current(); //Returns handle to the calling thread
void            platform_thread_sleep(int64_t ms); //Sleeps the calling thread for ms milliseconds
void            platform_thread_exit(int code); //Terminates a thread with an exit code
void            platform_thread_yield(); //Yields the remainder of this thread's time slice to the OS
void            platform_thread_detach(Platform_Thread thread);
void            platform_thread_join(const Platform_Thread* threads, int64_t count); //Blocks calling thread until all threads finish. Must not join the current calling thread!

Platform_Error  platform_mutex_init(Platform_Mutex* mutex);
void            platform_mutex_deinit(Platform_Mutex* mutex);
void            platform_mutex_lock(Platform_Mutex* mutex);
void            platform_mutex_unlock(Platform_Mutex* mutex);
bool            platform_mutex_try_lock(Platform_Mutex* mutex); //Tries to lock a mutex. Returns true if mutex was locked successfully. If it was not returns false without waiting.


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
inline static bool platform_atomic_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value);
inline static bool platform_atomic_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value);

//Performs atomically: { return *target; }
inline static int64_t platform_atomic_load64(volatile const int64_t* target);
inline static int32_t platform_atomic_load32(volatile const int32_t* target);

//Performs atomically: { *target = value; }
inline static void platform_atomic_store64(volatile int64_t* target, int64_t value);
inline static void platform_atomic_store32(volatile int32_t* target, int32_t value);

//Performs atomically: { int64_t copy = *target; *target = value; return copy; }
inline static int64_t platform_atomic_excahnge64(volatile int64_t* target, int64_t value);
inline static int32_t platform_atomic_excahnge32(volatile int32_t* target, int32_t value);

//Performs atomically: { int64_t copy = *target; *target += value; return copy; }
inline static int32_t platform_atomic_add32(volatile int32_t* target, int32_t value);
inline static int64_t platform_atomic_add64(volatile int64_t* target, int64_t value);

//Performs atomically: { int64_t copy = *target; *target -= value; return copy; }
inline static int32_t platform_atomic_sub32(volatile int32_t* target, int32_t value);
inline static int64_t platform_atomic_sub64(volatile int64_t* target, int64_t value);

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
//returns the number of micro-seconds between the epoch and the call to platform_init()
int64_t platform_startup_epoch_time(); 

//converts the epoch time (micro second time since unix epoch) to calendar representation
Platform_Calendar_Time platform_calendar_time_from_epoch_time(int64_t epoch_time_usec);
//Converts calendar time to the precise epoch time (micro second time since unix epoch)
int64_t platform_epoch_time_from_calendar_time(Platform_Calendar_Time calendar_time);

Platform_Calendar_Time platform_local_calendar_time_from_epoch_time(int64_t epoch_time_usec);
int64_t platform_epoch_time_from_local_calendar_time(Platform_Calendar_Time calendar_time);

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
//Saves to was_just_created_or_null wheter the file was just now created. If is null doesnt save anything.
Platform_Error platform_file_create(Platform_String file_path, bool* was_just_created_or_null);
//Removes a file at the specified path. Succeeds if the file exists after the call the file does not exist.
//Saves to was_just_deleted_or_null wheter the file was just now deleted. If is null doesnt save anything.
Platform_Error platform_file_remove(Platform_String file_path, bool* was_just_deleted_or_null);
//Moves or renames a file. If the file cannot be found or renamed to file that already exists, fails.
Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path);
//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path);

//Makes an empty directory
//Saves to was_just_created_or_null wheter the file was just now created. If is null doesnt save anything.
Platform_Error platform_directory_create(Platform_String dir_path, bool* was_just_created_or_null);
//Removes an empty directory
//Saves to was_just_deleted_or_null wheter the file was just now deleted. If is null doesnt save anything.
Platform_Error platform_directory_remove(Platform_String dir_path, bool* was_just_deleted_or_null);

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

typedef struct Platform_File_Watch {
    void* handle;
} Platform_File_Watch;

typedef enum Platform_File_Watch_Flag {
    PLATFORM_FILE_WATCH_CREATED     = 1,
    PLATFORM_FILE_WATCH_DELETED     = 2,
    PLATFORM_FILE_WATCH_MODIFIED    = 4,
    PLATFORM_FILE_WATCH_RENAMED     = 8,
    PLATFORM_FILE_WATCH_DIRECTORY   = 1 << 30,
} Platform_File_Watch_Flag;
 
typedef struct Platform_File_Watch_Event {
    Platform_File_Watch_Flag flag;
    Platform_File_Watch handle;
    Platform_String path;
    Platform_String old_path; //only used in case of PLATFORM_FILE_WATCH_RENAMED to store the previous path.
} Platform_File_Watch_Event;

//Creates a watch of a directory or a single file. 
//file_watch_flags is a bitwise combination of members of Platform_File_Watch_Flag specifying which events we to be reported.
//If file file_watch_or_null is present saves to it a handle to unwatch this watch else doesnt save anything.
//If PLATFORM_FILE_WATCH_DIRECTORY flags is set interprets file_path as directory path and watches it.
//Returns Platform_Error indicating wheter the operation was successfull. 
//Note that the directory in which the watched file resides (or the watched directory itself) must exist else returns error.
Platform_Error platform_file_watch(Platform_File_Watch* file_watch_or_null, Platform_String file_path, int32_t file_watch_flags);
//Deinits a give watch represetn by file_watch_or_null. If file_watch_or_null is null uses file_path to remove specified watch instead.
//Note that if using file_path to unwatch and there are multiple watches watching the same file removes all of them.
Platform_Error platform_file_unwatch(Platform_File_Watch* file_watch_or_null, Platform_String file_path);
//Polls watch events from the given file watch. 
//Returns true if event was polled and false if there are no events in the qeuue.
//If `file_watch_or_null` is null polls from all watches.
//Note that once event is polled it is removed from the queue.
//Note that this functions is implemented efficiently and in case of no events is practically free.
bool platform_file_watch_poll(Platform_File_Watch* file_watch_or_null, Platform_File_Watch_Event* event);

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
    void* address;
} Platform_Stack_Trace_Entry;

//Stops the debugger at the call site
#define platform_debug_break() (*(char*)0 = 0)
//Marks a piece of code as unreachable for the compiler
#define platform_assume_unreachable() (*(char*)0 = 0)

//Captures the current stack frame pointers. 
//Saves up to stack_size pointres into the stack array and returns the number of
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
    //The exception that occured
    Platform_Exception exception;
    
    //A translated stack trace and its size
    const void* const* call_stack; 
    int64_t call_stack_size;

    //Platform specific data containing the cpu state and its size (so that it can be copied and saved)
    const void* execution_context;
    int64_t execution_context_size;

    //The epoch time of the exception
    int64_t epoch_time;
} Platform_Sandbox_Error;

//Launches the sandboxed_func inside a sendbox protecting the outside environment 
// from any exceptions, including hardware exceptions that might occur inside sandboxed_func.
//If an exception occurs collects execution context including stack pointers and calls
// 'error_func_or_null' if not null. Gracefuly recovers from all errors. 
//Returns the error that occured or PLATFORM_EXCEPTION_NONE = 0 on success.
Platform_Exception platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context,
    void (*error_func_or_null)(void* error_context, Platform_Sandbox_Error error),
    void* error_context);

//Convertes the sandbox error to string. The string value is the name of the enum
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

#undef ATTRIBUTE_RESTRICT                                   
#undef ATTRIBUTE_ALIGNED                            
#undef ATTRIBUTE_INLINE_ALWAYS                               
#undef ATTRIBUTE_INLINE_NEVER                                  
#undef ATTRIBUTE_THREAD_LOCAL                               
#undef ATTRIBUTE_FORMAT_FUNC 
#undef ATTRIBUTE_FORMAT_ARG                          
#undef ATTRIBUTE_NORETURN  

#undef ATTRIBUTE_RETURN_RESTRICT
#undef ATTRIBUTE_RETURN_ALIGNED
#undef ATTRIBUTE_RETURN_ALIGNED_ARG

#if defined(_MSC_VER)
    #define ATTRIBUTE_RESTRICT                                      __restrict
    #define ATTRIBUTE_INLINE_ALWAYS                                  __forceinline
    #define ATTRIBUTE_INLINE_NEVER                                     __declspec(noinline)
    #define ATTRIBUTE_THREAD_LOCAL                                  __declspec(thread)
    #define ATTRIBUTE_ALIGNED(bytes)                                __declspec(align(bytes))
    #define ATTRIBUTE_FORMAT_FUNC(format_arg, format_arg_index)     /* empty */
    #define ATTRIBUTE_FORMAT_ARG                                    _Printf_format_string_  
    #define ATTRIBUTE_RETURN_RESTRICT                               __declspec(restrict)
    #define ATTRIBUTE_RETURN_ALIGNED(align)                         /* empty */
    #define ATTRIBUTE_RETURN_ALIGNED_ARG(align_arg_index)           /* empty */
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_RESTRICT                                      __restrict__
    #define ATTRIBUTE_INLINE_ALWAYS                                  __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER                                     __attribute__((noinline))
    #define ATTRIBUTE_THREAD_LOCAL                                  __thread
    #define ATTRIBUTE_ALIGNED(bytes)                                __attribute__((aligned(bytes)))
    #define ATTRIBUTE_FORMAT_FUNC(format_arg, format_arg_index)     __attribute__((format_arg (printf, format_arg_index, 0)))
    #define ATTRIBUTE_FORMAT_ARG                                    /* empty */    
    #define ATTRIBUTE_NORETURN                                      __attribute__((noreturn))
    #define ATTRIBUTE_RETURN_RESTRICT                               __attribute__((malloc))
    #define ATTRIBUTE_RETURN_ALIGNED(align)                         __attribute__((assume_aligned(align))
    #define ATTRIBUTE_RETURN_ALIGNED_ARG(align_arg_index)           __attribute__((alloc_align (align_arg_index)));
#else
    #define ATTRIBUTE_RESTRICT                                      /* empty */                              
    #define ATTRIBUTE_INLINE_ALWAYS                                  /* empty */                            
    #define ATTRIBUTE_INLINE_NEVER                                     /* empty */                              
    #define ATTRIBUTE_THREAD_LOCAL                                  /* empty */                           
    #define ATTRIBUTE_ALIGNED                                       /* empty */                   
    #define ATTRIBUTE_FORMAT_FUNC                                   /* empty */  
    #define ATTRIBUTE_FORMAT_ARG                                    /* empty */                   
    #define ATTRIBUTE_NORETURN                                      /* empty */  
    #define ATTRIBUTE_RETURN_RESTRICT                               /* empty */
    #define ATTRIBUTE_RETURN_ALIGNED(align)                         /* empty */
    #define ATTRIBUTE_RETURN_ALIGNED_ARG(align_arg_index)           /* empty */
#endif

// =================== INLINE IMPLEMENTATION ============================
#if defined(_MSC_VER)
    #include <stdio.h>
    #include <intrin.h>
    #include <assert.h>
    #include <sal.h> //for _Printf_format_string_

    #undef platform_debug_break
    #define platform_debug_break() __debugbreak() 

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
    
    inline static int64_t platform_atomic_load64(volatile const int64_t* target)
    {
        return (int64_t) _InterlockedOr64((volatile long long*) target, 0);
    }
    inline static int32_t platform_atomic_load32(volatile const int32_t* target)
    {
        return (int32_t) _InterlockedOr((volatile long*) target, 0);
    }

    inline static void platform_atomic_store64(volatile int64_t* target, int64_t value)
    {
        platform_atomic_excahnge64(target, value);
    }
    inline static void platform_atomic_store32(volatile int32_t* target, int32_t value)
    {
        platform_atomic_excahnge32(target, value);
    }

    inline static bool platform_atomic_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value)
    {
        return _InterlockedCompareExchange64((volatile long long*) target, (long long) new_value, (long long) old_value) == (long long) old_value;
    }

    inline static bool platform_atomic_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value)
    {
        return _InterlockedCompareExchange((volatile long*) target, (long) new_value, (long) old_value) == (long) old_value;
    }

    inline static int64_t platform_atomic_excahnge64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) _InterlockedExchange64((volatile long long*) target, (long long) value);
    }

    inline static int32_t platform_atomic_excahnge32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) _InterlockedExchange((volatile long*) target, (long) value);
    }
    
    inline static int64_t platform_atomic_add64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) _InterlockedExchangeAdd64((volatile long long*) target, (long long) value);
    }

    inline static int32_t platform_atomic_add32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) _InterlockedExchangeAdd((volatile long*) target, (long) value);
    }

    inline static int32_t platform_atomic_sub32(volatile int32_t* target, int32_t value)
    {
        return platform_atomic_add32(target, -value);
    }

    inline static int64_t platform_atomic_sub64(volatile int64_t* target, int64_t value)
    {
        return platform_atomic_add64(target, -value);
    }
   
   

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
        return 32 - __builtin_ctz((unsigned int) num) - 1;
    }
    inline static int32_t platform_find_first_set_bit64(uint64_t num)
    {
        return 64 - __builtin_ctzll((unsigned long long) num) - 1;
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
    inline static bool platform_atomic_compare_and_swap64(volatile int64_t* target, int64_t old_value, int64_t new_value)
    {
        return __atomic_compare_exchange_n(target, &old_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }

    inline static bool platform_atomic_compare_and_swap32(volatile int32_t* target, int32_t old_value, int32_t new_value)
    {
        return __atomic_compare_exchange_n(target, &old_value, new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }

    inline static int64_t platform_atomic_load64(volatile const int64_t* target)
    {
        return (int64_t) __atomic_load_n(target, __ATOMIC_SEQ_CST);
    }
    inline static int32_t platform_atomic_load32(volatile const int32_t* target)
    {
        return (int32_t) __atomic_load_n(target, __ATOMIC_SEQ_CST);
    }

    inline static void platform_atomic_store64(volatile int64_t* target, int64_t value)
    {
        __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
    }
    inline static void platform_atomic_store32(volatile int32_t* target, int32_t value)
    {
        __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
    }

    inline static int64_t platform_atomic_excahnge64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }
    inline static int32_t platform_atomic_excahnge32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }

    inline static int32_t platform_atomic_add32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_add_fetch(target, value, __ATOMIC_SEQ_CST);
    }
    inline static int64_t platform_atomic_add64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_add_fetch(target, value, __ATOMIC_SEQ_CST);
    }

    inline static int32_t platform_atomic_sub32(volatile int32_t* target, int32_t value)
    {
        return (int32_t) __atomic_sub_fetch(target, value, __ATOMIC_SEQ_CST);
    }
    inline static int64_t platform_atomic_sub64(volatile int64_t* target, int64_t value)
    {
        return (int64_t) __atomic_sub_fetch(target, value, __ATOMIC_SEQ_CST);
    }


#endif

#endif
