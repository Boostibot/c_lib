#ifndef LIB_VFORMAT
#define LIB_VFORMAT

#include "string.h"
#include "profile.h"
#include <stdarg.h>

EXPORT void vformat_append_into(String_Builder* append_to, const char* format, va_list args);
EXPORT void vformat_append_into_sized(String_Builder* append_to, String format, va_list args);

EXPORT void format_append_into(String_Builder* append_to, const char* format, ...);
EXPORT void format_append_into_sized(String_Builder* append_to, String format, ...);

EXPORT void vformat_into(String_Builder* into, const char* format, va_list args);
EXPORT void vformat_into_sized(String_Builder* into, String format, va_list args);

EXPORT void format_into(String_Builder* into, const char* format, ...);
EXPORT void format_into_sized(String_Builder* into, String format, ...);

EXPORT String format_ephemeral(const char* format, ...);

#define CSTRING_ESCAPE(s) (s) == NULL ? "" : (s)

#define STRING_FMT "%.*s"
#define STRING_PRINT(string) (string).size, (string).data

#define SOURCE_INFO_FMT "( %s : %lli )"
#define SOURCE_INFO_PRINT(source_info) cstring_escape((source_info).file), (source_info).line

typedef long long int lli;
typedef unsigned long long llu;
#endif // !LIB_VFORMAT

#if (defined(LIB_ALL_IMPL) || defined(LIB_VFORMAT_IMPL)) && !defined(LIB_VFORMAT_HAS_IMPL)
#define LIB_VFORMAT_HAS_IMPL
    #include <stdio.h>

    EXPORT void vformat_append_into(String_Builder* append_to, const char* format, va_list args)
    {
        PERF_COUNTER_START(c);
        //an attempt to estimate the needed size so we dont need to call vsnprintf twice
        isize format_size = strlen(format);
        isize estimated_size = format_size + 64 + format_size/4;
        isize base_size = append_to->size; 
        array_resize(append_to, base_size + estimated_size);

        va_list args_copy;
        va_copy(args_copy, args);
        isize count = vsnprintf(append_to->data + base_size, (size_t) (append_to->size - base_size), format, args);
        
        if(count > estimated_size)
        {
            PERF_COUNTER_START(format_twice);
            array_resize(append_to, base_size + count + 3);
            count = vsnprintf(append_to->data + base_size, (size_t) (append_to->size - base_size), format, args);
            PERF_COUNTER_END(format_twice);
        }
    
        //Sometimes apparently the standard linrary screws up and returns negative...
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

    EXPORT void format_append_into(String_Builder* append_to, const char* format, ...)
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

    EXPORT void format_into(String_Builder* into, const char* format, ...)
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
    
    EXPORT String format_ephemeral(const char* format, ...)
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
#endif