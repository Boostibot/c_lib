#ifndef MODULE_DEFINES
#define MODULE_DEFINES

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

typedef int64_t    isize;

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

typedef long long int      lli;
typedef unsigned long long llu;

#ifndef EXTERNAL
    #define EXTERNAL
#endif

#ifndef INTERNAL
    #define INTERNAL inline static
#endif

#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#define CLAMP(value, low, high) MAX(low, MIN(value, high))
#define MOD(val, range)         (((val) % (range) + (range)) % (range)) //just like % but allows negatives: MOD(-2, 5) == 3 
#define ARRAY_COUNT(array)      ((isize) (sizeof(array) / sizeof (array)[0]))

#define DIV_CEIL(a, b)          ((a) / (b) + ((a) % (b) > 0 ? 1 : 0))               //a/b rounding towards positive infinity
#define DIV_FLOOR(a, b)         ((a) / (b) + ((a) % (b) < 0 ? -1 : 0))              //a/b rounding towards negative infinity
#define DIV_AWAY(a, b)          (((a) >= 0 ? (a) + (b) - 1  : (a) - (b) + 1) / (b)) //a/b rounding away from zero

#define CACHE_LINE 64
#define PAGE_BYTES 4096
#define KB (1LL << 10)
#define MB (1LL << 20)
#define GB (1LL << 30)
#define TB (1LL << 40)

#define SWAP(a, b) do { \
    void* _x = (a); \
    void* _y = (b); \
    char _t[sizeof *(a)]; \
    size_t _N = sizeof *(a); \
    memcpy(_t, _x, _N); \
    memcpy(_x, _y, _N); \
    memcpy(_y, _t, _N); \
} while(0) \

#ifdef __cplusplus
    #define SINIT(Struct_Type) Struct_Type
#else
    #define SINIT(Struct_Type) (Struct_Type)
#endif 

#if defined(_MSC_VER)
    #define ATTRIBUTE_RESTRICT           __restrict
    #define ATTRIBUTE_INLINE_ALWAYS      __forceinline
    #define ATTRIBUTE_INLINE_NEVER       __declspec(noinline)
    #define ATTRIBUTE_THREAD_LOCAL       __declspec(thread)
    #define ATTRIBUTE_ALIGNED(bytes)     __declspec(align(bytes))
    #define ATTRIBUTE_NORETURN           __declspec(noreturn)
    #define ATTRIBUTE_RESTRICT_RETURN    __declspec(restrict)
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_RESTRICT           __restrict__
    #define ATTRIBUTE_INLINE_ALWAYS      __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER       __attribute__((noinline))
    #define ATTRIBUTE_THREAD_LOCAL       __thread
    #define ATTRIBUTE_ALIGNED(bytes)     __attribute__((aligned(bytes)))
    #define ATTRIBUTE_NORETURN           __attribute__((noreturn))
    #define ATTRIBUTE_RESTRICT_RETURN    __attribute__((malloc))
#else
    #define ATTRIBUTE_RESTRICT           
    #define ATTRIBUTE_INLINE_ALWAYS      inline
    #define ATTRIBUTE_INLINE_NEVER       
    #define ATTRIBUTE_THREAD_LOCAL       _Thread_local 
    #define ATTRIBUTE_ALIGNED(align)     _Alignas(align) 
    #define ATTRIBUTE_NORETURN           
    #define ATTRIBUTE_RESTRICT_RETURN    
#endif

#endif