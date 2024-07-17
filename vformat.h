#ifndef JOT_VFORMAT
#define JOT_VFORMAT

#include "string.h"
#include "profile_defs.h"
#include <stdarg.h>

EXTERNAL void vformat_append_into(String_Builder* append_to, const char* format, va_list args);
EXTERNAL void format_append_into_no_check(String_Builder* append_to, const char* format, ...);
#define  format_append_into(append_to, format, ...) (sizeof printf((format), ##__VA_ARGS__), format_append_into_no_check((append_to), (format), ##__VA_ARGS__))

EXTERNAL void vformat_into(String_Builder* into, const char* format, va_list args);
EXTERNAL void format_into_no_check(String_Builder* into, const char* format, ...);
#define  format_into(into, format, ...) (sizeof printf((format), ##__VA_ARGS__), format_append_into_no_check((into), (format), ##__VA_ARGS__))

EXTERNAL String_Builder vformat(Allocator* alloc, const char* format, va_list args);
EXTERNAL String_Builder format_no_check(Allocator* alloc, const char* format, ...);
#define  format(alloc, format, ...) (sizeof printf((format), ##__VA_ARGS__), format_no_check((alloc), (format), ##__VA_ARGS__))

EXTERNAL String format_ephemeral(const char* format, ...);
EXTERNAL String vformat_ephemeral(const char* format, va_list args);
EXTERNAL const char* cstring_ephemeral(String string);

#endif // !JOT_VFORMAT

#if (defined(JOT_ALL_IMPL) || defined(JOT_VFORMAT_IMPL)) && !defined(JOT_VFORMAT_HAS_IMPL)
#define JOT_VFORMAT_HAS_IMPL
    #include <stdio.h>

    EXTERNAL void vformat_append_into(String_Builder* append_to, const char* format, va_list args)
    {
        PROFILE_START();

        if(format == NULL)
            format = "";

        //An attempt to estimate the needed size so we dont need to call vsnprintf twice.
        //We use some heuristic or the maximum capacity whichever is bigger. This saves us
        // 99% of double calls to vsnprintf which makes this function almost twice as fast
        //Because its used for pretty much all logging and setting shader uniforms thats
        // a big difference.
        isize format_size = (isize) strlen(format);
        isize estimated_size = format_size + 64 + format_size/4;
        isize base_size = append_to->len; 
        // array_resize(append_to, base_size + estimated_size);
        isize first_resize_size = MAX(base_size + estimated_size, append_to->capacity - 1);
        builder_resize(append_to, first_resize_size);

        //gcc modifies va_list on use! make sure to copy it!
        va_list args_copy;
        va_copy(args_copy, args);
        isize count = vsnprintf(append_to->data + base_size, (size_t) (append_to->len - base_size), format, args);
        
        if(count > estimated_size)
        {
            PROFILE_START(format_twice);
            builder_resize(append_to, base_size + count + 3);
            count = vsnprintf(append_to->data + base_size, (size_t) (append_to->len - base_size), format, args_copy);
            PROFILE_END(format_twice);
        }
    
        //Sometimes apparently the MSVC standard library screws up and returns negative...
        if(count < 0)
            count = 0;
        builder_resize(append_to, base_size + count);
        ASSERT(append_to->data[base_size + count] == '\0');
        
        PROFILE_END();
        return;
    }
    
    EXTERNAL void format_append_into_no_check(String_Builder* append_to, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_append_into(append_to, format, args);
        va_end(args);
    }
    
    EXTERNAL void vformat_into(String_Builder* into, const char* format, va_list args)
    {
        builder_clear(into);
        vformat_append_into(into, format, args);
    }

    EXTERNAL void format_into_no_check(String_Builder* into, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vformat_into(into, format, args);
        va_end(args);
    }
    
    EXTERNAL String_Builder vformat(Allocator* alloc, const char* format, va_list args)
    {
        String_Builder builder = builder_make(alloc, 0);
        vformat_append_into(&builder, format, args);
        return builder;
    }
    
    EXTERNAL String_Builder format_no_check(Allocator* alloc, const char* format, ...)
    {   
        va_list args;
        va_start(args, format);
        String_Builder builder = vformat(alloc, format, args);
        va_end(args);
        return builder;
    }

    EXTERNAL String vformat_ephemeral(const char* format, va_list args)
    {
        PROFILE_START();
        enum {EPHEMERAL_SLOTS = 4, RESET_EVERY = 32, KEPT_SIZE = 256};

        static ATTRIBUTE_THREAD_LOCAL String_Builder ephemeral_strings[EPHEMERAL_SLOTS] = {0};
        static ATTRIBUTE_THREAD_LOCAL isize slot = 0;

        String_Builder* curr = &ephemeral_strings[slot % EPHEMERAL_SLOTS];
        
        //We periodically shrink the strings so that we can use this
        //function regularly for small and big strings without fearing that we will
        //use too much memory
        if(slot % RESET_EVERY < EPHEMERAL_SLOTS)
        {
            if(curr->capacity == 0 || curr->capacity > KEPT_SIZE)
            {
                PROFILE_COUNTER(reset);
                builder_init_with_capacity(curr, allocator_get_static(), KEPT_SIZE);
            }
        }
        
        vformat_into(curr, format, args);

        slot += 1;
        PROFILE_END();
        return curr->string;
    }

    EXTERNAL String format_ephemeral(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        String out = vformat_ephemeral(format, args);
        va_end(args);

        return out;
    }

    EXTERNAL const char* cstring_ephemeral(String string)
    {
        PROFILE_START();

        enum {EPHEMERAL_SLOTS = 4, RESET_EVERY = 32, KEPT_SIZE = 256, PAGE_MIN_SIZE = 1024};
        const char* string_end = string.data + string.len;
        const char* out = NULL;

        //If string end is not on different memory page we cannot cause segfault and thus we can 
        // freely check if the string is escaped. If it is already escaped we simply return it.
        if((size_t) string_end % PAGE_MIN_SIZE != 0 && *string_end == '\0')
            out = string.data;
        else
        {
            PROFILE_START(required_escaping);

            static ATTRIBUTE_THREAD_LOCAL String_Builder ephemeral_strings[EPHEMERAL_SLOTS] = {0};
            static ATTRIBUTE_THREAD_LOCAL isize slot = 0;

            String_Builder* curr = &ephemeral_strings[slot % EPHEMERAL_SLOTS];
        
            //We periodically shrink the strings so that we can use this
            //function regularly for small and big strings without fearing that we will
            //use too much memory
            if(slot % RESET_EVERY < EPHEMERAL_SLOTS)
            {
                if(curr->capacity == 0 || curr->capacity > KEPT_SIZE)
                {
                    PROFILE_COUNTER(reset);
                    isize required_capacity = MAX(string.len, KEPT_SIZE);
                    builder_init_with_capacity(curr, allocator_get_static(), required_capacity);
                }
            }

            slot += 1;
            builder_assign(curr, string);
            out = curr->data;
            
            PROFILE_END(required_escaping);
        }

        PROFILE_END();
        return out;
    }

#endif