#ifndef LIB_LIB_LOGGER_FILEGER_FILE
#define LIB_LIB_LOGGER_FILEGER_FILE

//This is a simple logger (log formatter)

//The main focus is to be able to split logs into modules and types and then use these modules
// to filter the output. For example we know that one system lets say animation - ANIM - system 
// is behaving oddly so we simply set the console to only display ANIM logs and only 
// ones that are WARNING, ERROR, or FATAL.

// The syntax is: 
// LOG_INFO("ANIM", "iterating all entitites");
// log_group_push();
// for(int i = 0; i < 10; i++)
//     LOG_INFO("anim", "entity id:%d found", i);
// 
// log_group_pop();
// LOG_FATAL("ANIM", 
//    "Fatal error encountered!\n"
//    "Some more info\n" 
//    "%d-%d", 10, 20);
//
//
// Which results in:
// 00-00-00 000 INFO  ANIM :iterating all entitites
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
// 1) Readable for humnas 
// 2) Lack of needless symbols such as [ ] around time and ( ) around module
// 3) Simple parsing of the file
//    Each line is compltely separate for the parser. It begins fixed ammount of date chars then space
//    then mdoule which cannot contain space. Then follows sequence of dots and some number of spaces. 
//    Each dot signifies one level of depth. Then comes : or , marking the end of meta data and start of the message.
//    Message data is till the end of the line. If message is mutlilined the next entry does not have date.

#include "time.h"
#include "string.h"
#include "profile.h"
#include "format.h"
#include "log.h"

typedef bool(*File_Logger_Print)(const void* data, isize size, void* context); 

EXPORT typedef struct File_Logger {
    Logger logger;
    Allocator* default_allocator;
    Allocator* scratch_allocator;
    String_Builder buffer;

    // Flushes the file once some number of bytes were written (buffer size) 
    // or if mor than flush_every_seconds passed since the last flush. 
    // The flushing always happens AFTER the latest append to the log.
    // This means a call to log will only produce one flush per call at max.
    isize flush_every_bytes;                        //defaults to 4K
    f64   flush_every_seconds;                      //defaults to 2ms
    
    //A binary mask to specify which log types to output. 
    //For example LOG_TYPE_INFO has value 0 so its bitmask is 1 << 0 or 1 << LOG_TYPE_INFO
    u64 file_type_filter;                       //defaults to all 1s in binary ie 0xFFFFFFFFFFFFFFFF    
    u64 console_type_filter;                    //defaults to all 1s in binary ie 0xFFFFFFFFFFFFFFFF
    
    //A list of modules to output. If this list is empty does not print anything.
    //Only has effect if the xxxx_use_filter is set to true
    String_Builder_Array console_module_filter;    //defaults to {0} 
    String_Builder_Array file_module_filter;        //defaults to {0} 
    
    //Specify wheter any module filtering should be used.
    // (this is setting primarily important because often we want to print all
    //  log modules without apriory knowing their names)
    bool console_use_filter;                        //defaults to false
    bool file_use_filter;                           //defaults to false
    
    String_Builder file_directory_path;             //defaults to "logs/"
    String_Builder file_prefix;                     //defaults to ""
    String_Builder file_postfix;                    //defaults to ".txt"
    
    FILE* file;                                     //file to whcih to print;
    f64 last_flush_time;

    File_Logger_Print console_print_func;        //defaults to NULL in which case prints to stdout (using fwrite)
    File_Logger_Print file_print_func;           //defaults to NULL in whcih case creates a file in file_directory_path and writes to it
    
    void* console_print_context;    //defaults to NULL
    void* file_print_context;       //defaults to NULL

    bool has_prev_logger;
    Logger* prev_logger;
} File_Logger;


EXPORT void file_logger_log(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args);
EXPORT bool file_logger_flush(File_Logger* logger);

EXPORT void file_logger_deinit(File_Logger* logger);
EXPORT void file_logger_init_custom(File_Logger* logger, Allocator* def_alloc, Allocator* scratch_alloc, isize flush_every_bytes, f64 flush_every_seconds, String folder, String prefix, String postfix);
EXPORT void file_logger_init(File_Logger* logger, Allocator* def_alloc, Allocator* scratch_alloc);
EXPORT void file_logger_init_use(File_Logger* logger, Allocator* def_alloc, Allocator* scratch_alloc);

