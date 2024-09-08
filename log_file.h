#ifndef JOT_JOT_LOGGER_FILEGER_FILE
#define JOT_JOT_LOGGER_FILEGER_FILE

//This is a simple logger (log formatter)

//The main focus is to be able to split logs into modules and types and then use these modules
// to filter the output. For example we know that one system lets say animation - ANIM - system 
// is behaving oddly so we simply set the console to only display ANIM logs and only 
// ones that are WARNING, ERROR, or FATAL.

// The syntax is: 
// LOG_INFO("ANIM", "iterating all entities");
// log_indent();
// for(int i = 0; i < 10; i++)
//     LOG_INFO("anim", "entity id:%d found", i);
// 
// log_outdent();
// LOG_FATAL("ANIM", 
//    "Fatal error encountered!\n"
//    "Some more info\n" 
//    "%d-%d", 10, 20);
//
//
// Which results in:
// 00-00-00 000 INFO  ANIM :iterating all entities
// 00-00-00 000 INFO  ANIM .  :entity id:0 found
//                    ANIM .  :Hello from entity
// 00-00-00 000 INFO  ANIM .  :entity id:1 found
//                    ANIM .  :Hello from entity
// 00-00-00 000 INFO  ANIM .  :entity id:2 found
//                    ANIM .  :Hello from entity
// 00-00-00 000 INFO  ANIM .  :entity id:3 found
//                    ANIM .  :Hello from entity
// 00-00-00 000 INFO  ANIM .  :entity id:4 found
//                    ANIM .  :Hello from entity
// 00-00-00 000 FATAL ANIM :Fatal error encountered!
//                    ANIM :Some more info
//                    ANIM :10-20
//
// The advantages of this format are as follows:
// 1) Readable for humans 
// 2) Lack of needless symbols such as [ ] around time and ( ) around module
// 3) Simple parsing of the file
//    Each line is completely separate for the parser. It begins fixed amount of date chars then space
//    then module which cannot contain space. Then follows sequence of dots and some number of spaces. 
//    Each dot signifies one level of depth. Then comes : or , marking the end of meta data and start of the message.
//    Message data is till the end of the line. If message is multiline the next entry does not have date.

#include "time.h"
#include "string.h"
#include "profile_defs.h"
#include "vformat.h"
#include "time.h"
#include "log.h"
#include "arena_stack.h"

typedef bool(*File_Logger_Print)(const void* data, isize size, void* context); 

typedef struct File_Logger {
    Allocator* default_allocator;
    String_Builder buffer;
    Platform_Mutex mutex;

    // Flushes the file once some number of bytes were written (buffer size) 
    // or if mor than flush_every_seconds passed since the last flush. 
    // The flushing always happens AFTER the latest append to the log.
    // This means a call to log will only produce one flush per call at max.
    isize flush_every_bytes;                        //defaults to 4K
    f64   flush_every_seconds;                      //defaults to 2ms
    
    //A binary mask to specify which log types to output. 
    //For example LOG_INFO has value 0 so its bitmask is 1 << 0 or 1 << LOG_INFO
    u64 file_type_filter;                       //defaults to all 1s in binary ie 0xFFFFFFFFFFFFFFFF    
    u64 console_type_filter;                    //defaults to all 1s in binary ie 0xFFFFFFFFFFFFFFFF
    
    String_Builder file_directory_path;             //defaults to "logs/"
    String_Builder file_prefix;                     //defaults to ""
    String_Builder file_postfix;                    //defaults to ".txt"
    
    FILE* file;                                     //file to which to print;
    f64 last_flush_time;
    i64 init_epoch_time;

    File_Logger_Print console_print_func;        //defaults to NULL in which case prints to stdout (using fwrite)
    File_Logger_Print file_print_func;           //defaults to NULL in which case creates a file in file_directory_path and writes to it
    
    void* console_print_context;    //defaults to NULL
    void* file_print_context;       //defaults to NULL

    bool open_failed;
    bool has_prev_logger;
    bool _[6];
    Log* prev_logger;

    Log_Set prev;
} File_Logger;

typedef enum File_Logger_Type{
    FILE_LOGGER_INFO , 
    FILE_LOGGER_OKAY , 
    FILE_LOGGER_WARN , 
    FILE_LOGGER_ERROR, 
    FILE_LOGGER_FATAL, 
    FILE_LOGGER_DEBUG, 
    FILE_LOGGER_TRACE,
} File_Logger_Type;

