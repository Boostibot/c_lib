#define __USE_LARGEFILE64
#define __USE_GNU
#define _GNU_SOURCE
#define _GNU_SOURCE_
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64 

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

typedef struct Platform_State {
    int64_t startup_perf_counter;
    int64_t startup_epoch_time;
} Platform_State;

Platform_State gp_state = {0};

static int64_t untested_stack_overflow_calendar_to_time_t(int64_t sec, int64_t min, int64_t hour, int64_t day, int64_t month, int64_t year);
static int64_t platform_startup_local_epoch_time();

void platform_init(Platform_Allocator* alloc_or_null)
{
    platform_perf_counter_startup();
    platform_epoch_time_startup();
    platform_perf_counter_startup();
}

void platform_deinit()
{
    memset(&gp_state, 0, sizeof gp_state);
}

void platform_set_internal_allocator(Platform_Allocator allocator)
{
    (void) allocator;
}

void* _platform_internal_reallocate(int64_t new_size, void* old_ptr)
{
    assert(new_size >= 0);
    if(new_size == 0)
    {
        free(old_ptr);
        return NULL;
    }
    else
    {
        void* out = realloc(old_ptr, (size_t) new_size);
        if(out == NULL)
            errno = ENOMEM;

        return out;
    }
}

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

void print_errno(int errno_val)
{
    const char* name = "None";
    #define X(ERRNO_CODE) \
        if(errno_val == ERRNO_CODE) name = #ERRNO_CODE;

    POSIX_ERRNO_CODES_X()
    printf("errno %s: %s\n", name, strerror(errno_val));
    #undef X
}

#include <sys/mman.h>
void* platform_virtual_reallocate(void* allocate_at, int64_t bytes, Platform_Virtual_Allocation action, Platform_Memory_Protection protection)
{
    void* out = NULL;
    bool had_action = false;
    if(action & PLATFORM_VIRTUAL_ALLOC_RESERVE)   
    {
        had_action = true;
        out = mmap(allocate_at, (size_t) bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if(out == MAP_FAILED)
        {
            print_errno(errno);
            out = NULL;
        }
    }
    else if(action & PLATFORM_VIRTUAL_ALLOC_RELEASE)
    {
        had_action = true;
        munmap(allocate_at, (size_t) bytes);
    }

    if(action & PLATFORM_VIRTUAL_ALLOC_COMMIT)
    {
        if(action & PLATFORM_VIRTUAL_ALLOC_RESERVE)
            allocate_at = out;

        if(allocate_at) 
        {
            had_action = true;
            int prot = PROT_NONE;
            if(protection & PLATFORM_MEMORY_PROT_READ) prot |= PROT_READ;
            if(protection & PLATFORM_MEMORY_PROT_WRITE) prot |= PROT_WRITE;
            if(protection & PLATFORM_MEMORY_PROT_EXECUTE) prot |= PROT_EXEC;

            mprotect(allocate_at, (size_t) bytes, prot);
            madvise(allocate_at, (size_t) bytes, MADV_WILLNEED);
            out = allocate_at;
        }
    }
    if(action & PLATFORM_VIRTUAL_ALLOC_DECOMMIT && had_action == false)
    {
        had_action = true;
        mprotect(allocate_at, (size_t) bytes, PROT_NONE);
        madvise(allocate_at, (size_t) bytes, MADV_DONTNEED);
        out = allocate_at;
    }

    if(had_action == false)
        printf("Strange combination of arguments (commit and decommit)\n");

    return out;
}

#include <unistd.h>
int64_t platform_page_size()
{
    return (int64_t) getpagesize();
}

//=========================================
// Threading
//=========================================
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

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

//typedef struct Platform_Thread {
//    void* handle;
//    int32_t id;
//} Platform_Thread;
//
//typedef struct Platform_Mutex {
//    void* handle;
//} Platform_Mutex;

int64_t         platform_thread_get_proccessor_count()
{
    cpu_set_t cs;
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);
    return CPU_COUNT(&cs);
}

//initializes a new thread and immedietely starts it with the func function.
//The thread has stack_size_or_zero bytes of stack sizes rounded up to page size
//If stack_size_or_zero is zero or lower uses system default stack size.

