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

typedef char        c8;
typedef wchar_t     c16;
typedef uint32_t    c32;

#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define CLAMP(value, low, high) MAX(low, MIN(value, high))
#define DIV_ROUND_UP(value, div_by) (((value) + (div_by) - 1) / (div_by))
#define MODULO(val, range) (((val) % (range) + (range)) % (range))
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

#define STATIC_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#ifndef EXPORT
    #define EXPORT
#endif
#ifndef INTERNAL
    #define INTERNAL static
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define RESTRICT __restrict__
    #define FORCE_INLINE __attribute__((always_inline))
    #define NO_INLINE __attribute__((noinline))
    #define THREAD_LOCAL __thread
    #define ALIGNED(bytes) __attribute__((aligned(bytes)))
    #define ASSUME_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
    #define FORCE_INLINE __forceinline
    #define NO_INLINE __declspec(noinline)
    #define THREAD_LOCAL __declspec(thread)
    #define ALIGNED(bytes) __declspec(align(bytes))
    #define ASSUME_UNREACHABLE() __assume(0)
#else
    #define RESTRICT
    #define FORCE_INLINE
    #define NO_INLINE
    #define THREAD_LOCAL __thread
    #define ASSUME_UNREACHABLE()
#endif

#ifndef _MSC_VER
    #define __FUNCTION__ __func__
#endif

#endif