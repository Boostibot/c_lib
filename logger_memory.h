#ifndef JOT_LOGGER_MEMORYGER_MEMORY
#define JOT_LOGGER_MEMORYGER_MEMORY

// A logger that only collects error messages in memory without printing them. 
// Makes for a surprisingly powerfull error management utility.
// 
// If we set it up prior to calling some function that can fail. The function broadcasts its errors without having to worry
// about some custom complex error handling startegy. We use this with functions that can fail for very diverse reasons and 
// the error handling startegy outlined in error.h is not enough.

#include "string.h"
#include "vformat.h"
#include "log.h"

typedef struct Memory_Log {
    //A string that holds the module string and right after it the formatted message. 
    //This is to reduce the number of allocations by half.
    String_Builder module_and_message; 
    i32 module_size;
    i32 log_type;
    i32 indentation;
    i64 epoch_time;
    Source_Info source;
} Memory_Log;

DEFINE_ARRAY_TYPE(Memory_Log, Memory_Log_Array);

typedef struct Memory_Logger {
    Logger logger;
    
    Memory_Log_Array logs;
    isize total_log_count;     //The number of calls to log
    isize ignored_log_count;   //The number of ignored calls to log
    
    isize max_logs;            //Maximum number of logs to keep. Defaults to INT32_MAX
    isize log_every_nth;       //Logs only every log_every_nth. Defaults to 1

    bool has_prev_logger;
    Logger* prev_logger;
} Memory_Logger;

EXPORT String memory_log_get_module(const Memory_Log* log);
EXPORT String memory_log_get_message(const Memory_Log* log);
EXPORT void memory_logger_log(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args);

EXPORT void memory_logger_deinit(Memory_Logger* logger);
EXPORT void memory_logger_init_custom(Memory_Logger* logger, Allocator* alloc, isize max_logs, isize log_every_nth);
EXPORT void memory_logger_init(Memory_Logger* logger, Allocator* alloc);
EXPORT void memory_logger_init_use(Memory_Logger* logger, Allocator* alloc);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOGGER_MEMORY_IMPL)) && !defined(JOT_LOGGER_MEMORY_HAS_IMPL)
#define JOT_LOGGER_MEMORY_HAS_IMPL

EXPORT String memory_log_get_module(const Memory_Log* log)
{
    return string_head(string_from_builder(log->module_and_message), log->module_size);
}

EXPORT String memory_log_get_message(const Memory_Log* log)
{
    return string_tail(string_from_builder(log->module_and_message), log->module_size);
}

EXPORT void memory_logger_log(Logger* logger, const char* module, Log_Type type, isize indentation, Source_Info source, const char* format, va_list args)
{
    Memory_Logger* self = (Memory_Logger*) (void*) logger;
    if(self->total_log_count % self->log_every_nth == 0 && self->total_log_count < self->max_logs)
    {
        Allocator* alloc = self->logs.allocator;
        Memory_Log log = {alloc};
        array_reserve(&log.module_and_message, 255);
        builder_append(&log.module_and_message, string_make(module));
        log.module_size = (i32) log.module_and_message.size;
        vformat_append_into(&log.module_and_message, format, args);
        log.log_type = type;
        log.indentation = (i32) indentation;
        log.source = source;
        log.epoch_time = platform_epoch_time();

        array_push(&self->logs, log);
    }
    else
    {
        self->ignored_log_count += 1;

    }
    self->total_log_count += 1;
}

EXPORT void memory_logger_deinit(Memory_Logger* logger)
{
    for(isize i = 0; i < logger->logs.size; i++)
        array_deinit(&logger->logs.data[i].module_and_message);

    array_deinit(&logger->logs);

    if(logger->has_prev_logger)
        log_system_set_logger(logger->prev_logger);

    memset(logger, 0, sizeof *logger);
}

EXPORT void memory_logger_init_custom(Memory_Logger* logger, Allocator* alloc, isize max_logs, isize log_every_nth)
{
    memory_logger_deinit(logger);
    array_init(&logger->logs, alloc);
    logger->logger.log = memory_logger_log;
    logger->max_logs = max_logs;
    logger->log_every_nth = log_every_nth;
}

EXPORT void memory_logger_init(Memory_Logger* logger, Allocator* alloc)
{
    memory_logger_init_custom(logger, alloc, INT32_MAX, 1);
}

EXPORT void memory_logger_init_use(Memory_Logger* logger, Allocator* alloc)
{
    memory_logger_init(logger, alloc);
    logger->prev_logger = log_system_set_logger(&logger->logger);
    logger->has_prev_logger = true;
}

#endif