typedef struct Platform_Pthread_State {
    pthread_t thread;
    void (*func)(void*);
    void* context;
} Platform_Pthread_State;

typedef struct Platform_Mutex_State {
    pthread_mutex_t mutex;
} Platform_Mutex_State;

void _pthread_cleanup_routine(void* arg)
{
    assert(arg != NULL);
    Platform_Pthread_State* thread_state = (Platform_Pthread_State*) arg;
    _platform_internal_reallocate(0, thread_state);
}

void* _platform_pthread_start_routine(void* arg)
{
    assert(arg != NULL);

    //Sandly broken on WSL
    // pthread_cleanup_push(_pthread_cleanup_routine, arg);
    Platform_Pthread_State* thread_state = (Platform_Pthread_State*) arg;
    thread_state->func(thread_state->context);
    _platform_internal_reallocate(0, thread_state);
    return NULL;
}

// #if 0
Platform_Error platform_thread_launch(Platform_Thread* thread, void (*func)(void*), void* context, int64_t stack_size_or_zero)
{
    bool state = true;
    pthread_attr_t attr = {0};
    pthread_attr_t* attr_ptr = {0};

    Platform_Pthread_State* thread_state = NULL;
    if(stack_size_or_zero > 0)
    {
        attr_ptr = &attr;
        pthread_attr_init(&attr);
        state = state && 0 == pthread_attr_setstacksize(&attr, (size_t) stack_size_or_zero);
    }
    
    if(state)
    {
        thread_state = (Platform_Pthread_State*) _platform_internal_reallocate(sizeof(Platform_Pthread_State), NULL);
        state = thread_state != NULL;
        if(state)
        {
            memset(thread_state, 0, sizeof *thread_state);

            thread_state->func = func;
            thread_state->context = context;
            if(pthread_create(&thread_state->thread, attr_ptr, _platform_pthread_start_routine, thread_state) != 0)
            {
                state = false;
                _platform_internal_reallocate(0, thread_state);
            }

        }
    }

    if(stack_size_or_zero > 0)
        pthread_attr_destroy(&attr);

    if(state && thread_state && thread)
    {
        thread->handle = thread_state;
        thread->id = 0;
    }

    return _platform_error_code(state);
}

