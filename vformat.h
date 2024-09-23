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
#define  formata(allocator, format, ...) (sizeof printf((format), ##__VA_ARGS__), format_no_check((allocator).alloc, (format), ##__VA_ARGS__)).string

EXTERNAL String translate_error(Allocator* alloc, Platform_Error error);
EXTERNAL String_Builder translate_error_builder(Allocator* alloc, Platform_Error error);

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

    EXTERNAL String_Builder translate_error_builder(Allocator* alloc, Platform_Error error)
    {
        isize size = platform_translate_error(error, NULL, 0);
        String_Builder out = builder_make(alloc, size - 1);
        platform_translate_error(error, out.data, size);

        ASSERT(builder_is_invariant(out));
        return out;
    }

    EXTERNAL String translate_error(Allocator* alloc, Platform_Error error)
    {
        isize size = platform_translate_error(error, NULL, 0);
        char* data = allocator_allocate(alloc, size, 1);
        platform_translate_error(error, data, size);
        return string_make(data, size - 1);
    }
#endif
