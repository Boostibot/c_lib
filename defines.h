#ifndef JOT_DEFINES
#define JOT_DEFINES

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdbool.h>

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

//@TODO: remove
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

//@TODO remove
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

#define SOURCE_INFO() BRACE_INIT(Source_Info){__LINE__, __FILE__, __func__}

#define ARRAY_SIZE(array) (isize) (sizeof(array) / sizeof((array)[0]))

//MIN and MAX of types
#define IS_SIGNED(T)		((T) ~(T) 0 < 0) 
#define MAX_OF(T)			((T) (0xFFFFFFFFFFFFFFFFULL >> (64 - sizeof(T)*8 + IS_SIGNED(T))))
#define MIN_OF(T)			((T) (-IS_SIGNED(T)*((0x7FFFFFFFFFFFFFFFULL >> (64-sizeof(T)*8)) + 1)))

//=========================================
// Attributes
//=========================================

//See below for implementation on each compiler.

#if defined(_MSC_VER)
    #define ASSUME_UNREACHABLE()                                    __assume(0)
    #define ATTRIBUTE_RESTRICT                                      __restrict
    #define ATTRIBUTE_INLINE_ALWAYS                                 __forceinline
    #define ATTRIBUTE_INLINE_NEVER                                  __declspec(noinline)
    #define ATTRIBUTE_THREAD_LOCAL                                  __declspec(thread)
    #define ATTRIBUTE_ALIGNED(bytes)                                __declspec(align(bytes))
    #define ATTRIBUTE_NORETURN                                      __declspec(noreturn)
    #define ATTRIBUTE_ALLOCATOR(size_arg_index, align_arg_index)    __declspec(restrict)
#elif defined(__GNUC__) || defined(__clang__)
    #define ASSUME_UNREACHABLE()                                    __builtin_unreachable() 
    #define ATTRIBUTE_RESTRICT                                      __restrict__
    #define ATTRIBUTE_INLINE_ALWAYS                                 __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER                                  __attribute__((noinline))
    #define ATTRIBUTE_THREAD_LOCAL                                  __thread
    #define ATTRIBUTE_ALIGNED(bytes)                                __attribute__((aligned(bytes)))
    #define ATTRIBUTE_NORETURN                                      __attribute__((noreturn))
    #define ATTRIBUTE_ALLOCATOR(size_arg_index, align_arg_index)    __attribute__((malloc, alloc_size(size_arg_index), alloc_align(align_arg_index)))
#else
    #define ASSUME_UNREACHABLE()                                (*(int*)0 = 0)
    #define ATTRIBUTE_RESTRICT                                  /* C's restrict keyword. see: https://en.cppreference.com/w/c/language/restrict */
    #define ATTRIBUTE_INLINE_ALWAYS                             /* Ensures function will get inlined. Applied before function declartion. */
    #define ATTRIBUTE_INLINE_NEVER                              /* Ensures function will not get inlined. Applied before function declartion. */
    #define ATTRIBUTE_THREAD_LOCAL                              /* Declares a variable thread local. Applied before variable declarition. */
    #define ATTRIBUTE_ALIGNED(align)                            /* Places a variable on the stack aligned to 'align' */
    #define ATTRIBUTE_NORETURN                                  /* Specifices that this function will not return (for example abort, exit ...) . Applied before function declartion. */
    #define ATTRIBUTE_ALLOCATOR(size_arg_index, align_arg_index)
#endif

#ifndef EXTERNAL
    #define EXTERNAL
#endif
#ifndef INTERNAL
    #define INTERNAL static
#endif

#endif