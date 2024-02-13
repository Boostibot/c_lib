#ifndef JOT_ERROR
#define JOT_ERROR

#include "arena.h"

typedef Platform_String String;

typedef struct Error {
    const char* module;
    const char* type;
    String message;
    i64 time;
    Source_Info location;

    struct Error* prev;
    struct Error* child;

    isize data_from; 
    isize data_to; 
    isize generation;
} Error;

typedef struct Error_Buffer {
    Arena* arena_error;
    Arena* arena_error_data;
    bool is_growing;

    Error* errors;
    u8* error_data;

    isize error_size;
    isize error_capacity;
    isize error_data_capaicty;
    
    isize error_from;
    isize error_to;
    
    isize error_data_from;
    isize error_data_to;

    isize generation;
} Error_Buffer;

typedef struct Error_List {
    Error_Buffer* buffer;

    isize error_from;
    isize error_to;

    isize error_data_from;
    isize error_data_to;

    isize generation_from;
    isize generation_to;
} Error_List;

Error_List error_list_begin(Error_Buffer* buffer)
{
    Error_List list = {buffer};

    list.error_from = buffer->error_to;
    list.error_to = buffer->error_to;

    list.error_data_from = buffer->error_data_to;
    list.error_data_to = buffer->error_data_to;

    list.generation_from = buffer->generation;
    list.generation_to = buffer->generation;

    return list;
}

void error_list_end(Error_List* list)
{
    list->error_to = list->buffer->error_to;
    list->error_data_to = list->buffer->error_data_to;
    list->generation_to = list->buffer->generation;
}

Error* error_push(Error_Buffer* buffer, String message)
{
    isize push_to = buffer->error_to;
    if(buffer->error_to == buffer->error_capacity)
    {
        if(buffer->is_growing)
            ASSERT(false && "TODO");
        else
        {
            push_to = 0;
            buffer->generation += 1;
        }
    }


    if(message.size > buffer->error_data_capaicty)
        message.size = 0;

    ASSERT(buffer->error_capacity > 0);

    while(buffer->error_to + message.size > buffer->error_data_capaicty || buffer->error_to + message.size > buffer->error_from)
    {
        CHECK_BOUNDS(buffer->error_from, buffer->error_data_capaicty);
        Error* first = &buffer->errors[buffer->error_from];
        if(first->generation > 0)
        {
            buffer->error_from = first->data_to;
            buffer->error_size -= 1;
            first->generation = buffer->generation;

            if(buffer->error_size <= 0)
                break;
        }
    }
    
    if(buffer->error_size <= 0)
    {
        buffer->error_from = 0;
        buffer->error_to = 0;
        
        buffer->error_data_from = 0;
        buffer->error_data_to = 0;
        buffer->generation += 1;
    }
}

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_ERROR_IMPL)) && !defined(JOT_ERROR_HAS_IMPL)
#define JOT_ERROR_HAS_IMPL

#endif