EXPORT void file_logger_log_append_into(Allocator* scratch, String_Builder* append_to, String module, Log_Type type, isize indentation, i64 epoch_time, const char* format, va_list args);
EXPORT void file_logger_log_into(Allocator* scratch, String_Builder* append_to, String module, Log_Type type, isize indentation, i64 epoch_time, const char* format, va_list args);

EXPORT void file_logger_console_add_module_filter(File_Logger* logger, String module, bool use_filter);
EXPORT void file_logger_console_add_type_filter(File_Logger* logger, isize type);
EXPORT void file_logger_console_set_none_type_filter(File_Logger* logger);
EXPORT void file_logger_console_clear_filters(File_Logger* logger);

extern File_Logger global_logger;
#endif

#define LIB_ALL_IMPL

#if (defined(LIB_ALL_IMPL) || defined(LIB_LOGGER_FILE_IMPL)) && !defined(LIB_LOGGER_FILE_HAS_IMPL)
#define LIB_LOGGER_FILE_HAS_IMPL

File_Logger global_logger = {0};

EXPORT void file_logger_log_append_into(Allocator* scratch, String_Builder* append_to, String module, Log_Type type, isize indentation, i64 epoch_time, const char* format, va_list args)
{       
    PERF_COUNTER_START(counter);

    const isize module_field_size = 6;

    isize size_before = append_to->size;
    String group_separator = STRING("    ");
    String message_string = {0};
    
    String_Builder formatted_module = {0};
    String_Builder formatted_message = {0};
    array_init_backed(&formatted_module, scratch, 64);
    array_init_backed(&formatted_message, scratch, 512);

    //formats module: "module name" -> "MODULE_NAME    "
    //                                 <--------------->
    //                                 module_field_size
    array_resize(&formatted_module, MAX(module.size, module_field_size));

    isize writting_to = 0;
    for(isize i = 0; i < module.size; i++)
    {
        //to ascii uppercase
        char c = module.data[i];
        if('a' <= c && c <= 'z')
            c = c - 'a' + 'A';

        if(c == '\n' || c == ' ' || c == '\f' || c == '\t' || c == '\r' || c == '\v')
            formatted_module.data[writting_to ++] = '_'; 
        else
            formatted_module.data[writting_to ++] = c; 
    }

    for(isize i = writting_to; i < formatted_module.size; i++)
        formatted_module.data[i] = ' ';

    vformat_into(&formatted_message, format, args);

    //Skip all trailing newlines
    message_string = string_from_builder(formatted_message);
    for(isize message_size = message_string.size; message_size > 0; message_size --)
    {
        if(message_string.data[message_size - 1] != '\n')
        {
            message_string = string_head(message_string, message_size);
            break;
        }
    }

    //Convert type to string
    const char* type_str = NULL;
    switch(type)
    {
        case LOG_TYPE_INFO: type_str = "INFO"; break;
        case LOG_TYPE_WARN: type_str = "WARN"; break;
        case LOG_TYPE_ERROR: type_str = "ERROR"; break;
        case LOG_TYPE_FATAL: type_str = "FATAL"; break;
        case LOG_TYPE_DEBUG: type_str = "DEBUG"; break;
        case LOG_TYPE_TRACE: type_str = "TRACE"; break;
        case LOG_TYPE_ENUM_MAX:
        default: break;
    }

    Platform_Calendar_Time c = platform_epoch_time_to_calendar_time(epoch_time);
    
    //Try to guess size
    array_grow(append_to, size_before + message_string.size + 100 + module.size);

    if(type_str != NULL)
    {
        format_into(append_to, "%02d-%02d-%02d %03d %-5s  ", 
            (int) c.hour, (int) c.minute, (int) c.second, (int) c.millisecond, type_str);
    }
    else
    {
        format_into(append_to, "%02d-%02d-%02d %03d %-5d  ", 
            (int) c.hour, (int) c.minute, (int) c.second, (int) c.millisecond, (int) type);
    }
    
    isize header_size = append_to->size - size_before;

    isize curr_line_pos = 0;
    for(bool run = true; run;)
    {
        isize next_line_pos = -1;
        if(curr_line_pos >= message_string.size)
        {
            if(message_string.size != 0)
                break;
        }
        else
        {
            next_line_pos = string_find_first_char(message_string, '\n', curr_line_pos);
        }

        if(next_line_pos == -1)
        {
            next_line_pos = message_string.size;
            run = false;
        }
        
        ASSERT(curr_line_pos <= message_string.size);
        ASSERT(next_line_pos <= message_string.size);

        String curr_line = string_range(message_string, curr_line_pos, next_line_pos);

        //if is first line do else insert header-sized ammount of spaces
        if(curr_line_pos != 0)
        {
            isize before_padding = append_to->size;
            array_resize(append_to, before_padding + header_size);
            memset(append_to->data + before_padding, ' ', header_size);
        }
        
        builder_append(append_to, string_from_builder(formatted_module));

        //insert n times group separator
        for(isize i = 0; i < indentation; i++)
            builder_append(append_to, group_separator);
        
        builder_append(append_to, STRING(" :"));
        builder_append(append_to, curr_line);
        array_push(append_to, '\n');

        curr_line_pos = next_line_pos + 1;
    }
    
    array_deinit(&formatted_module);
    array_deinit(&formatted_message);

    PERF_COUNTER_END(counter);
}