EXTERNAL void file_logger_log(void* context, int indent, int custom, int is_flush, const char* name, const char* format, va_list args);
EXTERNAL bool file_logger_flush(File_Logger* logger);
EXTERNAL Log_Set file_logger_log_set(File_Logger* logger);

EXTERNAL void file_logger_deinit(File_Logger* logger);
EXTERNAL void file_logger_init_custom(File_Logger* logger, Allocator* def_alloc, isize flush_every_bytes, f64 flush_every_seconds, String folder, String prefix, String postfix);
EXTERNAL void file_logger_init(File_Logger* logger, Allocator* def_alloc, const char* folder);
EXTERNAL void file_logger_init_use(File_Logger* logger, Allocator* def_alloc, const char* folder);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOGGER_FILE_IMPL)) && !defined(JOT_LOGGER_FILE_HAS_IMPL)
#define JOT_LOGGER_FILE_HAS_IMPL

EXTERNAL void file_logger_log_append_into(Allocator* scratch, String_Builder* append_to, int indent, int custom, const char* name, String message, i64 now)
{       
    PROFILE_START();
    const isize module_field_size = 8;
    String module = string_of(name);

    isize size_before = append_to->len;
    String group_separator = STRING("    ");
    
    String_Builder formatted_module = builder_make(scratch, 0);

    //formats module: "module name" -> "MODULE_NAME    "
    //                                 <--------------->
    //                                 module_field_size
    builder_resize(&formatted_module, MAX(module.len, module_field_size));
    {
        isize written = 0;
        for(isize i = 0; i < module_field_size - module.len; i++)
            formatted_module.data[written++] = ' ';

        for(isize i = 0; i < module.len; i++)
        {
            //to ascii uppercase
            char c = module.data[i];
            if('a' <= c && c <= 'z')
                c = c - 'a' + 'A';

            CHECK_BOUNDS(written, formatted_module.len);
            if(c == '\n' || c == ' ' || c == '\f' || c == '\t' || c == '\r' || c == '\v')
                formatted_module.data[written++] = '_'; 
            else
                formatted_module.data[written++] = c; 
        }
    }

    //Skip all trailing newlines
    for(isize message_size = message.len; message_size > 0; message_size --)
    {
        if(message.data[message_size - 1] != '\n')
        {
            message = string_head(message, message_size);
            break;
        }
    }

    //Convert type to string
    //Platform_Calendar_Time c = platform_epoch_time_to_calendar_time(epoch_time);
    Posix_Date c = local_date_from_epoch_time(now);
        
    //Try to guess size
    builder_reserve(append_to, size_before + message.len + 100 + module.len);
    
    const char* type_str = "";
    switch(custom)
    {
        case FILE_LOGGER_INFO: type_str = "INFO"; break;
        case FILE_LOGGER_OKAY: type_str = "SUCC"; break;
        case FILE_LOGGER_WARN: type_str = "WARN"; break;
        case FILE_LOGGER_ERROR: type_str = "ERROR"; break;
        case FILE_LOGGER_FATAL: type_str = "FATAL"; break;
        case FILE_LOGGER_DEBUG: type_str = "DEBUG"; break;
        case FILE_LOGGER_TRACE: type_str = "TRACE"; break;
    }

    format_append_into(append_to, "%02i-%02i-%02i %-5s ", 
        (int) c.tm_hour, (int) c.tm_min, (int) c.tm_sec, type_str);
    
    isize header_size = append_to->len - size_before;

    isize curr_line_pos = 0;
    for(bool run = true; run;)
    {
        isize next_line_pos = -1;
        if(curr_line_pos >= message.len)
        {
            if(message.len != 0)
                break;
        }
        else
        {
            next_line_pos = string_find_first_char(message, '\n', curr_line_pos);
        }

        if(next_line_pos == -1)
        {
            next_line_pos = message.len;
            run = false;
        }
        
        ASSERT(curr_line_pos <= message.len);
        ASSERT(next_line_pos <= message.len);

        String curr_line = string_range(message, curr_line_pos, next_line_pos);

        //if is first line do else insert header-sized amountof spaces
        if(curr_line_pos != 0)
        {
            isize before_ = append_to->len;
            builder_resize(append_to, before_ + header_size);
            memset(append_to->data + before_, ' ', (size_t) header_size);
        }
        
        builder_append(append_to, formatted_module.string);

        //insert n times group separator
        for(isize i = 0; i < indent; i++)
            builder_append(append_to, group_separator);
        
        builder_push(append_to, ' ');
        builder_append(append_to, curr_line);
        builder_push(append_to, '\n');

        curr_line_pos = next_line_pos + 1;
    }
    
    builder_deinit(&formatted_module);
    PROFILE_END();
}