void platform_thread_sleep(int64_t ms)
{
    if(ms > 10)
        sleep((unsigned) ms);
    else
    {
        struct timespec req = {0};
        req.tv_nsec = ms*1000;
        nanosleep(&req, NULL);
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

void platform_thread_detach(Platform_Thread thread)
{
    Platform_Pthread_State* thread_state = (Platform_Pthread_State*) thread.handle;
    bool state = pthread_detach(thread_state->thread) == 0;
    (void) state;
    // return _platform_error_code(state);
}

void platform_thread_join(const Platform_Thread* threads, int64_t count) 
{
    Platform_Error last_error = _platform_error_code(true);
    for(int64_t i = 0; i < count; i++)
    {
        Platform_Pthread_State* thread_state = (Platform_Pthread_State*) threads[i].handle;
        if(pthread_join(thread_state->thread, NULL) != 0)
            last_error = _platform_error_code(false);
    }
    (void) last_error;
    // return last_error;
}

Platform_Error platform_mutex_init(Platform_Mutex* mutex)
{
    platform_mutex_deinit(mutex);
    bool state = true;
    Platform_Mutex_State* mutex_state = (Platform_Mutex_State*) _platform_internal_reallocate(sizeof(Platform_Mutex_State), NULL);

    state = mutex_state != NULL;
    if(state)
        state = pthread_mutex_init(&mutex_state->mutex, NULL) == 0;

    Platform_Error error = _platform_error_code(state);
    mutex->handle = mutex_state;
    if(!state)
        platform_mutex_deinit(mutex);

    return error;
}
void platform_mutex_deinit(Platform_Mutex* mutex)
{
    Platform_Mutex_State* mutex_state = (Platform_Mutex_State*) mutex->handle;
    if(mutex_state)
    {
        pthread_mutex_destroy(&mutex_state->mutex);
        _platform_internal_reallocate(0, mutex_state);
    }

    memset(mutex, 0, sizeof *mutex);
}

void platform_mutex_lock(Platform_Mutex* mutex)
{
    bool state = false;
    Platform_Mutex_State* mutex_state = (Platform_Mutex_State*) mutex->handle;
    if(mutex_state)
        state = pthread_mutex_lock(&mutex_state->mutex) == 0; 
    (void) state;
    // return _platform_error_code(state);
}

void platform_mutex_unlock(Platform_Mutex* mutex)
{
    bool state = false;
    Platform_Mutex_State* mutex_state = (Platform_Mutex_State*) mutex->handle;
    if(mutex_state)
        state = pthread_mutex_unlock(&mutex_state->mutex) == 0;
    (void) state;
    // return _platform_error_code(state);
}

//=========================================
// Timings 
//=========================================

#define _SECOND_MILLISECONDS    ((int64_t) 1000LL)
#define _SECOND_MICROSECS       ((int64_t) 1000000LL)
#define _SECOND_NANOSECS        ((int64_t) 1000000000LL)
#define _MINUTE_SECS            ((int64_t) 60)

typedef long long lli;

int64_t platform_perf_counter()
{
    struct timespec ts = {0};
    (void) clock_gettime(CLOCK_MONOTONIC_RAW , &ts);
    return (int64_t) ts.tv_nsec + ts.tv_sec * _SECOND_NANOSECS;
}

int64_t platform_perf_counter_frequency()
{
	return _SECOND_NANOSECS;
}

int64_t platform_perf_counter_startup()
{
    if(gp_state.startup_perf_counter == 0)
        gp_state.startup_perf_counter = platform_perf_counter();

    return gp_state.startup_perf_counter;
}

int64_t platform_epoch_time()
{
    struct timespec ts = {0};
    (void) clock_gettime(CLOCK_REALTIME , &ts);
    return (int64_t) ts.tv_nsec/1000 + ts.tv_sec * _SECOND_MICROSECS;
}

int64_t platform_epoch_time_startup()
{
    if(gp_state.startup_epoch_time == 0)
        gp_state.startup_epoch_time = platform_epoch_time();

    return gp_state.startup_epoch_time;
}

//=========================================
// Filesystem
//=========================================

//@TODO: free allocated!
#define PLATFORM_PRINT_OUT_OF_MEMORY(ammount) \
    fprintf(stderr, "platform allocation failed while calling function %s! not enough memory! requested: %lli", (__FUNCTION__), (lli) (ammount))

const char* platform_null_terminate(Platform_String string)
{
    if(string.data == NULL || string.size <= 0)
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
        const char* potential_null_termination = string.data + string.size;
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

    static char* strings[MAX_COPIED_SIMULATENOUS] = {0};
    static int64_t string_sizes[MAX_COPIED_SIMULATENOUS] = {0};
    static int64_t string_slot = 0;

    const char* out_string = "";
    char** curr_data = &strings[string_slot];
    int64_t* curr_size = &string_sizes[string_slot];
    string_slot = (string_slot + 1) % MAX_COPIED_SIMULATENOUS;

    bool had_error = false;
    //If we need a bigger buffer OR the previous allocation was too big and the new one isnt
    if(*curr_size <= string.size || (*curr_size > MAX_COPIED_SIZE && string.size <= MAX_COPIED_SIZE))
    {
        int64_t alloc_size = string.size + 1;
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
        memmove(*curr_data, string.data, (size_t) string.size);
        (*curr_data)[string.size] = '\0';
        out_string = *curr_data;
    }

    return out_string;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


int64_t platform_epoch_time_from_time_t(time_t time)
{
    return (int64_t) time * _SECOND_MICROSECS;
}

Platform_Error platform_file_info(Platform_String file_path, Platform_File_Info* info_or_null)
{
    struct stat buf = {0};
    bool state = stat(platform_null_terminate(file_path), &buf) == 0;
    if(state && info_or_null != NULL)
    {
        memset(info_or_null, 0, sizeof *info_or_null);
        info_or_null->size = buf.st_size;
        info_or_null->created_epoch_time = platform_epoch_time_from_time_t(buf.st_ctime);
        info_or_null->last_write_epoch_time = platform_epoch_time_from_time_t(buf.st_mtime);
        info_or_null->last_access_epoch_time = platform_epoch_time_from_time_t(buf.st_atime);

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
Platform_Error platform_file_open(Platform_File* file, Platform_String path, int open_flags)
{
    platform_file_close(file);

    int mode = 0;
    if((open_flags & PLATFORM_FILE_MODE_WRITE) && (open_flags & PLATFORM_FILE_MODE_READ))
        mode |= O_RDWR;
    else if(open_flags & PLATFORM_FILE_MODE_READ)
        mode |= O_RDONLY;
    else if(open_flags & PLATFORM_FILE_MODE_WRITE)
        mode |= O_WRONLY;

    if(open_flags & PLATFORM_FILE_MODE_CREATE_MUST_NOT_EXIST)
        mode |= O_EXCL;
    else if(open_flags & PLATFORM_FILE_MODE_CREATE)
        mode |= O_CREAT;

    if(open_flags & PLATFORM_FILE_MODE_REMOVE_CONTENT)
        mode |= O_TRUNC;
        
    if(open_flags & PLATFORM_FILE_MODE_TEMPORARY)
        mode |= O_TMPFILE;

    // #ifndef O_LARGEFILE
    //     #define O_LARGEFILE 0
    // #endif

    int fd = open(platform_null_terminate(path), mode | O_LARGEFILE, OPEN_FILE_PERMS);
    bool state = fd != -1;
    if(state)
    {
        file->handle.linux = fd;
        file->is_open = true;
    }

    return _platform_error_code(state);
}

Platform_Error platform_file_close(Platform_File* file)
{
    bool state = true;
    if(file->is_open)
        state = close(file->handle.linux) == 0;

    memset(file, 0, sizeof *file);
    return _platform_error_code(state);
}


Platform_Error platform_file_read(Platform_File* file, void* buffer, int64_t size, bool* eof_or_null)
{
    bool state = true;
    bool eof = false;
    if(file->is_open)
    {
        for(int64_t total_read = 0; total_read < size;)
        {
            ssize_t bytes_read = read(file->handle.linux, (unsigned char*)buffer + total_read, (size_t) (size - total_read));
            if(bytes_read == -1)
            {
                state = false;
                break;
            }

            if(bytes_read == 0)
            {
                eof = true;
                break;
            }

            total_read += bytes_read;
        }
    }

    if(eof_or_null)
        *eof_or_null = eof;

    return _platform_error_code(state);
}

Platform_Error platform_file_write(Platform_File* file, const void* buffer, int64_t size)
{
    bool state = true;
    if(file->is_open)
    {
        for(int64_t total_written = 0; total_written < size;)
        {
            ssize_t bytes_written = write(file->handle.linux, (unsigned char*) buffer + total_written, (size_t) (size - total_written));
            if(bytes_written <= 0)
            {
                state = false;
                break;
            }

            total_written += bytes_written;
        }
    }

    return _platform_error_code(state);

}
//Obtains the current offset from the start of the file and saves it into offset. Does not modify the file 
Platform_Error platform_file_tell(Platform_File file, int64_t* offset_ptr)
{
    bool state = true;
    loff_t offset = 0;
    if(file.is_open)
    {
        offset = lseek64(file.handle.linux, 0, SEEK_CUR);
        if(offset == -1)
        {
            state = false;
            offset = 0;
        }
    }
    if(offset_ptr) 
        *offset_ptr = offset;

    return _platform_error_code(state);
}
//Offset the current file position relative to: start of the file (0 value), current possition, end of the file
Platform_Error platform_file_seek(Platform_File* file, int64_t offset, Platform_File_Seek from)
{
    bool state = true;
    if(file->is_open)
    {
        int from_linux = SEEK_SET;
        if(from == PLATFORM_FILE_SEEK_FROM_START)
            from_linux = SEEK_SET;
        else if(from == PLATFORM_FILE_SEEK_FROM_CURRENT)
            from_linux = SEEK_CUR;
        else if(from == PLATFORM_FILE_SEEK_FROM_END)
            from_linux = SEEK_END;
        else
            assert(false && "bad Platform_File_Seek given");

        state = lseek64(file->handle.linux, (loff_t) offset, from_linux) != -1;
    }
    return _platform_error_code(state);
}

Platform_Error platform_file_flush(Platform_File* file)
{
    bool state = true;
    if(file->is_open)
        state = fsync(file->handle.linux) == 0;
    
    return _platform_error_code(state);
}

Platform_Error platform_file_create(Platform_String file_path, bool* was_just_created)
{   
    int fd = open(platform_null_terminate(file_path), O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE, OPEN_FILE_PERMS);
    bool was_just_created_ = true;
    bool state = fd != -1;

    if(state == false)
    {
        //if the failiure was because the file already exists it was no failiure at all!
        //Only it must not have been created by this call...
        if(errno == EEXIST)
        {
            state = true;
            was_just_created_ = false;
        }
    }
    else
        close(fd);


    if(was_just_created)
        *was_just_created = was_just_created_ && state;

    return _platform_error_code(state);
}

Platform_Error platform_file_remove(Platform_String file_path, bool* was_just_deleted)
{
    bool state = unlink(platform_null_terminate(file_path)) == 0;
    bool was_just_deleted_ = true;
    if(state == false)
    {
        //if the failiure was because the file doesnt exist its sucess
        //Only it must not have been deleted by this call...
        if(errno == ENOENT)
        {
            state = true;
            was_just_deleted_ = false;
        }
    }

    if(was_just_deleted)
        *was_just_deleted = was_just_deleted_ && state;

    return _platform_error_code(state);
}

#include <sys/syscall.h>
#include <linux/fs.h>

Platform_Error platform_file_move(Platform_String new_path, Platform_String old_path)
{
    const char* _new = platform_null_terminate(new_path);
    const char* _old = platform_null_terminate(old_path);
    
    bool state = syscall(SYS_renameat2, AT_FDCWD, _old, AT_FDCWD, _new, RENAME_NOREPLACE)== 0;
    // bool state = renameat2(AT_FDCWD, _old, AT_FDCWD, _new, RENAME_NOREPLACE) == 0;
    return _platform_error_code(state);
}

//Copies a file. If the file cannot be found or copy_to_path file that already exists, fails.
Platform_Error platform_file_copy(Platform_String copy_to_path, Platform_String copy_from_path)
{
    const char* to = platform_null_terminate(copy_to_path);
    const char* from = platform_null_terminate(copy_from_path);
    size_t GB = 1 << (30);

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
        to_fd = open(to, O_WRONLY | O_CREAT | O_LARGEFILE, OPEN_FILE_PERMS);
        state = to_fd != -1;
    }

    while(state)
    {
        ssize_t bytes_copied = copy_file_range(from_fd, NULL, to_fd, NULL, GB, 0);
        if(bytes_copied == -1)
            state = false;
        //If no more to read stop
        if(bytes_copied == 0)
            break;
    }

    return _platform_error_code(state); 

}
//Resizes a file. The file must exist.
Platform_Error platform_file_resize(Platform_String file_path, int64_t size)
{
    bool state = truncate64(platform_null_terminate(file_path), size);
    return _platform_error_code(state);
}

Platform_Error platform_directory_create(Platform_String dir_path, bool* was_just_created_or_null)
{
    bool state = mkdir(platform_null_terminate(dir_path), OPEN_FILE_PERMS) == 0;
    return _platform_error_code(state);
}

Platform_Error platform_directory_remove(Platform_String dir_path, bool* was_just_deleted_or_null)
{
    bool state = rmdir(platform_null_terminate(dir_path)) == 0;
    return _platform_error_code(state);
}

Platform_Error platform_directory_set_current_working(Platform_String new_working_dir)
{
    bool state = chdir(platform_null_terminate(new_working_dir)) == 0;
    return _platform_error_code(state);
}
const char* platform_directory_get_current_working()
{
    static char* dir_path = NULL;
    static int64_t dir_path_size = 0;

    for(int i = 0; i < 24; i++)
    {
        if(dir_path_size > 0 && dir_path != NULL)
        {
            //if success
            if(getcwd(dir_path, (size_t) dir_path_size) != NULL)
                break;
            //if error was caused by something else then small buffer
            //returne error
            if(errno != ERANGE)
                dir_path[0] = '\0';
            //Else continue reallocating
            else
            {}
        }

        dir_path_size *= 2;
        if(dir_path_size < 256)
            dir_path_size = 256;
            
        void* realloced_to = realloc(dir_path, (size_t) dir_path_size);
        if(realloced_to == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(dir_path_size);
            dir_path_size = 0;
            break;
        }

        dir_path = (char*) realloced_to;
    }

    if(dir_path)
        return dir_path;
    else    
        return "";
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

const char* platform_translate_error(Platform_Error error)
{
    switch(error)
    {
        default:
        case PLATFORM_ERROR_OK:
            return strerror((int) error);

        case PLATFORM_ERROR_OTHER:
            return "Other platform specific error occured";
    }
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

void* platform_heap_reallocate(int64_t new_size, void* old_ptr, int64_t align)
{
    if(align <= (int64_t) sizeof(long long int))
    {
        if(new_size <= 0)
        {
            free(old_ptr);
            return NULL;
        }

        return realloc(old_ptr, (size_t) new_size);
    }
    else
    {
        void* out = NULL;
        if(new_size > 0)
        {
            if(posix_memalign(&out, (size_t) align, (size_t) new_size) != 0)
                out = NULL;

            if(out != NULL && old_ptr != NULL)
            {
                int64_t min_size = (int64_t) malloc_usable_size(old_ptr);
                if(min_size > new_size)
                    min_size = new_size;

                memcpy(out, old_ptr, (size_t) min_size);
            }
        }

        if(old_ptr)
            free(old_ptr);

        return out;
    }
}


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
    static void* stack_ptrs[PLATFORM_CALLSTACKS_MAX] = {0};
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

void platform_translate_call_stack(Platform_Stack_Trace_Entry* translated, const void* const* stack, int64_t stack_size)
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

#include <signal.h>
#include <setjmp.h>

#define PLATFORM_SANDBOXES_MAX 256
#define PLATFORM_SANDBOXE_JUMP_CODE 0x123

//An X macro collection of all signals with some commented out that we are not handling. 
//If you want to enable/disable comment/uncomment additional lines.
//Taken from: https://man7.org/linux/man-pages/man7/signal.7.html
//For info on X macros: https://en.wikipedia.org/wiki/X_macro 
#define SIGNAL_ACTION_X \
    X(SIGABRT, PLATFORM_EXCEPTION_ABORT)                /*P1990      Core    Abort signal from abort(3) */ \
    /*X(SIGALRM, PLATFORM_EXCEPTION_OTHER)                P1990      Term    Timer signal from alarm(2) */ \
    X(SIGBUS, PLATFORM_EXCEPTION_ACCESS_VIOLATION)      /*P2001      Core    Bus error (bad memory access) */ \
    /*X(SIGCHLD, PLATFORM_EXCEPTION_OTHER)                P1990      Ign     Child stopped or terminated  */ \
    /*X(SIGCLD, PLATFORM_EXCEPTION_OTHER)                   -        Ign     A synonym for SIGCHLD  */ \
    /*X(SIGCONT, PLATFORM_EXCEPTION_OTHER)                P1990      Cont    Continue if stopped  */ \
    /*X(SIGEMT, PLATFORM_EXCEPTION_OTHER)                   -        Term    Emulator trap  */ \
    X(SIGFPE, PLATFORM_EXCEPTION_FLOAT_OTHER)           /*P1990      Core    Floating-point exception */ \
    X(SIGHUP, PLATFORM_EXCEPTION_OTHER)                 /*P1990      Term    Hangup detected on controlling terminal or death of controlling process */ \
    X(SIGILL, PLATFORM_EXCEPTION_ILLEGAL_INSTRUCTION)   /*P1990      Core    Illegal Instruction */ \
    /*X(SIGINFO, PLATFORM_EXCEPTION_OTHER)                  -                A synonym for SIGPWR */ \
    /*X(SIGINT, PLATFORM_EXCEPTION_OTHER)                 P1990      Term    Interrupt from keyboard */ \
    /*X(SIGIO, PLATFORM_EXCEPTION_OTHER)                    -        Term    I/O now possible (4.2BSD) */ \
    X(SIGIOT, PLATFORM_EXCEPTION_ABORT)                 /*  -        Core    IOT trap. A synonym for SIGABRT */ \
    /*X(SIGKILL, PLATFORM_EXCEPTION_OTHER)                P1990      Term    Kill signal */ \
    /*X(SIGLOST, PLATFORM_EXCEPTION_OTHER)                  -        Term    File lock lost (unused) */ \
    /*X(SIGPIPE, PLATFORM_EXCEPTION_OTHER)                P1990      Term    Broken pipe: write to pipe with no readers; see pipe(7) */ \
    /*X(SIGPOLL, PLATFORM_EXCEPTION_OTHER)                P2001      Term    Pollable event (Sys V); synonym for SIGIO */ \
    /*X(SIGPROF, PLATFORM_EXCEPTION_OTHER)                P2001      Term    Profiling timer expired */ \
    X(SIGPWR, PLATFORM_EXCEPTION_OTHER)                 /*  -        Term    Power failure (System V) */ \
    /*X(SIGQUIT, PLATFORM_EXCEPTION_OTHER)               P1990      Core    Quit from keyboard */ \
    X(SIGSEGV, PLATFORM_EXCEPTION_ACCESS_VIOLATION)     /*P1990      Core    Invalid memory reference */ \
    X(SIGSTKFLT, PLATFORM_EXCEPTION_ACCESS_VIOLATION)   /*  -        Term    Stack fault on coprocessor (unused) */ \
    /*X(SIGSTOP, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Stop process */ \
    /*X(SIGTSTP, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Stop typed at terminal */ \
    X(SIGSYS, PLATFORM_EXCEPTION_OTHER)                 /*P2001      Core    Bad system call (SVr4); see also seccomp(2) */ \
    X(SIGTERM, PLATFORM_EXCEPTION_TERMINATE)            /*P1990      Term    Termination signal */ \
    X(SIGTRAP, PLATFORM_EXCEPTION_BREAKPOINT)           /*P2001      Core    Trace/breakpoint trap */ \
    /*X(SIGTTIN, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Terminal input for background process */ \
    /*X(SIGTTOU, PLATFORM_EXCEPTION_OTHER)                P1990      Stop    Terminal output for background process */ \
    /*X(SIGUNUSED, PLATFORM_EXCEPTION_OTHER)                -        Core    Synonymous with SIGSYS */ \
    /*X(SIGURG, PLATFORM_EXCEPTION_OTHER)                 P2001      Ign     Urgent condition on socket (4.2BSD) */ \
    /*X(SIGUSR1, PLATFORM_EXCEPTION_OTHER)                P1990      Term    User-defined signal 1 */ \
    /*X(SIGUSR2, PLATFORM_EXCEPTION_OTHER)                P1990      Term    User-defined signal 2 */ \
    /*X(SIGVTALRM, PLATFORM_EXCEPTION_OTHER)              P2001      Term    Virtual alarm clock (4.2BSD) */ \
    /*X(SIGXCPU, PLATFORM_EXCEPTION_OTHER)                P2001      Core    CPU time limit exceeded (4.2BSD); see setrlimit(2) */ \
    /*X(SIGXFSZ, PLATFORM_EXCEPTION_OTHER)                P2001      Core    File size limit exceeded (4.2BSD); see setrlimit(2) */ \
    /*X(SIGWINCH, PLATFORM_EXCEPTION_OTHER)                 -        Ign     Window resize signal (4.3BSD, Sun) */ \

typedef struct Signal_Handler_State {
    sigjmp_buf jump_buffer;
    int signal;

    int32_t stack_size;
    void* stack[PLATFORM_CALLSTACKS_MAX];

    int64_t perf_counter;
    int64_t epoch_time;
} Signal_Handler_State;

static __thread Signal_Handler_State* platform_signal_handler_queue = NULL;
static __thread int64_t platform_signal_handler_i1 = 0;

void platform_sighandler(int sig, struct sigcontext ctx) 
{
    (void) ctx;

    int64_t my_index_i1 = platform_signal_handler_i1;
    if(my_index_i1 >= 1)
    {
        //@TODO: add more specific flag testing!
        Signal_Handler_State* handler = &platform_signal_handler_queue[my_index_i1-1];
        handler->perf_counter = platform_perf_counter();
        handler->perf_counter = platform_epoch_time();
        handler->stack_size = (int32_t) platform_capture_call_stack(handler->stack, PLATFORM_CALLSTACKS_MAX, 1);
        handler->signal = sig;
        siglongjmp(handler->jump_buffer, PLATFORM_SANDBOXE_JUMP_CODE);
    }
}

Platform_Exception platform_exception_sandbox(
    void (*sandboxed_func)(void* sandbox_context),   
    void* sandbox_context,
    void (*error_func)(void* error_context, Platform_Sandbox_Error error),
    void* error_context)
{
    typedef struct {
        int signal;
        Platform_Exception platform_error;
        struct sigaction action;
        struct sigaction prev_action;
    } Signal_Error;

    #undef X
    #define X(SIGNAL_NAME, PLATFORM_ERROR) \
         {(SIGNAL_NAME), (PLATFORM_ERROR), {0}, {0}},

    Signal_Error error_handlers[] = {
        SIGNAL_ACTION_X
    };

    const int64_t handler_count = (int64_t) sizeof(error_handlers) / (int64_t) sizeof(Signal_Error);
    for(int64_t i = 0; i < handler_count; i++)
    {
        Signal_Error* sig_error = &error_handlers[i];
        sig_error->action.sa_handler = (void(*)(int)) (void *)platform_sighandler;
        sigemptyset(&sig_error->action.sa_mask);
        // sigaddset(&sig_error->action.sa_mask, (int) SA_RESETHAND);
        sigaddset(&sig_error->action.sa_mask, (int) SA_NOCLDSTOP);

        bool state = sigaction(sig_error->signal, &sig_error->action, &sig_error->prev_action) == 0;
        assert(state && "bad signal specifier!");
    }

    Platform_Exception had_exception = PLATFORM_EXCEPTION_NONE;
    if(platform_signal_handler_queue == NULL)
    {
        size_t needed_size = PLATFORM_SANDBOXES_MAX * sizeof(Signal_Handler_State);
        platform_signal_handler_queue = (Signal_Handler_State*) malloc(needed_size);
        if(platform_signal_handler_queue == NULL)
        {
            PLATFORM_PRINT_OUT_OF_MEMORY(needed_size);
            assert("out of memory! @TODO: possible exception?");
            had_exception = PLATFORM_EXCEPTION_OTHER;
        }
        else
        {
            memset(platform_signal_handler_queue, 0, needed_size);
        }
    }
    
    if(platform_signal_handler_queue != NULL)
    {
        platform_signal_handler_i1 = _CLAMP(platform_signal_handler_i1 + 1, 1, PLATFORM_SANDBOXES_MAX);
        Signal_Handler_State* handler = &platform_signal_handler_queue[platform_signal_handler_i1 - 1];
        memset(handler, 0, sizeof *handler);

        switch(sigsetjmp(handler->jump_buffer, 0))
        {
            case 0: {
                sandboxed_func(sandbox_context);
                break;
            }
            case PLATFORM_SANDBOXE_JUMP_CODE: {
                had_exception = PLATFORM_EXCEPTION_OTHER;
                
                for(int64_t i = 0; i < handler_count; i++)
                {
                    if(error_handlers[i].signal == handler->signal)
                    {
                        had_exception = error_handlers[i].platform_error;
                        break;
                    }
                }
                
                Platform_Sandbox_Error sanbox_error = {PLATFORM_EXCEPTION_NONE};
                sanbox_error.exception = had_exception;
                sanbox_error.call_stack = (const void * const*) (void*) handler->stack;
                sanbox_error.call_stack_size = handler->stack_size;
                sanbox_error.epoch_time = handler->epoch_time;
                
                //@TODO
                sanbox_error.execution_context = NULL;
                sanbox_error.execution_context_size = 0;

                error_func(error_context, sanbox_error);
                break;
            }
            default: {
                assert(false && "unexpected jump occured!");
                break;
            }
        }

        platform_signal_handler_i1 = _CLAMP(platform_signal_handler_i1 - 1, 0, PLATFORM_SANDBOXES_MAX);

    }
    for(int64_t i = 0; i < handler_count; i++)
    {
        Signal_Error* sig_error = &error_handlers[i];
        bool state = sigaction(sig_error->signal, &sig_error->prev_action, NULL) == 0;
        assert(state && "bad signal specifier");
    }

    return had_exception;
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
        default:
        case PLATFORM_EXCEPTION_OTHER: return "PLATFORM_EXCEPTION_OTHER";
    }
}

// #endif