EXPORT void file_logger_log_into(Allocator* scratch, String_Builder* append_to, String module, Log_Type type, isize indentation, i64 epoch_time, const char* format, va_list args)
{
    array_clear(append_to);
    file_logger_log_append_into(scratch, append_to, module, type, indentation, epoch_time, format, args);
}


EXPORT void file_logger_deinit(File_Logger* logger)
{
    array_deinit(&logger->buffer);
    array_deinit(&logger->file_directory_path);
    array_deinit(&logger->file_prefix);
    array_deinit(&logger->file_postfix);

    builder_array_deinit(&logger->console_module_filter);
    builder_array_deinit(&logger->file_module_filter);
    
    if(logger->has_prev_logger)
        log_system_set_logger(logger->prev_logger);

    if(logger->file)
        fclose(logger->file);

    memset(logger, 0, sizeof *logger);
}

EXPORT void file_logger_init_custom(File_Logger* logger, Allocator* def_alloc, Allocator* scratch_alloc, isize flush_every_bytes, f64 flush_every_seconds, String folder, String prefix, String postfix)
{
    file_logger_deinit(logger);
    
    logger->default_allocator = def_alloc;
    logger->scratch_allocator = scratch_alloc;
    array_init(&logger->buffer, def_alloc);
    array_init(&logger->file_directory_path, def_alloc);
    array_init(&logger->file_prefix, def_alloc);
    array_init(&logger->file_postfix, def_alloc);
    array_init(&logger->console_module_filter, def_alloc);
    array_init(&logger->file_module_filter, def_alloc);

    logger->logger.log = file_logger_log;
    logger->flush_every_bytes = flush_every_bytes;
    logger->flush_every_seconds = flush_every_seconds;
    logger->file_type_filter = 0xFFFFFFFFFFFFFFFF;
    logger->console_type_filter = 0xFFFFFFFFFFFFFFFF;
    logger->console_use_filter = false;
    logger->file_use_filter = false;

    builder_assign(&logger->file_directory_path, folder);
    builder_assign(&logger->file_prefix, prefix);
    builder_assign(&logger->file_postfix, postfix);

    array_grow(&logger->buffer, flush_every_bytes);
}

EXPORT void file_logger_init(File_Logger* logger, Allocator* def_alloc, Allocator* scratch_alloc)
{
    file_logger_init_custom(logger, def_alloc, scratch_alloc, PAGE_BYTES, 2.0 / 1000, STRING("logs"), STRING(""), STRING(".txt"));
}

EXPORT void file_logger_init_use(File_Logger* logger, Allocator* def_alloc, Allocator* scratch_alloc)
{
    file_logger_init(logger, def_alloc, scratch_alloc);
    logger->prev_logger = log_system_set_logger(&logger->logger);
    logger->has_prev_logger = true;
}

