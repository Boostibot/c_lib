#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>

//=========================================
// Virtual memory
//=========================================

#define POSIX_ERRNO_CODES_X() \
    X(E2BIG) \
    X(EACCES) \
    X(EADDRINUSE) \
    X(EADDRNOTAVAIL) \
    X(EAFNOSUPPORT) \
    X(EAGAIN) \
    X(EALREADY) \
    X(EBADF) \
    X(EBADMSG) \
    X(EBUSY) \
    X(ECANCELED) \
    X(ECHILD) \
    X(ECONNABORTED) \
    X(ECONNREFUSED) \
    X(ECONNRESET) \
    X(EDEADLK) \
    X(EDESTADDRREQ) \
    X(EDOM) \
    X(EEXIST) \
    X(EFAULT) \
    X(EFBIG) \
    X(EHOSTUNREACH) \
    X(EIDRM) \
    X(EILSEQ) \
    X(EINPROGRESS) \
    X(EINTR) \
    X(EINVAL) \
    X(EIO) \
    X(EISCONN) \
    X(EISDIR) \
    X(ELOOP) \
    X(EMFILE) \
    X(EMLINK) \
    X(EMSGSIZE) \
    X(ENAMETOOLONG) \
    X(ENETDOWN) \
    X(ENETRESET) \
    X(ENETUNREACH) \
    X(ENFILE) \
    X(ENOBUFS) \
    X(ENODATA) \
    X(ENODEV) \
    X(ENOENT) \
    X(ENOEXEC) \
    X(ENOLCK) \
    X(ENOLINK) \
    X(ENOMEM) \
    X(ENOMSG) \
    X(ENOPROTOOPT) \
    X(ENOSPC) \
    X(ENOSR) \
    X(ENOSTR) \
    X(ENOSYS) \
    X(ENOTCONN) \
    X(ENOTDIR) \
    X(ENOTEMPTY) \
    X(ENOTRECOVERABLE) \
    X(ENOTSOCK) \
    X(ENOTSUP) \
    X(ENOTTY) \
    X(ENXIO) \
    X(EOPNOTSUPP) \
    X(EOVERFLOW) \
    X(EOWNERDEAD) \
    X(EPERM) \
    X(EPIPE) \
    X(EPROTO) \
    X(EPROTONOSUPPORT) \
    X(EPROTOTYPE) \
    X(ERANGE) \
    X(EROFS) \
    X(ESPIPE) \
    X(ESRCH) \
    X(ETIME) \
    X(ETIMEDOUT) \
    X(ETXTBSY) \
    X(EWOULDBLOCK) \
    X(EXDEV) \

Platform_Error _platform_error_code(bool state)
{
    if(state)
        return PLATFORM_ERROR_OK;
    else
    {
        Platform_Error out = (Platform_Error) (errno);

        //make sure its actually an error
        if(out == PLATFORM_ERROR_OK)
            out = PLATFORM_ERROR_OTHER;
        return out;
    }
}

void _print_errno(int errno_val)
{
    const char* name = "None";
    #define X(ERRNO_CODE) \
        if(errno_val == ERRNO_CODE) name = #ERRNO_CODE;

    POSIX_ERRNO_CODES_X()
    printf("errno %s: %s\n", name, strerror(errno_val));
    #undef X
}

#include <sys/mman.h>
#include <errno.h>
Platform_Error platform_virtual_reallocate(void** output_adress_or_null, void* allocate_at, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection)
{
    Platform_Error error = PLATFORM_ERROR_OK;
    void* out = NULL;

    if(action & PLATFORM_VIRTUAL_ALLOC_RESERVE)   
    {
        out = mmap(allocate_at, (size_t) bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(out == MAP_FAILED)
        {
            error = (Platform_Error) errno;
            out = NULL;
        }
    }
    if(action & PLATFORM_VIRTUAL_ALLOC_RELEASE)
    {
        if(munmap(allocate_at, (size_t) bytes) == -1)
            error = (Platform_Error) errno;
    }

    if(action & PLATFORM_VIRTUAL_ALLOC_COMMIT)
    {
        if(action & PLATFORM_VIRTUAL_ALLOC_RESERVE)
            allocate_at = out;

        if(allocate_at) 
        {
            int prot = PROT_NONE;
            if(protection & PLATFORM_MEMORY_PROT_READ) prot |= PROT_READ;
            if(protection & PLATFORM_MEMORY_PROT_WRITE) prot |= PROT_WRITE;
            if(protection & PLATFORM_MEMORY_PROT_EXECUTE) prot |= PROT_EXEC;

            assert((size_t) allocate_at % platform_page_size() == 0);
            if(mprotect(allocate_at, (size_t) bytes, prot) == 0)
            {
                madvise(allocate_at, (size_t) bytes, MADV_WILLNEED);
                out = allocate_at;
            }
            else
                error = (Platform_Error) errno;
        }
    }
    if(action & PLATFORM_VIRTUAL_ALLOC_DECOMMIT)
    {
        if(mprotect(allocate_at, (size_t) bytes, PROT_NONE) == 0)
        {
            madvise(allocate_at, (size_t) bytes, MADV_DONTNEED);
            out = allocate_at;
        }
        else
            error = (Platform_Error) errno;
    }

    if(output_adress_or_null)
        *output_adress_or_null = out;

    return error;
}

#include <unistd.h>
int64_t platform_page_size()
{
    return (int64_t) getpagesize();
}

int64_t platform_allocation_granularity()
{
    return (int64_t) getpagesize();
}

int64_t platform_translate_error(Platform_Error error, char* translated, int64_t translated_size)
{
    const char* str = NULL;
    if(error == PLATFORM_ERROR_OK)
        str = "okay";
    if(error == PLATFORM_ERROR_OTHER)
        str = "Other platform specific error occurred";
    else
        str = strerror((int) error);

    int64_t needed_size = (int64_t) strlen(str);
    int64_t availible_size = needed_size < translated_size - 1 ? needed_size : translated_size - 1;
    if(translated_size > 0)
    {
        memcpy(translated, str, (size_t) availible_size);
        translated[availible_size] = '\0';
    }

    return needed_size + 1;
}

#include <malloc.h>
int64_t platform_heap_get_block_size(const void* old_ptr, int64_t align)
{
    assert(align > 0);
    int64_t size = 0;
    if(old_ptr)
        size = (int64_t) malloc_usable_size((void*) old_ptr);

    return size;
}

void* platform_heap_reallocate(isize new_size, void* old_ptr, isize old_size, isize align)
{
    if(align <= (int64_t) sizeof(long long int))
    {
        if(new_size > 0)
            return realloc(old_ptr, (size_t) new_size);

        free(old_ptr);
        return NULL;
    }
    else
    {
        void* out = NULL;
        if(new_size > 0)
        {
            if(posix_memalign(&out, (size_t) align, (size_t) new_size))
            {
                int64_t min_size = old_size < new_size ? old_size : new_size;
                memcpy(out, old_ptr, (size_t) min_size);
            }
        }

        if(old_ptr)
            free(old_ptr);

        return out;
    }
}

//=========================================
// Threading
//=========================================
#include <sched.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

//set name stuff
#include <linux/prctl.h>  /* Definition of PR_* constants */
#include <sys/prctl.h>
typedef struct Platform_Pthread_State {
    pthread_t thread;
    char* name;
    int name_size;
    void (*func)(void*);
    void* context;
} Platform_Pthread_State;

_Thread_local Platform_Pthread_State* t_thread_state = NULL;
void* _platform_pthread_start_routine(void* arg)
{
    Platform_Pthread_State* state = (Platform_Pthread_State*) arg;
    t_thread_state = state;
    prctl(PR_SET_NAME, state->name);
    state->func(state->context);
    free(state->name);
    free(state);
    return NULL;
}

#include <stdarg.h>
Platform_Error  platform_thread_launch(isize stack_size_or_zero, void (*func)(void*), void* context, const char* name_fmt, ...)
{
    Platform_Error error = 0;
    Platform_Pthread_State* thread_state = (Platform_Pthread_State*) calloc(1, sizeof(Platform_Pthread_State));
    error = _platform_error_code(thread_state != NULL);
    if(error == 0)
    {
        pthread_attr_t attr = {0};
        pthread_attr_init(&attr);
        if(stack_size_or_zero > 0)
            pthread_attr_setstacksize(&attr, (size_t) stack_size_or_zero);

        name_fmt = name_fmt ? name_fmt : "";
        va_list args;
        va_list copy;
        va_start(args, name_fmt);
        va_copy(copy, args);
        int count = vsnprintf(NULL, 0, name_fmt, copy);
        if(count < 16)
            count = 16;
        thread_state->name = (char*) malloc(count + 1);
        thread_state->name_size = vsnprintf(thread_state->name, count + 1, name_fmt, args);
        va_end(args);

        thread_state->func = func;
        thread_state->context = context;
        error = pthread_create(&thread_state->thread, &attr, _platform_pthread_start_routine, thread_state);
        pthread_attr_destroy(&attr);
    }

    if(error)
        free(thread_state);

    return error;
}

int32_t platform_thread_get_processor_count()
{
    cpu_set_t cs;
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);
    return CPU_COUNT(&cs);
}

