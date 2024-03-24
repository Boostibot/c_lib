#ifndef JOT_DEFINES
#define JOT_DEFINES

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

typedef int64_t    isize;
typedef uint64_t   usize;
//typedef ptrdiff_t  isize;
//typedef size_t     usize;

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;

typedef int8_t      i8;
typedef int16_t     i16;
typedef int32_t     i32;
typedef int64_t     i64;

typedef bool        b8;
typedef uint16_t    b16;
typedef uint32_t    b32;
typedef uint64_t    b64;

typedef float       f32;
typedef double      f64;

typedef long long int lli;
typedef unsigned long long llu;

typedef struct Source_Info {
    int64_t line;
    const char* file;
    const char* function;
} Source_Info;

#define isizeof (isize) sizeof
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define CLAMP(value, low, high) MAX(low, MIN(value, high))
#define DIV_CEIL(value, div_by) (((value) + (div_by) - 1) / (div_by))
#define MOD(val, range) (((val) % (range) + (range)) % (range))
#define SWAP(a_ptr, b_ptr, Type) \
    do { \
         Type temp = *(a_ptr); \
         *(a_ptr) = *(b_ptr); \
         *(b_ptr) = temp; \
    } while(0) \

#ifdef __cplusplus
    #define BRACE_INIT(Struct_Type) Struct_Type
#else
    #define BRACE_INIT(Struct_Type) (Struct_Type)
#endif 

#define SOURCE_INFO() BRACE_INIT(Source_Info){__LINE__, __FILE__, __FUNCTION__}

#define STATIC_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))


//=========================================
// Attributes
//=========================================

//See below for implementation on each compiler.

#if defined(_MSC_VER)
    #define ASSUME_UNREACHABLE() __assume(0)
    #define ATTRIBUTE_RESTRICT                                      __restrict
    #define ATTRIBUTE_INLINE_ALWAYS                                 __forceinline
    #define ATTRIBUTE_INLINE_NEVER                                  __declspec(noinline)
    #define ATTRIBUTE_THREAD_LOCAL                                  __declspec(thread)
    #define ATTRIBUTE_ALIGNED(bytes)                                __declspec(align(bytes))
    #define ATTRIBUTE_FORMAT_FUNC(format_arg, format_arg_index)     /* empty */
    #define ATTRIBUTE_FORMAT_ARG                                    _Printf_format_string_  
    #define ATTRIBUTE_RETURN_RESTRICT                               __declspec(restrict)
    #define ATTRIBUTE_RETURN_ALIGNED(align)                         /* empty */
    #define ATTRIBUTE_RETURN_ALIGNED_ARG(align_arg_index)           /* empty */
#elif defined(__GNUC__) || defined(__clang__)
    #define ASSUME_UNREACHABLE()                                                __builtin_unreachable() /*move to platform! */
    #define ATTRIBUTE_RESTRICT                                      __restrict__
    #define ATTRIBUTE_INLINE_ALWAYS                                 __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER                                  __attribute__((noinline))
    #define ATTRIBUTE_THREAD_LOCAL                                  __thread
    #define ATTRIBUTE_ALIGNED(bytes)                                __attribute__((aligned(bytes)))
    #define ATTRIBUTE_FORMAT_FUNC(format_arg, format_arg_index)     __attribute__((format_arg (printf, format_arg_index, 0)))
    #define ATTRIBUTE_FORMAT_ARG                                    /* empty */    
    #define ATTRIBUTE_NORETURN                                      __attribute__((noreturn))
    #define ATTRIBUTE_RETURN_RESTRICT                               __attribute__((malloc))
    #define ATTRIBUTE_RETURN_ALIGNED(align)                         __attribute__((assume_aligned(align))
    #define ATTRIBUTE_RETURN_ALIGNED_ARG(align_arg_index)           __attribute__((alloc_align (align_arg_index)));
#else
    #define ASSUME_UNREACHABLE()                                (*(int*)0 = 0)
    #define ATTRIBUTE_RESTRICT                                  /* C's restrict keyword. see: https://en.cppreference.com/w/c/language/restrict */
    #define ATTRIBUTE_INLINE_ALWAYS                             /* Ensures function will get inlined. Applied before function declartion. */
    #define ATTRIBUTE_INLINE_NEVER                              /* Ensures function will not get inlined. Applied before function declartion. */
    #define ATTRIBUTE_THREAD_LOCAL                              /* Declares a variable thread local. Applied before variable declarition. */
    #define ATTRIBUTE_ALIGNED(align)                            /* Places a variable on the stack aligned to 'align' */
    #define ATTRIBUTE_FORMAT_FUNC(format_arg, format_arg_index) /* Marks a function as formatting function. Applied before function declartion. See log.h for example */
    #define ATTRIBUTE_FORMAT_ARG                                /* Marks a format argument. Applied before const char* format argument. See log.h for example */  
    #define ATTRIBUTE_NORETURN                                  /* Specifices that this function will not return (for example abort, exit ...) . Applied before function declartion. */
    #define ATTRIBUTE_RETURN_RESTRICT                           /* Specifies that the retuned pointer from this function does not align any other obejct. Most often used on allocators. */
    #define ATTRIBUTE_RETURN_ALIGNED(align)                     /* Specifies that the retuned pointer is aligned to align. */
    #define ATTRIBUTE_RETURN_ALIGNED_ARG(align_arg_index)       /* Specifies that the retuned pointer is aligned to the integer value of argument at align_arg_index position. */
#endif

#ifndef EXPORT
    #define EXPORT
#endif
#ifndef INTERNAL
    #define INTERNAL static
#endif

#ifndef _MSC_VER
    #define __FUNCTION__ __func__
#endif

#endif