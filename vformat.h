#ifndef JOT_VFORMAT
#define JOT_VFORMAT

#include "string.h"
#include "profile.h"
#include <stdarg.h>

EXPORT void vformat_append_into(String_Builder* append_to, const char* format, va_list args);
EXPORT void vformat_append_into_sized(String_Builder* append_to, String format, va_list args);

EXPORT MODIFIER_FORMAT_FUNC(format, 2) void format_append_into(String_Builder* append_to, MODIFIER_FORMAT_ARG const char* format, ...);
EXPORT void format_append_into_sized(String_Builder* append_to, String format, ...);

EXPORT void vformat_into(String_Builder* into, const char* format, va_list args);
EXPORT void vformat_into_sized(String_Builder* into, String format, va_list args);

EXPORT MODIFIER_FORMAT_FUNC(format, 2) void format_into(String_Builder* into, MODIFIER_FORMAT_ARG const char* format, ...);
EXPORT void format_into_sized(String_Builder* into, String format, ...);

EXPORT MODIFIER_FORMAT_FUNC(format, 1) String format_ephemeral(MODIFIER_FORMAT_ARG const char* format, ...);
EXPORT const char* escape_string_ephemeral(String string);

#define CSTRING_ESCAPE(s) (s) == NULL ? "" : (s)

#define STRING_FMT "%.*s"
#define STRING_PRINT(string) (int) (string).size, (string).data

#define SOURCE_INFO_FMT "( %s : %lli )"
#define SOURCE_INFO_PRINT(source_info) cstring_escape((source_info).file), (lli) (source_info).line

#endif // !JOT_VFORMAT

#if (defined(JOT_ALL_IMPL) || defined(JOT_VFORMAT_IMPL)) && !defined(JOT_VFORMAT_HAS_IMPL)
#define JOT_VFORMAT_HAS_IMPL
    #include <stdio.h>

    EXPORT void vformat_append_into(String_Builder* append_to, const char* format, va_list args)
    {
        PERF_COUNTER_START(c);
        //An attempt to estimate the needed size so we dont need to call vsnprintf twice.
        //We use some heuristic or the maximum capacity whichever is bigger. This saves us
        // 99% of double calls to vsnprintf which makes this function almost twice as fast
        //Because its used for pretty much all logging and setting shader uniforms thats
        // a big difference.
        isize format_size = (isize) strlen(format);
        isize estimated_size = format_size + 64 + format_size/4;
        isize base_size = append_to->size; 
        // array_resize(append_to, base_size + estimated_size);
        isize first_resize_size = MAX(base_size + estimated_size, append_to->capacity - 1);
        array_resize(append_to, first_resize_size);

        //gcc modifies va_list on use! make sure to copy it!
        va_list args_copy;
        va_copy(args_copy, args);
        isize count = vsnprintf(append_to->data + base_size, (size_t) (append_to->size - base_size), format, args);
        
        if(count > estimated_size)
        {
            PERF_COUNTER_START(format_twice);
            array_resize(append_to, base_size + count + 3);
            count = vsnprintf(append_to->data + base_size, (size_t) (append_to->size - base_size), format, args_copy);
            PERF_COUNTER_END(format_twice);
        }
    
        //Sometimes apparently the msvc standard linrary screws up and returns negative...
        if(count < 0)
            count = 0;
        array_resize(append_to, base_size + count);
        ASSERT(append_to->data[base_size + count] == '\0');
        
        PERF_COUNTER_END(c);
        return;
    }

    EXPORT void vformat_append_into_sized(String_Builder* append_to, String format, va_list args)
    {
        String_Builder escaped = {0};
        array_init_backed(&escaped, allocator_get_scratch(), 1024);
        builder_append(&escaped, format);
    
        vformat_append_into(append_to, cstring_from_builder(escaped), args);

        array_deinit(&escaped);
    }
    
    EXPORT MODIFIER_FORMAT_FUNC(format, 2) void format_append_into(String_Builder* append_to, MODIFIER_FORMAT_ARG const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_append_into(append_to, format, args);
        va_end(args);
    }

    EXPORT void format_append_into_sized(String_Builder* append_to, String format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_append_into_sized(append_to, format, args);
        va_end(args);
    }
    
    EXPORT void vformat_into(String_Builder* into, const char* format, va_list args)
    {
        array_clear(into);
        vformat_append_into(into, format, args);
    }

    EXPORT void vformat_into_sized(String_Builder* into, String format, va_list args)
    {
        array_clear(into);
        vformat_append_into_sized(into, format, args);
    }

    EXPORT MODIFIER_FORMAT_FUNC(format, 2) void format_into(String_Builder* into, MODIFIER_FORMAT_ARG const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_into(into, format, args);
        va_end(args);
    }

    EXPORT void format_into_sized(String_Builder* into, String format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_into_sized(into, format, args);
        va_end(args);
    }
    
    EXPORT MODIFIER_FORMAT_FUNC(format, 1) String format_ephemeral(MODIFIER_FORMAT_ARG const char* format, ...)
    {
        enum {EPHEMERAL_SLOTS = 4, RESET_EVERY = 32, KEPT_SIZE = 256};

        static String_Builder ephemeral_strings[EPHEMERAL_SLOTS] = {0};
        static isize slot = 0;

        String_Builder* curr = &ephemeral_strings[slot % EPHEMERAL_SLOTS];
        
        //We periodacally shrink the strinks so that we can use this
        //function regulary for small and big strings without fearing that we will
        //use too much memory
        if(slot % RESET_EVERY < EPHEMERAL_SLOTS)
        {
            if(curr->capacity == 0 || curr->capacity > KEPT_SIZE)
            {
                array_init(curr, allocator_get_static());
                array_set_capacity(curr, KEPT_SIZE);
            }
        }
        
        va_list args;
        va_start(args, format);
        vformat_into(curr, format, args);
        va_end(args);

        slot += 1;

        String out = string_from_builder(*curr);
        return out;
    }

    EXPORT const char* escape_string_ephemeral(String string)
    {
        enum {EPHEMERAL_SLOTS = 4, RESET_EVERY = 32, KEPT_SIZE = 256, PAGE_MIN_SIZE = 1024};

        const char* string_end = string.data + string.size;

        //If string end is not on different memory page we cannot cause segfault and thus we can 
        // freely check if the string is escaped. If it is already escaped we simply return it.
        if((size_t) string_end % PAGE_MIN_SIZE != 0)
        {
            if(*string_end == '\0')
                return string.data;
        }

        static String_Builder ephemeral_strings[EPHEMERAL_SLOTS] = {0};
        static isize slot = 0;

        String_Builder* curr = &ephemeral_strings[slot % EPHEMERAL_SLOTS];
        
        //We periodacally shrink the strinks so that we can use this
        //function regulary for small and big strings without fearing that we will
        //use too much memory
        if(slot % RESET_EVERY < EPHEMERAL_SLOTS)
        {
            if(curr->capacity == 0 || curr->capacity > KEPT_SIZE)
            {
                array_init(curr, allocator_get_static());
                isize required_capacity = MAX(string.size, KEPT_SIZE);
                array_reserve(curr, required_capacity);
            }
        }

        slot += 1;
        builder_assign(curr, string);
        return curr->data;
    }

#endif