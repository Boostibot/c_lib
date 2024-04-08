#ifndef JOT_CFORMAT
#define JOT_CFORMAT

#include "vformat.h"

typedef void (*Formatter_Func)(String_Builder* append_into, const void* arg);

typedef struct Formatter {
    const void* non_zero_if_string_literal;
    const void* value;
    Formatter_Func format;
} Formatter;


static void _string_format_func(String_Builder* append_into, const void* arg)
{
    const String* str = (const String*) arg;
    builder_append(append_into, *str);
}
static void _cstring_format_func(String_Builder* append_into, const void* arg)
{
    builder_append(append_into, string_make((const char*) arg));
}

#define _DECLARE_CFORMAT_USING_PRINTF_FORMAT_NAMED(type, name, printf_format) \
    static void name ## _format_func(String_Builder* append_into, const void* arg) \
    { \
        format_append_into(append_into, "%" #printf_format, *(type*) arg); \
    } \
    
#define _DECLARE_CFORMAT_USING_PRINTF_FORMAT(type, printf_format) _DECLARE_CFORMAT_USING_PRINTF_FORMAT_NAMED(type, printf_format, printf_format)

_DECLARE_CFORMAT_USING_PRINTF_FORMAT(char, hhi)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(short, hi)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(int, i)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(long, li)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(long long, lli)

_DECLARE_CFORMAT_USING_PRINTF_FORMAT(unsigned char, hhx)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(unsigned short, hx)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(unsigned int, x)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(unsigned long, lx)
_DECLARE_CFORMAT_USING_PRINTF_FORMAT(unsigned long long, llx)

#define Fint(val) BRACE_INIT(Formatter){0, (int[]){val}, i_format_func}
#define Flli(val) BRACE_INIT(Formatter){0, (lli[]){val}, lli_format_func}
#define FEND      BRACE_INIT(Formatter){(void*) 1}

void cformat_append_into_processed(String_Builder* builder, const Formatter* formatters, isize count)
{
    for (isize i = 0; i < count; i++)
        formatters[i].format(builder, formatters[i].value);
}

void vcformat_append_into(String_Builder* builder, isize count, va_list args)
{
    Formatter formatters[64] = {0}; 
    if(count > 64)
        count = 64;

    for (isize i = 0; i < count; i++)
    {
        void* non_zero_if_string_literal = va_arg(args, void*);
        if(non_zero_if_string_literal != 0)
        {
            if(non_zero_if_string_literal == (void*)1)
            {
                count = i;
                break;
            }
            formatters[i].non_zero_if_string_literal = 0;
            formatters[i].value = non_zero_if_string_literal;
            formatters[i].format = _cstring_format_func;
        }
        else
        {
            formatters[i].non_zero_if_string_literal = 0;
            formatters[i].value = va_arg(args, void*);
            formatters[i].format = va_arg(args, Formatter_Func);
        }
    }

    cformat_append_into_processed(builder, formatters, count);
}

void cformat_append_into(String_Builder* builder, isize count, ...)
{
    va_list args;
    va_start(args, count);
    vcformat_append_into(builder, count, args);
    va_end(args);
}

void cformat_print(isize count, ...)
{
    String_Builder temp = {0};
    
    va_list args;
    va_start(args, count);
    vcformat_append_into(&temp, count, args);
    va_end(args);

    printf("%s\n", temp.data);
    builder_deinit(&temp);
}

#define CLOG(...) cformat_print(64, ##__VA_ARGS__, FEND)

#endif // !JOT_CFORMAT

#if (defined(JOT_ALL_IMPL) || defined(JOT_CFORMAT_IMPL)) && !defined(JOT_CFORMAT_HAS_IMPL)
#define JOT_CFORMAT_HAS_IMPL
    

#endif