int32_t platform_thread_id()
{
    return (int32_t) gettid();
}

static _Thread_local char* t_thread_name = NULL;
static _Thread_local char t_thread_name_string[16] = {0};
static bool g_main_thread_id_init = false;
int32_t platform_thread_main_id()
{
    static int32_t g_main_thread_id = 0;
    if(g_main_thread_id_init == false) {
        g_main_thread_id = (int32_t) gettid();
        g_main_thread_id_init = true;
    }

    return g_main_thread_id;
}

void _platform_threads_deinit() 
{
    g_main_thread_id_init = false;
    t_thread_name = NULL;
}

#include <stdio.h>
const char* platform_thread_name()
{
    if(t_thread_name == NULL) {

        if(t_thread_state)
            t_thread_name = t_thread_state->name;
        else if(platform_thread_id() == platform_thread_main_id())
            t_thread_name = "main";
        else {
            // else init to the name retrieved from the OS
            prctl(PR_GET_NAME, t_thread_name_string);
            //if the name is the default one use the thread id instead (since the default one provides no information)
            if(strncmp(program_invocation_name, t_thread_name_string, sizeof t_thread_name_string) == 0)
                snprintf(t_thread_name_string, sizeof t_thread_name_string, "<%04x>", platform_thread_id());
            t_thread_name = t_thread_name_string;
        }
    }
    
    return t_thread_name;
}

void platform_thread_sleep(double seconds)
{
    if(seconds > 0)
    {
        int64_t nanosecs = (int64_t) (seconds*1000000000LL);
        struct timespec ts = {0};
        ts.tv_sec = nanosecs / 1000000000LL; 
        ts.tv_nsec = nanosecs % 1000000000LL; 

        while(nanosleep(&ts, &ts) == -1);
    }
}

void platform_thread_exit(int code)
{
    pthread_exit((void*) (int64_t) code);
}

void platform_thread_yield()
{
    sched_yield();
}


//======================================
// MUTEX
//======================================
Platform_Error platform_mutex_init(Platform_Mutex* mutex)
{
    Platform_Error error = 0;
    platform_mutex_deinit(mutex);
    mutex->handle = calloc(1, sizeof(pthread_mutex_t));
    error = _platform_error_code(mutex->handle != NULL);
    if(error == 0)
        error = pthread_mutex_init((pthread_mutex_t*) mutex->handle, NULL);

    if(error)
        platform_mutex_deinit(mutex);

    return error;
}

void platform_mutex_deinit(Platform_Mutex* mutex)
{
    if(mutex->handle)
    {
        pthread_mutex_destroy((pthread_mutex_t*) mutex->handle);
        free(mutex->handle);
        mutex->handle = 0;
    }
}

void platform_mutex_lock(Platform_Mutex* mutex) { pthread_mutex_lock((pthread_mutex_t*) mutex->handle); }
void platform_mutex_unlock(Platform_Mutex* mutex) { pthread_mutex_unlock((pthread_mutex_t*) mutex->handle); }
bool platform_mutex_try_lock(Platform_Mutex* mutex) { return pthread_mutex_trylock((pthread_mutex_t*) mutex->handle) == 0; }

//======================================
// RW Lock
//======================================
Platform_Error platform_rwlock_init(Platform_RW_Lock* mutex)
{
    Platform_Error error = 0;
    platform_rwlock_deinit(mutex);
    mutex->handle = calloc(1, sizeof(pthread_rwlock_t));
    error = _platform_error_code(mutex->handle != NULL);
    if(error == 0)
        error = pthread_rwlock_init((pthread_rwlock_t*) mutex->handle, NULL);

    if(error)
        platform_rwlock_deinit(mutex);

    return error;
}

void platform_rwlock_deinit(Platform_RW_Lock* mutex)
{
    if(mutex->handle)
    {
        pthread_rwlock_destroy((pthread_rwlock_t*) mutex->handle);
        free(mutex->handle);
        mutex->handle = 0;
    }
}

void platform_rwlock_reader_lock(Platform_RW_Lock* mutex)       { pthread_rwlock_rdlock((pthread_rwlock_t*) mutex->handle);}
void platform_rwlock_reader_unlock(Platform_RW_Lock* mutex)     { pthread_rwlock_unlock((pthread_rwlock_t*) mutex->handle);}
void platform_rwlock_writer_lock(Platform_RW_Lock* mutex)       { pthread_rwlock_wrlock((pthread_rwlock_t*) mutex->handle);}
void platform_rwlock_writer_unlock(Platform_RW_Lock* mutex)     { pthread_rwlock_unlock((pthread_rwlock_t*) mutex->handle);}
bool platform_rwlock_reader_try_lock(Platform_RW_Lock* mutex)   { return pthread_rwlock_tryrdlock((pthread_rwlock_t*) mutex->handle) == 0;}
bool platform_rwlock_writer_try_lock(Platform_RW_Lock* mutex)   { return pthread_rwlock_trywrlock((pthread_rwlock_t*) mutex->handle) == 0;}

