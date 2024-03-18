#ifndef JOT_LOG_LIST
#define JOT_LOG_LIST

#include "defines.h"
#include "platform.h"
#include "allocator.h"
#include "log.h"

typedef struct Log_List {
    Logger logger;
    Allocator* allocator;
    Log_Filter filter;

    Log* first;
    Log* last;

    isize size;

    //For init_and_use type of tasks
    Logger* prev_logger;
    bool had_prev_logger;
} Log_List;

void _log_dealloc_recursive(Log* log_list, Allocator* allocator, isize depth)
{
    //For all practical means it should not reach such a depth.
    //Is removed in release builds
    ASSERT(depth < 100);

    for(Log* curr = log_list; curr != NULL && curr != log_list->next; )
    {
        if(curr->first_child != NULL)
            _log_dealloc_recursive(curr->first_child, allocator, depth + 1);

        Log* next = curr->next;
        allocator_deallocate(allocator, curr, sizeof(Log) + curr->message.size + 1, DEF_ALIGN);
        curr = next;
    }
}

void _log_alloc_recursive(Log** first_child_ptr, Log** last_child_ptr, const Log* log_list, Allocator* allocator, Log_Filter filter, isize depth)
{
    ASSERT(depth < 100);
    for(const Log* curr = log_list; curr != NULL; curr = curr->next)
    {
        if(filter & (Log_Filter) 1 << curr->type)
        {
            //Coalesce allocation of the Log and the message into one.
            void* allocated = allocator_allocate(allocator, sizeof(Log) + curr->message.size + 1, DEF_ALIGN);

            //Copy everything except pointers
            Log* pushed = (Log*) allocated; 
            *pushed = *curr;
            pushed->first_child = NULL;
            pushed->last_child = NULL;
            pushed->prev = NULL;
            pushed->next = NULL;
        
            //copy string
            char* copied_string = (char*) (void*) (pushed + 1);
            memcpy(copied_string, curr->message.data, curr->message.size);
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

void log_list_deinit(Log_List* log_list)
{
    if(allocator_is_arena(log_list->allocator) == false && log_list->allocator != NULL)
        _log_dealloc_recursive(log_list->first, log_list->allocator, 0);
        
    if(log_list->had_prev_logger)
        log_set_logger(log_list->prev_logger);

    memset(log_list, 0, sizeof *log_list);
}

void log_list_log_func(Logger* logger_, const Log* log_list, i32 group_depth, Log_Action action)
{
    Log_List* logger = (Log_List*) (void*) logger_;

    if(action == LOG_ACTION_LOG && (logger->filter & (Log_Filter) 1 << log_list->type))
    {
        ASSERT(group_depth >= 0);
        group_depth = MAX(group_depth, 0);

        //@NOTE: Slow but reliable approach. Can be made faster by using lookup table but I am tired.
        i32 reached_depth = 0;
        Log** first = &logger->first;
        Log** last = &logger->last;

        logger->size += 1;
        //Try to reach the desired depth and allocate there
        for(; reached_depth < group_depth && *last != NULL; reached_depth += 1)
        {
            reached_depth += 1;
            Log* last_log = *last;
            first = &last_log->first_child;
            last = &last_log->last_child;
        }

        //If there it still needs extra depth just allocate empty entries
        for(; reached_depth < group_depth; reached_depth += 1)
        {
            Log empty = {"", ""};
            _log_alloc_recursive(first, last, &empty, logger->allocator, logger->filter, 0);
                
            Log* last_log = *last;
            first = &last_log->first_child;
            last = &last_log->last_child;
        }
            
        ASSERT(first != NULL);
        ASSERT(last != NULL);
            
        _log_alloc_recursive(first, last, log_list, logger->allocator, logger->filter, 0);
    }
}

Log_List log_list_make(Allocator* allocator)
{
    Log_List out = {0};
    out.allocator = allocator;
    out.filter = ~(Log_Filter) 0;
    out.logger.log = log_list_log_func;
    return out;
}

void log_list_init(Log_List* log_list, Allocator* allocator)
{
    log_list_deinit(log_list);
    *log_list = log_list_make(allocator);
}

void log_capture(Log_List* log_list, Allocator* allocator)
{
    log_list_init(log_list, allocator);
    log_list->prev_logger = log_set_logger(&log_list->logger);
    log_list->had_prev_logger = true;
}

void log_capture_end(Log_List* log_list)
{
    if(log_list->had_prev_logger)
    {
        log_list->had_prev_logger = false;
        log_set_logger(log_list->prev_logger);
    }
}

#endif