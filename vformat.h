#ifndef MODULE_VFORMAT
#define MODULE_VFORMAT

#include "string.h"
#include "profile.h"
#include <stdarg.h>

EXTERNAL void vformat_append_into(String_Builder* append_to, const char* format, va_list args);
EXTERNAL void format_append_into_no_check(String_Builder* append_to, const char* format, ...);
#define  format_append_into(append_to, format, ...) ((void) sizeof printf((format), ##__VA_ARGS__), format_append_into_no_check((append_to), (format), ##__VA_ARGS__))

EXTERNAL void vformat_into(String_Builder* into, const char* format, va_list args);
EXTERNAL void format_into_no_check(String_Builder* into, const char* format, ...);
#define  format_into(into, format, ...) ((void) sizeof printf((format), ##__VA_ARGS__), format_append_into_no_check((into), (format), ##__VA_ARGS__))

EXTERNAL String_Builder vformat(Allocator* alloc, const char* format, va_list args);
EXTERNAL String_Builder format_no_check(Allocator* alloc, const char* format, ...);

#define  format_builder(allocator, format, ...) ((void) sizeof printf((format), ##__VA_ARGS__), format_no_check((allocator), (format), ##__VA_ARGS__))
#define  format(allocator, format, ...) format_builder((allocator), (format), ##__VA_ARGS__).string

EXTERNAL String translate_error(Allocator* alloc, Platform_Error error);
EXTERNAL String_Builder translate_error_builder(Allocator* alloc, Platform_Error error);

#endif // !MODULE_VFORMAT

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_VFORMAT)) && !defined(MODULE_HAS_IMPL_VFORMAT)
#define MODULE_HAS_IMPL_VFORMAT
    #include <stdio.h>

    EXTERNAL void vformat_append_into(String_Builder* append_to, const char* format, va_list args)
    {
        PROFILE_START();
        if(format != NULL)
        {
            char local[512];
            va_list args_copy;
            va_copy(args_copy, args);

            int size = vsnprintf(local, sizeof local, format, args_copy);
            isize base_size = append_to->count; 
            builder_resize_for_overwrite(append_to, append_to->count + size);

            if(size > sizeof local) {
                PROFILE_INSTANT("format twice")
                vsnprintf(append_to->data + base_size, size + 1, format, args);
            }
            else
                memcpy(append_to->data + base_size, local, size);

        }
        PROFILE_STOP();
        ASSERT(builder_is_invariant(*append_to));
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
        return translate_error_builder(alloc, error).string;
    }
#endif