//======================================
// COND VAR
//======================================
Platform_Error platform_cond_var_init(Platform_Cond_Var* cond_var)
{
    Platform_Error error = 0;
    platform_cond_var_deinit(cond_var);
    cond_var->handle = calloc(1, sizeof(pthread_cond_t));
    error = _platform_error_code(cond_var->handle != NULL);
    if(error == 0)
        error = pthread_cond_init((pthread_cond_t*) cond_var->handle, NULL);

    if(error)
        platform_cond_var_deinit(cond_var);

    return error;
}

void platform_cond_var_deinit(Platform_Cond_Var* cond_var)
{
    if(cond_var->handle)
    {
        pthread_cond_destroy((pthread_cond_t*) cond_var->handle);
        free(cond_var->handle);
        cond_var->handle = 0;
    }
}

struct timespec _platform_waitsec_to_timespec(double sec)
{
    struct timespec now = {0};
    (void) clock_gettime(CLOCK_REALTIME , &now);

    uint64_t nanosecs = (uint64_t) (sec*1000000000LL);
    uint64_t combined_ns = (uint64_t) now.tv_nsec + nanosecs;
    struct timespec tm = {0};
    tm.tv_nsec = combined_ns % 1000000000ULL; 
    tm.tv_sec = now.tv_sec + combined_ns / 1000000000ULL; 
    return tm;
}

void platform_cond_var_wake_single(Platform_Cond_Var* cond_var) { pthread_cond_signal((pthread_cond_t*) cond_var->handle); }
void platform_cond_var_wake_all(Platform_Cond_Var* cond_var)    { pthread_cond_broadcast((pthread_cond_t*) cond_var->handle); }
bool platform_cond_var_wait_mutex(Platform_Cond_Var* cond_var, Platform_Mutex* mutex, double seconds_or_negative_if_infinite)
{
    if(seconds_or_negative_if_infinite < 0) {
        pthread_cond_wait((pthread_cond_t*) cond_var->handle, (pthread_mutex_t*) mutex->handle);
        return true;
    }
    else {
        struct timespec tm = _platform_waitsec_to_timespec(seconds_or_negative_if_infinite);
        return pthread_cond_timedwait((pthread_cond_t*) cond_var->handle, (pthread_mutex_t*) mutex->handle, &tm) != 0;
    }
}

#include <linux/futex.h> 
#include <sys/syscall.h> 
#include <unistd.h>
#include <sched.h>
#include <errno.h>
void platform_futex_wake_all(volatile void* state) {
    syscall(SYS_futex, (void*) state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX, NULL, NULL, 0);
}

void platform_futex_wake(volatile void* state) {
    syscall(SYS_futex, (void*) state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0);
}

bool platform_futex_wait(volatile void* state, uint32_t undesired, double seconds_or_negatove_if_infinite)
{
    struct timespec tm = {0};
    struct timespec* tm_ptr = NULL;
    if(seconds_or_negatove_if_infinite >= 0)
    {
        int64_t nanosecs = (int64_t) (seconds_or_negatove_if_infinite*1000000000LL);
        tm.tv_sec = nanosecs / 1000000000LL; 
        tm.tv_nsec = nanosecs % 1000000000LL; 
        tm_ptr = &tm;
    }
    long ret = syscall(SYS_futex, (void*) state, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, undesired, tm_ptr, NULL, 0);
    if (ret == -1 && errno == ETIMEDOUT) 
        return false;
    return true;
}

//=========================================
// Timings 
//=========================================
int64_t platform_perf_counter()
{
    struct timespec ts = {0};
    (void) clock_gettime(CLOCK_MONOTONIC_RAW , &ts);
    return (int64_t) ts.tv_nsec + ts.tv_sec * 1000000000LL;
}

int64_t platform_perf_counter_frequency()
{
	return (int64_t) 1000000000LL;
}

static int64_t g_startup_perf_counter;
static int64_t g_startup_epoch_time;
int64_t platform_perf_counter_startup()
{
    if(g_startup_perf_counter == 0)
        g_startup_perf_counter = platform_perf_counter();

    return g_startup_perf_counter;
}

int64_t platform_epoch_time()
{
    struct timespec ts = {0};
    (void) clock_gettime(CLOCK_REALTIME , &ts);
    return (int64_t) ts.tv_nsec/1000 + ts.tv_sec*100000;
}

int64_t platform_epoch_time_startup()
{
    if(g_startup_epoch_time == 0)
        g_startup_epoch_time = platform_epoch_time();

    return g_startup_epoch_time;
}

static void _platform_perf_counters_deinit()
{
    g_startup_perf_counter = 0;
    g_startup_epoch_time = 0;
}

static void _platform_perf_counters_init()
{
    platform_epoch_time_startup();
    platform_perf_counter_startup();
}
//=========================================
// Filesystem
//=========================================

//@TODO: Make a general ephemeral acquire
#define PLATFORM_PRINT_OUT_OF_MEMORY(ammount) \
    fprintf(stderr, "platform allocation failed while calling function %s! not enough memory! requested: %lli", (__FUNCTION__), (long long) (ammount))