EXTERNAL void file_logger_deinit(File_Logger* logger)
{
    builder_deinit(&logger->buffer);
    builder_deinit(&logger->file_directory_path);
    builder_deinit(&logger->file_prefix);
    builder_deinit(&logger->file_postfix);

    if(logger->has_prev_logger)
        set_log_set(logger->prev);

    if(logger->file)
        fclose(logger->file);

    platform_mutex_deinit(&logger->mutex);
    memset(logger, 0, sizeof *logger);
}

EXTERNAL void file_logger_init_custom(File_Logger* logger, Allocator* alloc, isize flush_every_bytes, f64 flush_every_seconds, String folder, String prefix, String postfix)
{
    file_logger_deinit(logger);
    
    Platform_Error error = platform_mutex_init(&logger->mutex);
    (void) error; //discarding the error for the moment;

    logger->default_allocator = alloc;
    builder_init_with_capacity(&logger->buffer, alloc, flush_every_bytes);
    builder_init(&logger->file_directory_path, alloc);
    builder_init(&logger->file_prefix, alloc);
    builder_init(&logger->file_postfix, alloc);

    logger->flush_every_bytes = flush_every_bytes;
    logger->flush_every_seconds = flush_every_seconds;
    logger->file_type_filter = 0xFFFFFFFFFFFFFFFF;
    logger->console_type_filter = 0xFFFFFFFFFFFFFFFF;
    logger->init_epoch_time = platform_epoch_time();

    builder_assign(&logger->file_directory_path, folder);
    builder_assign(&logger->file_prefix, prefix);
    builder_assign(&logger->file_postfix, postfix);
}

EXTERNAL void file_logger_init(File_Logger* logger, Allocator* def_alloc, const char* folder)
{
    file_logger_init_custom(logger, def_alloc, PAGE_BYTES, 2.0 / 1000, string_of(folder), STRING(""), STRING(".txt"));
}

EXTERNAL void file_logger_init_use(File_Logger* logger, Allocator* def_alloc, const char* folder)
{
    file_logger_init(logger, def_alloc, folder);
    logger->prev = set_log_set(file_logger_log_set(logger));
    logger->has_prev_logger = true;
}

//Some of the ansi colors that can be used within logs. 
//However their usage is not recommended since these will be written to log files and thus make their parsing more difficult.
#define ANSI_COLOR_NORMAL       "\x1B[0m"
#define ANSI_COLOR_RED          "\x1B[31m"
#define ANSI_COLOR_BRIGHT_RED   "\x1B[91m"
#define ANSI_COLOR_GREEN        "\x1B[32m"
#define ANSI_COLOR_YELLOW       "\x1B[33m"
#define ANSI_COLOR_BLUE         "\x1B[34m"
#define ANSI_COLOR_MAGENTA      "\x1B[35m"
#define ANSI_COLOR_CYAN         "\x1B[36m"
#define ANSI_COLOR_WHITE        "\x1B[37m"
#define ANSI_COLOR_GRAY         "\x1B[90m"

