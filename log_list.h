#ifndef JOT_LOG_LIST
#define JOT_LOG_LIST

#include "defines.h"
#include "platform.h"
#include "allocator.h"
#include "log.h"

typedef struct Log_List_Group {
    Log* first;
    Log* last;
} Log_Group;

typedef struct Log_List {
    Logger logger;
    Allocator* alloc;

    //Only log types that natch filter 
    Log_Filter filter;

    //We store an array of currently active groups
    // to supprot properly the hierarchical context
    // interface. We store the active_group inline
    // to not require any extra allocations most of the time
    // and allow zero init.
    Log_List_Group top_level_group;
    Log_List_Group* groups;
    i32 group_depth;
    i32 group_capacity;

    //For init_and_use type of tasks
    Logger* prev_logger;
    bool had_prev_logger;
} Log_List;

void _log_dealloc_recursive(Log* log_list, Allocator* alloc, isize depth)
{
    //For all practical means it should not reach such a depth.
    //Is removed in release builds
    ASSERT(depth < 100);

    for(Log* curr = log_list; curr != NULL; )
    {
        if(curr->child != NULL)
            _log_dealloc_recursive(curr->child, alloc, depth + 1);

        Log* prev = curr->prev;
        allocator_deallocate(alloc, curr, sizeof(Log) + curr->message.size + 1, DEF_ALIGN);
        curr = prev;
    }
}

void _log_alloc_recursive(Log** parents_child, const Log* log_list, Allocator* alloc, isize depth)
{
    ASSERT(depth < 100);
    for(const Log* curr = log_list; curr != NULL; curr = curr->prev)
    {
        void* allocated = allocator_allocate(alloc, sizeof(Log) + curr->message.size + 1, DEF_ALIGN);
        Log* pushed = (Log*) allocated; 
        char* copied_string = (char*) (void*) (pushed + 1);
        memcpy(copied_string, curr->message.data, curr->message.size);
        copied_string[curr->message.size] = '\0';
    
        *pushed = *curr;

        //@TODO: string make!
        pushed->message.data = copied_string;
        pushed->message.size = curr->message.size;

        pushed->prev = *parents_child;
        *parents_child = pushed;

        if(curr->child)
        {
            pushed->child = NULL;
            _log_alloc_recursive(&pushed->child, curr->child, alloc, depth + 1);
        }
    }
}

void log_list_deinit(Log_List* log_list)
{
    if(allocator_is_arena(log_list->alloc) == false && log_list->alloc != NULL)
    {
        _log_dealloc_recursive(log_list->top_level_group.last, log_list->alloc, 0);
        allocator_deallocate(log_list->alloc, log_list->groups, log_list->group_capacity * sizeof(Log_List_Group), DEF_ALIGN);
    }
        
    if(log_list->had_prev_logger)
        log_set_logger(log_list->prev_logger);

    memset(log_list, 0, sizeof *log_list);
}

void log_list_log_func(Logger* logger_, const Log* log_list, Log_Action action)
{
    Log_List* logger = (Log_List*) (void*) logger_;
    

    switch(action)
    {
        case LOG_ACTION_LOG: {
            Log* pushed = NULL;
            _log_alloc_recursive(&pushed, log_list, logger->alloc, 0);

            Log_List_Group* active_group = logger->groups + logger->group_depth;
            if(logger->group_depth == 0)
                active_group = &logger->top_level_group;

            pushed->prev = active_group->last;
            active_group->last = pushed;
            if(active_group->first == NULL)
                active_group->first = pushed;
        } break;

        case LOG_ACTION_GROUP: {
            //If needed reallocate the group array with pretty agressive growth
            if(logger->group_depth >= logger->group_capacity)
            {
                i32 new_capacity = logger->group_capacity * 2 + 8;
                logger->groups = (Log_List_Group*) allocator_reallocate(logger->alloc, new_capacity * sizeof(Log_List_Group), logger->groups, logger->group_capacity * sizeof(Log_List_Group), DEF_ALIGN);
                logger->group_capacity = new_capacity;
            }

            Log_List_Group* active_group = logger->groups + logger->group_depth;
            if(logger->group_depth == 0)
                active_group = &logger->top_level_group;

            //Push a new group and zero it
            Log_List_Group* new_active_group = &logger->groups[logger->group_depth++];
            memset(new_active_group, 0, sizeof *new_active_group);
        } break;
    
        case LOG_ACTION_UNGROUP: {
            logger->group_depth -= 1;
            if(logger->group_depth < 0)
            {
                ASSERT_MSG(false, "Too many ungroups!");
                logger->group_depth = 0;
            }
        } break;

        case LOG_ACTION_FLUSH: {
            //nothing
        } break;

        default: ASSERT_MSG(false, "Invalid action!");
    }
}

Log_List log_list_make(Allocator* alloc)
{
    Log_List out = {0};
    out.alloc = alloc;
    out.logger.log = log_list_log_func;
    return out;
}

void log_list_init(Log_List* log_list, Allocator* alloc)
{
    log_list_deinit(log_list);
    *log_list = log_list_make(alloc);
}

void log_capture(Log_List* log_list, Allocator* alloc)
{
    log_list_init(log_list, alloc);
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