EXPORT bool file_logger_flush(File_Logger* logger)
{
    File_Logger* self = (File_Logger*) (void*) logger;

    bool state = true;
    if(self->buffer.size > 0)
    {
        if(self->file_print_func)
            self->file_print_func(self->buffer.data, self->buffer.size, self->file_print_context);
        else
        {
            if(self->file == NULL)
            {
                String_Builder file_name = {0};
                array_init_backed(&file_name, self->scratch_allocator, 256);

                builder_append(&file_name, string_from_builder(self->file_directory_path));
                builder_append(&file_name, STRING("/"));
                builder_append(&file_name, string_from_builder(self->file_prefix));
                Platform_Calendar_Time calendar = platform_epoch_time_to_calendar_time(platform_local_epoch_time());
                format_append_into(&file_name, "%04d-%02d-%02d__%02d-%02d-%02d", 
                    (int) calendar.year, (int) calendar.month, (int) calendar.day, 
                    (int) calendar.hour, (int) calendar.minute, (int) calendar.second);
                    
                builder_append(&file_name, string_from_builder(self->file_postfix));

                platform_directory_create(self->file_directory_path.data);
                self->file = fopen(cstring_from_builder(file_name), "ab");
                
                state = state && self->file != NULL;
                state = state && setvbuf(self->file , NULL, _IONBF, 0) == 0;

                array_deinit(&file_name);
            }

            if(self->file)
                fwrite(self->buffer.data, 1, self->buffer.size, self->file);
        }

        self->last_flush_time = clock_s();
        array_clear(&self->buffer);
    }

    return state;
}

EXPORT void file_logger_log(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args)
{
    PERF_COUNTER_START(counter);
    (void) source;

    File_Logger* self = (File_Logger*) (void*) logger;
    String module_string = string_make(module);

    String_Builder formatted_log = {0};
    array_init_backed(&formatted_log, self->scratch_allocator, 1024);
    file_logger_log_append_into(self->scratch_allocator, &formatted_log, module_string, type, indentation, platform_local_epoch_time(), format, args);

    bool print_to_console = false;
    bool print_to_file = false;

    //Determines wheter or not to print the message to conosle and file
    if((type > LOG_TYPE_ENUM_MAX) || (((i64) 1 << type) & self->console_type_filter))
    {
        if(self->console_use_filter == false)
            print_to_console = true;
        else
        {
            for(isize i = 0; i < self->console_module_filter.size; i++)
            {
                if(string_is_equal(module_string, string_from_builder(self->console_module_filter.data[i])))
                {
                    print_to_console = true;
                    break;
                }
            }
        }
    }

    if((type > LOG_TYPE_ENUM_MAX) || (((i64) 1 << type) & self->file_type_filter))
    {
        if(self->file_use_filter == false)
            print_to_file = true;
        else
        {
            for(isize i = 0; i < self->file_module_filter.size; i++)
            {
                if(string_is_equal(module_string, string_from_builder(self->file_module_filter.data[i])))
                {
                    print_to_file = true;
                    break;
                }
            }
        }
    }

    if(print_to_console)
    {
        if(self->console_print_func)
            self->console_print_func(formatted_log.data, formatted_log.size, self->console_print_context);
        else
            fwrite(formatted_log.data, 1, formatted_log.size, stdout);
    }

    if(print_to_file)
        builder_append(&self->buffer, string_from_builder(formatted_log));
    
    f64 time_since_last_flush = clock_s() - self->last_flush_time;
    if(self->buffer.size > self->flush_every_bytes || time_since_last_flush > self->flush_every_seconds)
        file_logger_flush(self);

    array_deinit(&formatted_log);
    PERF_COUNTER_END(counter);
}

EXPORT void file_logger_console_add_module_filter(File_Logger* logger, String module, bool use_filter)
{
    array_push(&logger->console_module_filter, builder_from_string(module, logger->default_allocator));
    logger->console_use_filter = use_filter;
}

EXPORT void file_logger_console_add_type_filter(File_Logger* logger, isize type)
{
    if(type <= LOG_TYPE_ENUM_MAX)
        logger->console_type_filter |= (i64) 1 << type;
}
EXPORT void file_logger_console_set_none_type_filter(File_Logger* logger)
{
    logger->console_type_filter = 0;
}

EXPORT void file_logger_console_clear_filters(File_Logger* logger)
{
    for(isize i = 0; i < logger->console_module_filter.size; i++)
        array_deinit(&logger->console_module_filter.data[i]);

    logger->console_type_filter = 0xFFFFFFFFFFFFFFFF;
    array_clear(&logger->console_module_filter);
}


EXPORT void file_logger_console_add_module_filter(File_Logger* logger, String module, bool use_filter);
EXPORT void file_logger_console_add_type_filter(File_Logger* logger, isize type);
EXPORT void file_logger_console_set_none_type_filter(File_Logger* logger);
EXPORT void file_logger_console_clear_filters(File_Logger* logger);


#endif