EXTERNAL bool file_logger_flush(File_Logger* logger)
{
    PROFILE_START();
    File_Logger* self = (File_Logger*) (void*) logger;

    bool state = true;
    if(self->buffer.len > 0)
    {
        if(self->file_print_func)
            self->file_print_func(self->buffer.data, self->buffer.len, self->file_print_context);
        else
        {
            if(self->file == NULL)
            {
                SCRATCH_ARENA(arena)
                {
                    platform_directory_create(logger->file_directory_path.string, false);

                    Posix_Date calendar = local_date_from_epoch_time(logger->init_epoch_time);

                    const char* filename = format(arena.alloc, "%s/%s%04d-%02d-%02d__%02d-%02d-%02d%s", 
                        self->file_directory_path.data,
                        self->file_prefix.data,
                        (int) calendar.tm_year + 1900, (int) calendar.tm_mon, (int) calendar.tm_mday, 
                        (int) calendar.tm_hour, (int) calendar.tm_min, (int) calendar.tm_sec,
                        self->file_postfix.data
                    ).data;

                    self->file = fopen(filename, "ab");
                    state = state && self->file != NULL;
                    state = state && setvbuf(self->file , NULL, _IONBF, 0) == 0;

                    if(state == false && self->open_failed == false)
                    {
                        printf(ANSI_COLOR_RED "File logger failed to create/open log file or containing directory '%s' " ANSI_COLOR_NORMAL "\n", filename);
                        self->open_failed = true;
                    }

                    if(state == true)
                        self->open_failed = false;

                }
            }

            if(self->file)
                fwrite(self->buffer.data, 1, (size_t) self->buffer.len, self->file);
        }

        self->last_flush_time = clock_s();
        builder_clear(&self->buffer);
    }

    PROFILE_END();
    return state;
}

INTERNAL Log _file_logger_make_log(File_Logger* logger, int custom)
{
    Log out = {0};
    out.log = file_logger_log;
    out.context = logger;
    out.name = "";
    out.custom = custom;
    out.indent = 0;
    return out;
}

EXTERNAL Log_Set file_logger_log_set(File_Logger* logger)
{
    Log_Set out = {0};
    out.info = _file_logger_make_log(logger, FILE_LOGGER_INFO);
    out.okay = _file_logger_make_log(logger, FILE_LOGGER_OKAY);
    out.warn = _file_logger_make_log(logger, FILE_LOGGER_WARN);
    out.error = _file_logger_make_log(logger, FILE_LOGGER_ERROR);
    out.fatal = _file_logger_make_log(logger, FILE_LOGGER_FATAL);
    out.debug = _file_logger_make_log(logger, FILE_LOGGER_DEBUG);
    out.trace = _file_logger_make_log(logger, FILE_LOGGER_TRACE);
    return out;
}

EXTERNAL void file_logger_log(void* context, int indent, int custom, int is_flush, const char* name, const char* format, va_list args)
{
    i64 now = platform_epoch_time();
    PROFILE_START();
    File_Logger* self = (File_Logger*) context;
    
    platform_mutex_lock(&self->mutex);
    if(is_flush)
    {
        file_logger_flush(self);
    }
    else
    {
        Arena_Frame arena = scratch_arena_frame_acquire();
        {
            String_Builder message = vformat(arena.alloc, format, args);
            String_Builder formatted_log = builder_make(arena.alloc, 1024);
            file_logger_log_append_into(arena.alloc, &formatted_log, indent, custom, name, message.string, now);

            bool print_to_console = !!((1ULL << custom) & self->console_type_filter);
            bool print_to_file = !!((1ULL << custom) & self->file_type_filter);

            if(print_to_console)
            {
                const char* color_mode = ANSI_COLOR_NORMAL;
                if(custom == FILE_LOGGER_ERROR || custom == FILE_LOGGER_FATAL)
                    color_mode = ANSI_COLOR_BRIGHT_RED;
                else if(custom == FILE_LOGGER_WARN)
                    color_mode = ANSI_COLOR_YELLOW;
                else if(custom == FILE_LOGGER_OKAY)
                    color_mode = ANSI_COLOR_GREEN;
                else if(custom == FILE_LOGGER_TRACE || custom == FILE_LOGGER_DEBUG)
                    color_mode = ANSI_COLOR_GRAY;

                if(self->console_print_func)
                    self->console_print_func(formatted_log.data, formatted_log.len, self->console_print_context);
                else
                    printf("%s%s" ANSI_COLOR_NORMAL, color_mode, formatted_log.data);
            }

            if(print_to_file)
                builder_append(&self->buffer, formatted_log.string);
    
            f64 time_since_last_flush = clock_s() - self->last_flush_time;
            if(self->buffer.len > self->flush_every_bytes || time_since_last_flush > self->flush_every_seconds)
                file_logger_flush(self);
        }
        arena_frame_release(&arena);
    }

    platform_mutex_unlock(&self->mutex);
    PROFILE_END();
}


#endif