const char* _ephemeral_null_terminate(Platform_String string)
{
    if(string.data == NULL || string.count <= 0)
        return "";

    //We use a trick to not pointlessly copy and null terminate strings that are null terminated.
    //For this of course we need to check if they contain null one-past-the-end-of-the-buffer.
    //This is illegal by the standard but we can exploit the fact that errors (or any other type
    //of side effect from reading memory location) can only occur on individual pages.
    //
    //This means if the null termination is on the same page as any part of the string we are free
    //to check it. This makes it so we only have probability of 1/PAGE_SIZE that we will needlessly
    //copy and null terminate string that was already null terminated

    enum {
        PAGE_BOUNDARY = 1024,         //Assume very small for safety
        DO_CONDTIONAL_NULL_TERMINATION = true, //set to true for maximum compatibility 
        MAX_COPIED_SIMULATENOUS = 4, //The number of copied strings that are able to coexist in the system. 
        MAX_COPIED_SIZE = 1024,
        MIN_COPIED_SIZE = 64,
    };

    if(DO_CONDTIONAL_NULL_TERMINATION)
    {
        const char* potential_null_termination = string.data + string.count;
        bool is_null_termianted = false;

        //if the potential_null_termination is on the same page as the rest of the string...
        if((int64_t) potential_null_termination % PAGE_BOUNDARY != 0)
        {
            //Do illegal read past the end of the buffer to check if it is null terminated
            is_null_termianted = *potential_null_termination == '\0';
        }

        if(is_null_termianted)
            return string.data;
    }

    static __thread char* strings[MAX_COPIED_SIMULATENOUS] = {0};
    static __thread int64_t string_sizes[MAX_COPIED_SIMULATENOUS] = {0};
    static __thread int64_t string_slot = 0;

    const char* out_string = "";
    char** curr_data = &strings[string_slot];
    int64_t* curr_size = &string_sizes[string_slot];
    string_slot = (string_slot + 1) % MAX_COPIED_SIMULATENOUS;

    bool had_error = false;
    //If we need a bigger buffer OR the previous allocation was too big and the new one isnt
    if(*curr_size <= string.count || (*curr_size > MAX_COPIED_SIZE && string.count <= MAX_COPIED_SIZE))
    {
        int64_t alloc_size = string.count + 1;
        if(alloc_size < MIN_COPIED_SIZE)
            alloc_size = MIN_COPIED_SIZE;

        void* new_data = realloc(*curr_data, (size_t) alloc_size);
        if(new_data == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(alloc_size);
            had_error = true;
            *curr_size = 0;
            *curr_data = NULL;
        }
        else
        {
            *curr_size = alloc_size;
            *curr_data = (char*) new_data;
        }
    }

    if(had_error == false)
    {
        memmove(*curr_data, string.data, (size_t) string.count);
        (*curr_data)[string.count] = '\0';
        out_string = *curr_data;
    }

    return out_string;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null)
{
    struct stat buf = {0};
    bool state = fstatat(AT_FDCWD, _ephemeral_null_terminate(file_path), &buf, AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) == 0;
    if(state && info_or_null != NULL)
    {
        memset(info_or_null, 0, sizeof *info_or_null);
        info_or_null->size = buf.st_size;
        info_or_null->created_epoch_time = (int64_t) buf.st_ctime * 1000000;
        info_or_null->last_write_epoch_time = (int64_t) buf.st_mtime * 1000000;
        info_or_null->last_access_epoch_time = (int64_t) buf.st_atime * 1000000;

        if(S_ISREG(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_FILE;
        else if(S_ISDIR(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_DIRECTORY;
        else if(S_ISCHR(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_CHARACTER_DEVICE;
        else if(S_ISFIFO(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_PIPE;
        else if(S_ISSOCK(buf.st_mode))
            info_or_null->type = PLATFORM_FILE_TYPE_SOCKET;
        else
            info_or_null->type = PLATFORM_FILE_TYPE_OTHER;

        if(S_ISLNK(buf.st_mode))
            info_or_null->link_type = PLATFORM_LINK_TYPE_SYM;
        else
            info_or_null->link_type = PLATFORM_LINK_TYPE_NOT_LINK;
    }

    return _platform_error_code(state);
}

#define OPEN_FILE_PERMS 0744

#include <unistd.h>
#include <sys/types.h>

int _platform_fd(const Platform_File* file)
{
    return (int) (uintptr_t) file->handle - 1;
}

Platform_Error platform_file_open(Platform_File* file, Platform_String path, int open_flags)
{
    platform_file_close(file);

    #ifndef O_LARGEFILE
        #define O_LARGEFILE 0
    #endif

    int mode = O_NOCTTY | O_LARGEFILE;
    if(((open_flags & PLATFORM_FILE_OPEN_WRITE) && (open_flags & PLATFORM_FILE_OPEN_READ)) || (open_flags & PLATFORM_FILE_OPEN_TEMPORARY))
        mode |= O_RDWR;
    else if(open_flags & PLATFORM_FILE_OPEN_READ)
        mode |= O_RDONLY;
    else if(open_flags & PLATFORM_FILE_OPEN_WRITE)
        mode |= O_WRONLY;

    if(open_flags & PLATFORM_FILE_OPEN_CREATE_MUST_NOT_EXIST)
        mode |= O_EXCL;
    else if(open_flags & PLATFORM_FILE_OPEN_CREATE)
        mode |= O_CREAT;
    if(open_flags & PLATFORM_FILE_OPEN_REMOVE_CONTENT)
        mode |= O_TRUNC;
    if(open_flags & PLATFORM_FILE_OPEN_TEMPORARY) 
        mode |= O_TMPFILE;
    if(open_flags & PLATFORM_FILE_OPEN_HINT_UNBUFFERED) 
        mode |= O_DIRECT;
    if(open_flags & PLATFORM_FILE_OPEN_HINT_WRITETHROUGH) 
        mode |= O_SYNC;

    int fd = open(_ephemeral_null_terminate(path), mode, OPEN_FILE_PERMS);
    file->handle = (void*) (ptrdiff_t) (fd + 1);

    if(file->handle) {
        if(open_flags & PLATFORM_FILE_OPEN_HINT_FRONT_TO_BACK_ACCESS) posix_fadvise(_platform_fd(file), 0, 0, POSIX_FADV_SEQUENTIAL);
        if(open_flags & PLATFORM_FILE_OPEN_HINT_RANDOM_ACCESS) posix_fadvise(_platform_fd(file), 0, 0, POSIX_FADV_RANDOM);
    }

    return _platform_error_code(file->handle != 0);
}

Platform_Error platform_file_close(Platform_File* file)
{
    bool state = true;
    if(file && file->handle) {
        state = close(_platform_fd(file)) == 0;
        file->handle = NULL;
    }

    return _platform_error_code(state);
}

Platform_Error platform_file_read(Platform_File* file, void* buffer, isize size, isize offset, isize* read_bytes_because_eof)
{
    bool state = false;
    isize total_read = 0;
    if(file->handle)
    {
        for(; total_read < size;)
        {
            ssize_t bytes_read = pread(_platform_fd(file), (unsigned char*)buffer + total_read, (size_t) (size - total_read), offset);
            //eof
            if(bytes_read == 0) {
                state = true;
                break;
            }
            if(bytes_read == -1) {
                state = false;
                break;
            }

            total_read += bytes_read;
        }
    }

    if(read_bytes_because_eof)
        *read_bytes_because_eof = total_read;

    return _platform_error_code(state);
}

Platform_Error platform_file_write(Platform_File* file, const void* buffer, int64_t size, isize offset)
{
    int64_t total_written = 0;
    for(; file->handle && total_written < size;) {
        ssize_t bytes_written = pwrite(_platform_fd(file), (unsigned char*) buffer + total_written, (size_t) (size - total_written), offset);
        if(bytes_written <= 0)
            break;

        total_written += bytes_written;
    }

    return _platform_error_code(file->handle && total_written == size);
}


Platform_Error platform_file_read_entire(Platform_String file_path, void* buffer, isize buffer_size)
{
    Platform_File file = {0};
    Platform_Error error = platform_file_open(&file, file_path, PLATFORM_FILE_OPEN_READ);
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
            PLATFORM_FILE_OPEN_WRITE | PLATFORM_FILE_OPEN_REMOVE_CONTENT | (fail_if_not_found ? 0 : PLATFORM_FILE_OPEN_CREATE));
    if(error == 0)
        error = platform_file_write(&file, buffer, buffer_size, 0);
    platform_file_close(&file);
    return error;
}
Platform_Error platform_file_append_entire(Platform_String file_path, const void* buffer, isize buffer_size, bool fail_if_not_found)
{
    int mode = O_NOCTTY | O_LARGEFILE | O_WRONLY | O_APPEND;
    if(fail_if_not_found == false)
        mode |= O_CREAT;

    int fd = open(_ephemeral_null_terminate(file_path), mode, OPEN_FILE_PERMS);
    int64_t total_written = 0;
    for(; fd != -1 && total_written < buffer_size;) {
        ssize_t bytes_written = write(fd, (unsigned char*) buffer + total_written, (size_t) (buffer_size - total_written));
        if(bytes_written <= 0)
            break;

        total_written += bytes_written;
    }
    bool state = fd >= 0 && total_written == buffer_size;
    return _platform_error_code(state);
}


#include <sys/stat.h>
Platform_Error platform_file_size(const Platform_File* file, isize* size)
{
    struct stat st = {0};
    bool state = false;
    if(file->handle)
        state = fstat(_platform_fd(file), &st) != -1;
    if(size)
        *size = st.st_size;
    return _platform_error_code(state);
}

Platform_Error platform_file_flush(Platform_File* file)
{
    bool state = false;
    if(file->handle)
        state = fsync(_platform_fd(file)) == 0;
    
    return _platform_error_code(state);
}

Platform_Error platform_file_create(Platform_String file_path, bool fail_if_exists)
{   
    int flags = O_WRONLY | O_CREAT | O_LARGEFILE;
    if(fail_if_exists)
        flags |= O_EXCL;
        
    int fd = open(_ephemeral_null_terminate(file_path), flags, OPEN_FILE_PERMS);
    Platform_Error out = _platform_error_code(fd != -1);

    if(fd != -1) close(fd);

    return out;
}

Platform_Error platform_file_remove(Platform_String file_path, bool fail_if_not_found)
{
    bool state = unlink(_ephemeral_null_terminate(file_path)) == 0;
    //if the failiure was because the file doesnt exist its sucess
    //Only it must not have been deleted by this call...
    if(state == false && errno == ENOENT && fail_if_not_found == false)
        state = true;

    return _platform_error_code(state);
}

#include <sys/syscall.h>
#include <linux/fs.h>

Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path, bool replace_exiting)
{
    const char* _new = _ephemeral_null_terminate(new_path);
    const char* _old = _ephemeral_null_terminate(old_path);
    
    bool state = syscall(SYS_renameat2, AT_FDCWD, _old, AT_FDCWD, _new, replace_exiting ? 0 : RENAME_NOREPLACE) == 0;
    // bool state = renameat2(AT_FDCWD, _old, AT_FDCWD, _new, RENAME_NOREPLACE) == 0;
    return _platform_error_code(state);
}

//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path, bool replace_exiting)
{
    const char* to = _ephemeral_null_terminate(copy_to_path);
    const char* from = _ephemeral_null_terminate(copy_from_path);
    size_t _GB = 1 << (30);

    int to_fd = -1;
    int from_fd = -1;
    bool state = true;
    if(state)
    {
        from_fd = open(from, O_RDONLY | O_LARGEFILE, OPEN_FILE_PERMS);
        state = from_fd != -1;
    }

    if(state)
    {
        int flags = O_WRONLY | O_CREAT | O_LARGEFILE;
        if(replace_exiting == false)
            flags |= O_EXCL;

        to_fd = open(to, flags, OPEN_FILE_PERMS);
        state = to_fd != -1;
    }

    while(state)
    {
        ssize_t bytes_copied = copy_file_range(from_fd, NULL, to_fd, NULL, _GB, 0);
        if(bytes_copied == -1)
            state = false;
        //If no more to read stop
        if(bytes_copied == 0)
            break;
    }

    Platform_Error out = _platform_error_code(state); 
    if(from_fd != -1) close(from_fd);
    if(to_fd != -1) close(to_fd);

    return out; 
}

Platform_Error platform_file_resize(Platform_String file_path, int64_t size)
{
    //@NOTE: For some reason truncate64 does not see files that normal open does. 
    //       I am very confused by this. I think it has something to do with relative files.
    // bool state = truncate64(_ephemeral_null_terminate(file_path), size);
    // return _platform_error_code(state);

    int fd = open(_ephemeral_null_terminate(file_path), O_WRONLY | O_LARGEFILE, OPEN_FILE_PERMS);
    bool state = fd != -1;
    if(state)
        state = ftruncate64(fd, size) == 0;

    Platform_Error out = _platform_error_code(state);
    if(fd != -1) close(fd);
    return out;
}

Platform_Error platform_directory_create(Platform_String dir_path, bool fail_if_exists)
{
    bool state = mkdir(_ephemeral_null_terminate(dir_path), OPEN_FILE_PERMS) == 0;
    //If failed because dir exists and we dont care about it then it didnt fail
    if(state == false && errno == EEXIST && fail_if_exists == false)
        state = true;

    return _platform_error_code(state);
}

Platform_Error platform_directory_remove(Platform_String dir_path, bool fail_if_not_found)
{
    bool state = rmdir(_ephemeral_null_terminate(dir_path)) == 0;
    //If failed because dir does not exists and we dont care about it then it didnt fail
    if(state == false && errno == ENOENT && fail_if_not_found == false)
        state = true;

    return _platform_error_code(state);
}

Platform_Error platform_directory_set_current_working(Platform_String new_working_dir)
{
    bool state = chdir(_ephemeral_null_terminate(new_working_dir)) == 0;
    return _platform_error_code(state);
}

Platform_Error platform_directory_get_current_working(void* buffer, int64_t buffer_size, bool* needs_bigger_buffer_or_null)
{
    Platform_Error error = 0;
    if(getcwd((char*) buffer, (size_t) buffer_size) == NULL)
        error = (Platform_Error) errno;
    if(needs_bigger_buffer_or_null)
        *needs_bigger_buffer_or_null = error == ERANGE;
    return error;
}    

static char* g_startup_cwd = NULL;
static char* g_executable_path = NULL;
static void _platform_paths_deinit()
{
    free(g_startup_cwd); g_startup_cwd = NULL;
    free(g_executable_path); g_executable_path = NULL;
}

static void _platform_paths_init()
{
    platform_directory_get_startup_working();
    platform_get_executable_path();
}

const char* platform_directory_get_startup_working()
{
    if(g_startup_cwd)
        g_startup_cwd = getcwd(NULL, 0);
    return g_startup_cwd;
}

const char* platform_get_executable_path()
{
    const int64_t path_size = PATH_MAX*4;
    static char* exe_path = NULL;

    if(exe_path == NULL)
    {
        exe_path = (char*) malloc((size_t) path_size);
        if(exe_path == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(path_size);
            assert(false && "allocation of executable failed!");
        }
        else
        {
            ssize_t count = readlink("/proc/self/exe", exe_path, (size_t) path_size);
            if(count < 0)
                count = 0;
            if(count >= path_size)
                count = path_size - 1;

            exe_path[count] = '\0';
        }
    }

    return exe_path ? exe_path : "";
}

#include <dirent.h> 
#include <stdio.h> 
#include <string.h>
#include <stdarg.h>
typedef struct Dir_Iter {   
    DIR* dir;
    struct dirent entry;
} Dir_Iter;

Platform_Error platform_directory_iter_init(Platform_Directory_Iter* iter, Platform_String directory_path)
{
    platform_directory_iter_deinit(iter);

    bool state = false;
    iter->internal = calloc(1, sizeof(Dir_Iter));
    if(iter->internal) {
        Dir_Iter* it = (Dir_Iter*) iter->internal; 
        it->dir = opendir(_ephemeral_null_terminate(directory_path));
        if(it->dir) {
            iter->index = -1;
            state = true;
        }
    }

    if(state == false)
        platform_directory_iter_deinit(iter);

    return _platform_error_code(state);
}

bool platform_directory_iter_next(Platform_Directory_Iter* iter)
{
    if(iter->internal)
    {
        for(;;) {


            Dir_Iter* it = (Dir_Iter*) iter->internal; 
            struct dirent* ent = readdir(it->dir);
            if(ent) {
                iter->index += 1;
                it->entry = *ent;
                iter->path.data = it->entry.d_name;
                iter->path.count = strlen(it->entry.d_name);
                return true;
            }
        }
    }
    return false;
}

void platform_directory_iter_deinit(Platform_Directory_Iter* iter)
{
    if(iter->internal) {
        Dir_Iter* it = (Dir_Iter*) iter->internal; 
        if(it->dir)
            closedir(it->dir);
        free(it);
    }
    memset(iter, 0, sizeof *iter);
}


//=========================================
// File Watch
//=========================================
#if 0
#include <sys/inotify.h>

#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>

Platform_Error _file_watch_scan_dir_recursive(const char *path, char*** out_dirs, isize* out_count)
{
    Platform_Error error = 0;
    char** dirs = NULL;
    isize dirs_capacity = 0;
    isize dirs_count = 0;
    isize dirs_visited_till = 0;
    for(;;) {
        const char* curr_path = path;
        if(dirs_visited_till > 0)
            curr_path = dirs[dirs_visited_till - 1];

        DIR *dir = opendir(curr_path);

        //if is an error on the first directory report it
        if (dir == NULL) {
            if(dirs_visited_till == 0)
                error = _platform_error_code(false);
        }
        else {
            //Iterate all files and push directories to a list
            for(struct dirent *entry = NULL; entry = readdir(dir); ) 
            {
                char* entry_path = NULL;
                bool is_directory = false;
                //if we got reliable information use it
                if(entry->d_type != DT_UNKNOWN)
                    is_directory = entry->d_type == DT_DIR || entry->d_type == DT_LNK;
                else {
                    //else construct the path and try to get info about the file
                    // through stat. This will fail for recursive symlinks so we
                    // dont need to handle this odd case in any way.
                    asprintf(&entry_path, "%s/%s", curr_path, entry->d_name);

                    struct stat64 buf = {0}; 
                    if(stat64(entry_path, &buf) == 0 && S_ISDIR(buf.st_mode))
                        is_directory = true;
                    else
                        free(entry_path);
                }

                if(is_directory) {
                    if(entry_path == NULL)
                        asprintf(&entry_path, "%s/%s", curr_path, entry->d_name);

                    if(dirs_count >= dirs_capacity) {
                        dirs_capacity = dirs_capacity*3/2 + 8;
                        dirs = realloc(dirs, dirs_capacity*sizeof(*dirs));
                    }

                    dirs[dirs_count++] = entry_path;
                }
            }

            closedir(dir);
        }    
    
        dirs_visited_till++;
        if(dirs_visited_till >= dirs_count)
            break;
    }

    if(out_dirs)
        *out_dirs = dirs;
    if(out_count)
        *out_count = dirs_count;

	return error;
}

typedef struct Platform_File_Watch_State {
    int inotify_fd;
    int32_t flags;
    uint32_t linux_flags;

    int*  watched_fds;
    char** watched_paths;
    isize watched_count;
    isize watched_capacity;
} Platform_File_Watch_State;

Platform_Error _file_watch_recursive_add_inotify(const char *path, Platform_File_Watch_State* state)
{
    Platform_File_Watch_State state = {0};

    Platform_Error error = 0;
    char** dirs = NULL;
    int** watched = NULL;
    isize dirs_capacity = 0;
    isize dirs_count = 0;
    isize dirs_visited_till = 0;
    for(;;) {
        const char* curr_path = path;
        if(dirs_visited_till > 0)
            curr_path = dirs[dirs_visited_till - 1];

        int watch = inotify_add_watch(state->inotify_fd, curr_path, state->linux_flags);

        DIR *dir = opendir(curr_path);
        //if is an error on the first directory report it
        if (dir == NULL) {
            if(dirs_visited_till == 0)
                error = _platform_error_code(false);
        }
        else {
            //Iterate all files and push directories to a list
            for(struct dirent *entry = NULL; entry = readdir(dir); ) 
            {
                char* entry_path = NULL;
                bool is_directory = false;
                //if we got reliable information use it
                if(entry->d_type != DT_UNKNOWN)
                    is_directory = entry->d_type == DT_DIR || entry->d_type == DT_LNK;
                else {
                    //else construct the path and try to get info about the file
                    // through stat. This will fail for recursive symlinks so we
                    // dont need to handle this odd case in any way.
                    asprintf(&entry_path, "%s/%s", curr_path, entry->d_name);

                    struct stat64 buf = {0}; 
                    if(stat64(entry_path, &buf) == 0 && S_ISDIR(buf.st_mode))
                        is_directory = true;
                    else
                        free(entry_path);
                }

                if(is_directory) {
                    if(entry_path == NULL)
                        asprintf(&entry_path, "%s/%s", curr_path, entry->d_name);

                    if(dirs_count >= dirs_capacity) {
                        dirs_capacity = dirs_capacity*3/2 + 8;
                        dirs = realloc(dirs, dirs_capacity*sizeof(*dirs));
                    }

                    dirs[dirs_count++] = entry_path;
                }
            }

            closedir(dir);
        }    
    
        dirs_visited_till++;
        if(dirs_visited_till >= dirs_count)
            break;
    }

    if(out_dirs)
        *out_dirs = dirs;
    if(out_count)
        *out_count = dirs_count;

	return error;
}

void _file_watch_dirs_dealloc(char** dirs, isize count)
{
    for(isize i = 0; i < count; i++)
        free(dirs[i]);
    free(dirs);
}


Platform_Error platform_file_watch_init(Platform_File_Watch* file_watch, int32_t flags, Platform_String path, isize buffer_size)
{
    platform_file_watch_deinit(file_watch);

    int inotify_fd = inotify_init1(IN_NONBLOCK);

    int linux_flags = 0;
    if(flags & PLATFORM_FILE_WATCH_CREATED) linux_flags |= IN_CREATE;
    if(flags & PLATFORM_FILE_WATCH_DELETED) linux_flags |= IN_DELETE;
    if(flags & PLATFORM_FILE_WATCH_MODIFIED) linux_flags |= IN_MODIFY | IN_ATTRIB;
    if(flags & PLATFORM_FILE_WATCH_RENAMED) linux_flags |= IN_MOVED_FROM | IN_MOVED_TO;

    if(flags & PLATFORM_FILE_WATCH_DIRECTORY) linux_flags |= XXX;
    if(flags & PLATFORM_FILE_WATCH_SUBDIRECTORIES) {
        char** dirs_prev = NULL;
        isize dirs_prev_count = 0;
        Platform_Error error = _file_watch_scan_dir_recursive(_ephemeral_null_terminate(path), &dirs_prev, &dirs_prev_count)

    }
    
    
    linux_flags |= XXX;

    inotify_add_watch(inotify_fd, _ephemeral_null_terminate(path), )
}

void platform_file_watch_deinit(Platform_File_Watch* file_watch)
{

}

bool platform_file_watch_poll(Platform_File_Watch* file_watch, Platform_File_Watch_Event* event, Platform_Error* error_or_null)
{

}
#endif

//=========================================
// Debug
//=========================================
#define PLATFORM_CALLSTACKS_MAX 256
#define PLATFORM_CALLSTACK_LINE_LEN 64

#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>

int64_t platform_capture_call_stack(void** stack, int64_t stack_size, int64_t skip_count)
{
    void* stack_ptrs[PLATFORM_CALLSTACKS_MAX] = {0};
    int64_t found_size = backtrace(stack_ptrs, (int) PLATFORM_CALLSTACKS_MAX);
    if(skip_count < 0)
        skip_count = 0;
        
    skip_count += 1; //for this function
    
    int64_t not_skipped_size = found_size - skip_count;
    if(not_skipped_size < 0)
        not_skipped_size = 0;

    memcpy(stack, stack_ptrs + skip_count, (size_t) not_skipped_size*sizeof(void*));
    return not_skipped_size;
}


#define _MIN(a, b)   ((a) < (b) ? (a) : (b))
#define _MAX(a, b)   ((a) > (b) ? (a) : (b))
#define _CLAMP(value, low, high) _MAX((low), _MIN((value), (high)))

void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, void** stack, int64_t stack_size)
{
    assert(stack_size >= 0);
    memset(translated, 0, (size_t) stack_size * sizeof *translated);

    char **semi_translated = backtrace_symbols((void *const *) stack, (int) stack_size);
    if (semi_translated != NULL)
    {
        for (int64_t i = 0; i < stack_size; i++)
        {
            Platform_Stack_Trace_Entry* entry = &translated[i];
            void* frame = (void*) stack[i];
            
            const char* message = semi_translated[i];
            int64_t message_size = (int64_t) strlen(message);

            int64_t function_from = -1;
            int64_t function_to = -1;

            int64_t file_from = 0;
            int64_t file_to = 0;

            for(int64_t i = message_size; i-- > 0; )
            {
                if(message[i] == '+' && function_to == -1)
                    function_to = i;

                if(message[i] == '(' && function_to != -1)
                    function_from = i + 1;
            }

            file_to = function_from - 1;

            function_from = _CLAMP(function_from, 0, message_size);
            function_to = _CLAMP(function_to, 0, message_size);
            file_from = _CLAMP(file_from, 0, message_size);
            file_to = _CLAMP(file_to, 0, message_size);

            entry->line = 0; //@TODO: make work but not through addr2line!
            entry->address = frame;
            memcpy(entry->function, message + function_from, _MIN((size_t) (function_to - function_from), sizeof entry->function));
            memcpy(entry->file,     message + file_from,     _MIN((size_t) (file_to - file_from), sizeof entry->file));
            memcpy(entry->module,   message + file_from,     _MIN((size_t) (file_to - file_from), sizeof entry->module));

            //if everything else failed just use the semi translate message...            
            if(strcmp(entry->function, "") == 0 && strcmp(entry->file, "") == 0)
            {
                size_t function_size = _CLAMP((size_t) message_size, 0, sizeof entry->function);
                memcpy(entry->function, message, function_size);
            }

            //null terminate everything just in case
            entry->module[sizeof entry->module - 1] = '\0';
            entry->file[sizeof entry->file - 1] = '\0';
            entry->function[sizeof entry->function - 1] = '\0';
        }
    }

    free(semi_translated);
}

#include <unistd.h>
#include <stdint.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

//taken from https://stackoverflow.com/a/24419586
int platform_is_debugger_attached()
{
    #if !defined(PTRACE_ATTACH) && defined(PT_ATTACH)
    #  define PTRACE_ATTACH PT_ATTACH
    #endif
    #if !defined(PTRACE_DETACH) && defined(PT_DETACH)
    #  define PTRACE_DETACH PT_DETACH
    #endif

    int from_child[2] = {-1, -1};
    if (pipe(from_child) < 0) {
        fprintf(stderr, "Debugger check failed: Error opening internal pipe: %s", strerror(errno));
        return -1;
    }

    int pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Debugger check failed: Error forking: %s", strerror(errno));
        return -1;
    }

    //child
    if (pid == 0) {
        int ppid = getppid();

        // Close parent's side
        close(from_child[0]);

        if (ptrace(PTRACE_ATTACH, ppid, 0, 0) == 0) {
            // Wait for the parent to stop
            waitpid(ppid, NULL, 0);

            // Tell the parent what happened
            uint8_t ret = 0;
            write(from_child[1], &ret, sizeof(ret));

            // Detach
            ptrace(PTRACE_DETACH, ppid, 0, 0);
            exit(0);
        }
        else {
            //Tell the parent what happened
            uint8_t ret = 1;
            write(from_child[1], &ret, sizeof(ret));

            exit(0);
        }
    // Parent
    } else {
        uint8_t ret = -1;
        while ((read(from_child[0], &ret, sizeof(ret)) < 0) && (errno == EINTR));

        // Ret not updated 
        if (ret < 0) 
            fprintf(stderr, "Debugger check failed: Error getting status from child: %s", strerror(errno));

        // Close the pipes here, to avoid races with pattach (if we did it above) 
        close(from_child[1]);
        close(from_child[0]);

        // Collect the status of the child 
        waitpid(pid, NULL, 0);
        return ret;
    }
}

#include <signal.h>
#include <setjmp.h>

#define PLATFORM_SANDBOXES_MAX 256
#define PLATFORM_SANDBOXE_JUMP_CODE 0x78626473 //sdbx

typedef struct Signal_Handler_State {
    sigjmp_buf jump_buffer;
    int signal;

    int32_t stack_size;
    void* stack[PLATFORM_CALLSTACKS_MAX];

    int64_t perf_counter;
    int64_t epoch_time;
} Signal_Handler_State;

static _Thread_local Signal_Handler_State* t_platform_sighandle_state = NULL;

void platform_sighandler(int sig, struct sigcontext ctx) 
{
    (void) ctx;

    Signal_Handler_State* handler = t_platform_sighandle_state;
    if(handler) {
        handler->perf_counter = platform_perf_counter();
        handler->perf_counter = platform_epoch_time();
        handler->stack_size = (int32_t) platform_capture_call_stack(handler->stack, PLATFORM_CALLSTACKS_MAX, 1);
        handler->signal = sig;
        siglongjmp(handler->jump_buffer, PLATFORM_SANDBOXE_JUMP_CODE);
    }
}

bool platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context, Platform_Exception* error_or_null)
{
    typedef struct {
        int signal;
        const char* error_string;
        struct sigaction action;
        struct sigaction prev_action;
    } Signal_Error;

    Signal_Error error_handlers[] = {
        {SIGABRT, "abort"},                //P1990      Core    Abort signal from abort(3)
        //{SIGALRM, "other"},              //P1990      Term    Timer signal from alarm(2)
        {SIGBUS, "access violation"},      //P2001      Core    Bus error (bad memory access)
        //{SIGCHLD, "other"},              //P1990      Ign     Child stopped or terminated 
        //{SIGCLD, "other"},               //  -        Ign     A synonym for SIGCHLD 
        //{SIGCONT, "other"},              //P1990      Cont    Continue if stopped 
        //{SIGEMT, "other"},               //  -        Term    Emulator trap 
        {SIGFPE, "floating point "},       //P1990      Core    Floating-point exception
        {SIGHUP, "hangup"},                //P1990      Term    Hangup detected on controlling terminal or death of controlling process
        {SIGILL, "illegal instruction"},   //P1990      Core    Illegal Instruction
        //{SIGINFO, "other"},              //  -                A synonym for SIGPWR
        //{SIGINT, "other"},               //P1990      Term    Interrupt from keyboard
        //{SIGIO, "other"},                //  -        Term    I/O now possible (4.2BSD)
        {SIGIOT, "abort"},                 //  -        Core    IOT trap. A synonym for SIGABRT
        //{SIGKILL, "other"},              //P1990      Term    Kill signal
        //{SIGLOST, "other"},              //  -        Term    File lock lost (unused)
        //{SIGPIPE, "other"},              //P1990      Term    Broken pipe: write to pipe with no readers; see pipe(7)
        //{SIGPOLL, "other"},              //P2001      Term    Pollable event (Sys V); synonym for SIGIO
        //{SIGPROF, "other"},              //P2001      Term    Profiling timer expired
        {SIGPWR, "other"},                 //  -        Term    Power failure (System V)
        //{SIGQUIT, "other"},              //P1990      Core    Quit from keyboard
        {SIGSEGV, "access violation"},     //P1990      Core    Invalid memory reference
        {SIGSTKFLT, "access violation"},   //  -        Term    Stack fault on coprocessor (unused)
        //{SIGSTOP, "other"},              //P1990      Stop    Stop process
        //{SIGTSTP, "other"},              //P1990      Stop    Stop typed at terminal
        {SIGSYS, "other"},                 //P2001      Core    Bad system call (SVr4); see also seccomp(2)
        {SIGTERM, "terminate"},            //P1990      Term    Termination signal
        {SIGTRAP, "breakpoint"},           //P2001      Core    Trace/breakpoint trap
        //{SIGTTIN, "other"},              //P1990      Stop    Terminal input for background process
        //{SIGTTOU, "other"},              //P1990      Stop    Terminal output for background process
        //{SIGUNUSED, "other"},            //  -        Core    Synonymous with SIGSYS
        //{SIGURG, "other"},               //P2001      Ign     Urgent condition on socket (4.2BSD)
        //{SIGUSR1, "other"},              //P1990      Term    User-defined signal 1
        //{SIGUSR2, "other"},              //P1990      Term    User-defined signal 2
        //{SIGVTALRM, "other"},            //P2001      Term    Virtual alarm clock (4.2BSD)
        //{SIGXCPU, "other"},              //P2001      Core    CPU time limit exceeded (4.2BSD); see setrlimit(2)
        //{SIGXFSZ, "other"},              //P2001      Core    File size limit exceeded (4.2BSD); see setrlimit(2)
        //{SIGWINCH, "other"},             //  -        Ign     Window resize signal (4.3BSD, Sun)
    };

    const int64_t handler_count = (int64_t) sizeof(error_handlers) / (int64_t) sizeof(Signal_Error);
    for(int64_t i = 0; i < handler_count; i++)
    {
        Signal_Error* sig_error = &error_handlers[i];
        sig_error->action.sa_handler = (void(*)(int)) (void *)platform_sighandler;
        sigemptyset(&sig_error->action.sa_mask);
        sigaddset(&sig_error->action.sa_mask, (int) SA_NOCLDSTOP);

        bool state = sigaction(sig_error->signal, &sig_error->action, &sig_error->prev_action) == 0;
        assert(state && "bad signal specifier!");
    }

    bool is_okay = false;
    Signal_Handler_State* state = calloc(1, sizeof(Signal_Handler_State));
    if(state != NULL)
    {
        Signal_Handler_State* prev_state = t_platform_sighandle_state;
        t_platform_sighandle_state = state;
        switch(sigsetjmp(state->jump_buffer, 0))
        {
            case 0: {
                sandboxed_func(sandbox_context);
                is_okay = true;
            } break;

            case PLATFORM_SANDBOXE_JUMP_CODE: {
                is_okay = false;
                const char* execption_text = "other";
                for(int64_t i = 0; i < handler_count; i++)
                    if(error_handlers[i].signal == state->signal)
                        execption_text = error_handlers[i].error_string;
                
                Platform_Exception exception = {0};
                exception.exception = execption_text;
                exception.call_stack = (void **) (void*) state->stack;
                exception.call_stack_size = state->stack_size;
                exception.epoch_time = state->epoch_time;
                if(error_or_null)
                    *error_or_null = exception;
            } break;

            default: {
                assert(false && "unexpected jump occurred!");
            } break;
        }

        t_platform_sighandle_state = prev_state;
    }
    free(state);

    for(int64_t i = 0; i < handler_count; i++)
    {
        Signal_Error* sig_error = &error_handlers[i];
        bool state = sigaction(sig_error->signal, &sig_error->prev_action, NULL) == 0;
        assert(state && "bad signal specifier");
    }

    return is_okay;
}

void platform_init()
{
    platform_perf_counter_startup();
    platform_epoch_time_startup();
    platform_perf_counter_startup();
}

void platform_deinit()
{
    _platform_perf_counters_deinit();
}

// #endif