#ifndef JOT_LOG_LIST
#define JOT_LOG_LIST

#include "log.h"
#include "vformat.h"
#include "profile.h"

typedef struct Log_List {
    Logger logger;
    Allocator* allocator;
    Log_Filter filter;

    Log* first;
    Log* last;

    isize size;
    i32 base_group_depth;

    //For init_and_use type of tasks
    bool had_prev_logger;
    Logger* prev_logger;
} Log_List;

EXPORT void log_list_init(Log_List* log_list, Allocator* allocator);
EXPORT void log_list_init_capture(Log_List* log_list, Allocator* allocator);
EXPORT void log_list_deinit(Log_List* log_list);
EXPORT void log_capture(Log_List* log_list);
EXPORT void log_capture_end(Log_List* log_list);

EXPORT void log_list_log_func(Logger* logger_, i32 group_depth, int actions, const char* module, const char* subject, Log_Type type, Source_Info source, const Log* child, const char* format, va_list args);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_LOG_LIST_IMPL)) && !defined(JOT_LOG_LIST_HAS_IMPL)
#define JOT_LOG_LIST_HAS_IMPL

INTERNAL void _log_dealloc_recursive(Log* log_list, Allocator* allocator, isize depth)
{
    //For all practical means it should not reach such a depth.
    //Is removed in release builds
    ASSERT(depth < 100);

    for(Log* curr = log_list; curr != NULL; )
    {
        if(curr->first_child != NULL)
            _log_dealloc_recursive(curr->first_child, allocator, depth + 1);

        Log* next = curr->next;
        allocator_deallocate(allocator, curr, isizeof(Log) + curr->message.size + 1, DEF_ALIGN);
        curr = next;
    }
}

INTERNAL void _log_alloc_recursive(Log** first_child_ptr, Log** last_child_ptr, const Log* log_list, Allocator* allocator, Log_Filter filter, isize depth)
{
    ASSERT(depth < 100);
    for(const Log* curr = log_list; curr != NULL; curr = curr->next)
    {
        if(filter & (Log_Filter) 1 << curr->type)
        {
            //Coalesce allocation of the Log and the message into one.
            void* allocated = allocator_allocate(allocator, isizeof(Log) + curr->message.size + 1, DEF_ALIGN);

            //Copy everything except pointers
            Log* pushed = (Log*) allocated; 
            *pushed = *curr;
            pushed->first_child = NULL;
            pushed->last_child = NULL;
            pushed->prev = NULL;
            pushed->next = NULL;
        
            //copy string
            char* copied_string = (char*) (void*) (pushed + 1);
            memcpy(copied_string, curr->message.data, (size_t) curr->message.size);
            copied_string[curr->message.size] = '\0';
            pushed->message.data = copied_string;
            pushed->message.size = curr->message.size;

            if(*first_child_ptr == NULL || *last_child_ptr == NULL)
            {
                *first_child_ptr = pushed;
                *last_child_ptr = pushed;
            }
            else
            {
                pushed->prev = *last_child_ptr;
                pushed->prev->next = pushed;
                *last_child_ptr = pushed;
            }

            if(curr->first_child)
                _log_alloc_recursive(&pushed->first_child, &pushed->last_child, curr->first_child, allocator, filter, depth + 1);
        }
    }
}

EXPORT void log_list_deinit(Log_List* log_list)
{
    if(allocator_is_arena(log_list->allocator) == false && log_list->allocator != NULL)
        _log_dealloc_recursive(log_list->first, log_list->allocator, 0);
        
    if(log_list->had_prev_logger)
        log_set_logger(log_list->prev_logger);

    memset(log_list, 0, sizeof *log_list);
}

//void log_list_log_func(Logger* logger_, const Log* log_list, i32 group_depth, Log_Action action)
EXPORT void log_list_log_func(Logger* logger_, i32 group_depth, int actions, const char* module, const char* subject, Log_Type type, Source_Info source, const Log* child, const char* format, va_list args)
{
    Log_List* logger = (Log_List*) (void*) logger_;

    PERF_COUNTER_START();

    Log log_list = {0};
    if(actions & LOG_ACTION_LOG)
    {
        log_list.module = module;
        log_list.subject = subject;
        log_list.message = vformat_ephemeral(format, args);
        log_list.type = type;
        log_list.time = platform_epoch_time();
        log_list.source = source;
        log_list.first_child = (Log*) child;
        log_list.last_child = (Log*) child;
    }
    else
    {
        log_list = *child;
    }

    if((actions & (LOG_ACTION_LOG | LOG_ACTION_CHILD)) && (logger->filter & (Log_Filter) 1 << log_list.type))
    {
        ASSERT(group_depth >= 0);
        i32 depth = MAX(group_depth - logger->base_group_depth, 0);

        //@NOTE: Slow but reliable approach. Can be made faster by using lookup table but I am tired.
        i32 reached_depth = 0;
        Log** first = &logger->first;
        Log** last = &logger->last;

        logger->size += 1;
        //Try to reach the desired depth and allocate there
        for(; reached_depth < depth && *last != NULL; reached_depth += 1)
        {
            reached_depth += 1;
            Log* last_log = *last;
            first = &last_log->first_child;
            last = &last_log->last_child;
        }

        //If it still needs extra depth just allocate empty entries
        for(; reached_depth < depth; reached_depth += 1)
        {
            Log empty = {"", ""};
            _log_alloc_recursive(first, last, &empty, logger->allocator, logger->filter, 0);
                
            Log* last_log = *last;
            first = &last_log->first_child;
            last = &last_log->last_child;
        }
            
        ASSERT(first != NULL);
        ASSERT(last != NULL);
            
        _log_alloc_recursive(first, last, &log_list, logger->allocator, logger->filter, 0);
    }

    PERF_COUNTER_END();
}

EXPORT void log_list_init(Log_List* log_list, Allocator* allocator)
{
    log_list_deinit(log_list);
    Log_List out = {0};
    out.allocator = allocator;
    out.filter = ~(Log_Filter) 0;
    out.logger.log = log_list_log_func;
    out.base_group_depth = *log_group_depth();

    *log_list = out;
}

EXPORT void log_list_init_capture(Log_List* log_list, Allocator* allocator)
{
    log_list_init(log_list, allocator);
    log_capture(log_list);
}

EXPORT void log_capture(Log_List* log_list)
{
    log_list->prev_logger = log_set_logger(&log_list->logger);
    log_list->had_prev_logger = true;
}

EXPORT void log_capture_end(Log_List* log_list)
{
    if(log_list->had_prev_logger)
    {
        log_list->had_prev_logger = false;
        log_set_logger(log_list->prev_logger);
    }
}



#endif