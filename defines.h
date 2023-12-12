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

#ifndef _MSC_VER
    #define __FUNCTION__ __func__
#endif

#endif