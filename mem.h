#ifndef MODULE_MEM
#define MODULE_MEM

#include <stdint.h>
#include <string.h>
typedef int64_t isize;

#ifndef EXTERNAL
    #define EXTERNAL
#endif

//Tiles pattern_size bytes long pattern across field of field_size bytes. 
//The first occurance of pattern is placed at the very start of field and subsequent repetitions follow. 
//If the field_size % pattern_size != 0 the last repetition of pattern is trimmed.
//If pattern_size == 0 field is filled with zeros instead.
EXTERNAL void memtile(void *field, isize field_size, const void* pattern, isize pattern_size);
//Swaps the contents of the memory blocks a and b
EXTERNAL void memswap(void* a, void* b, isize size);

EXTERNAL isize memfind(const void* ptr, uint8_t value, isize size);
EXTERNAL isize memfind_last(const void* ptr, uint8_t value, isize size);
EXTERNAL isize memfind_not(const void* ptr, uint8_t value, isize size);
EXTERNAL isize memfind_last_not(const void* ptr, uint8_t value, isize size);

//finds the first byte not matching the given 8B pattern (assumed to be in little endian).
//Is intended to be used in conjuction with mem_broadcast8, mem_broadcast16, mem_broadcast32 to create the mask
EXTERNAL isize memfind_pattern_not(const void* ptr, uint64_t value, isize size);
EXTERNAL isize memfind_pattern_last_not(const void* ptr, uint64_t value, isize size); //Same thing as memfind_pattern_not except in reverse

//SWAR programming utils (taken from bit twiddling hacks)
// https://graphics.stanford.edu/~seander/bithacks.html
static inline uint64_t mem_broadcast8(uint8_t val)      {return 0x0101010101010101ull*val;}
static inline uint64_t mem_broadcast16(uint16_t val)    {return 0x0001000100010001ull*val;}
static inline uint64_t mem_broadcast32(uint32_t val)    {return (val << 32) | val;}
static inline uint64_t mem_has_zero_byte(uint64_t val)  {return (val - 0x01010101ull) & ~val & 0x80808080ull; }

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_MEM)) && !defined(MODULE_HAS_IMPL_MEM)
#define MODULE_HAS_IMPL_MEM

#ifndef PROFILE_START
    #define PROFILE_START()
    #define PROFILE_STOP()
#endif

#ifndef REQUIRE
    #include <assert.h> 
    #define REQUIRE(x, ...) assert(x)
#endif

EXTERNAL void memtile(void *field, isize field_size, const void* pattern, isize pattern_size)
{
    PROFILE_START();
    REQUIRE(field_size >= 0 && (field || field_size == 0));
    REQUIRE(pattern_size >= 0 && (pattern || pattern_size == 0));

    if (field_size <= pattern_size)
        memcpy(field, pattern, (size_t) field_size);
    else if(pattern_size == 0)
        memset(field, 0, (size_t) field_size);
    else
    {
        isize cursor = pattern_size;
        isize copy_size = pattern_size;

        // make one full copy
        memcpy((char*) field, pattern, (size_t) pattern_size);
    
        // now copy from destination buffer, doubling size each iteration
        for (; cursor + copy_size < field_size; copy_size *= 2) 
        {
            memcpy((char*) field + cursor, field, (size_t) copy_size);
            cursor += copy_size;
        }
    
        // copy any remainder
        memcpy((char*) field + cursor, field, (size_t) (field_size - cursor));
    }
    PROFILE_STOP();
}

EXTERNAL void memswap_generic(void* a, void* b, isize size)
{
    PROFILE_START();
    REQUIRE(size >= 0 && ((a && b) || size == 0));
    enum {LOCAL = 16};
    char temp[LOCAL] = {0};

    char* ac = (char*) a;
    char* bc = (char*) b;

    size_t repeats = (size_t) size / LOCAL;
    size_t remainder = (size_t) size % LOCAL;
    for(size_t k = 0; k < repeats; k ++)
    {
        memcpy(temp,         ac + k*LOCAL, LOCAL);
        memcpy(ac + k*LOCAL, bc + k*LOCAL, LOCAL);
        memcpy(bc + k*LOCAL, temp,         LOCAL);
    }

    ac += repeats*LOCAL;
    bc += repeats*LOCAL;
    for(size_t i = 0; i < remainder; i++)
    {
        char t = ac[i];
        ac[i] = bc[i];
        bc[i] = t;         
    }
    PROFILE_STOP();
}

EXTERNAL void memswap(void* a, void* b, isize size)
{
    REQUIRE(size >= 0 && ((a && b) || size == 0));
    PROFILE_START();
    char temp[32] = {0};
    switch(size) {
        #define SWAP_X(N) \
            case N: { \
                memcpy(temp, a, N); \
                memcpy(a, b, N); \
                memcpy(b, temp, N); \
            } break;

        SWAP_X(1)
        SWAP_X(2)
        SWAP_X(4)
        SWAP_X(8)
        SWAP_X(12)
        SWAP_X(16)
        SWAP_X(20)
        SWAP_X(24)
        SWAP_X(28)
        SWAP_X(32)
        SWAP_X(64)
        #undef SWAP_X

        default: memswap_generic(a, b, size); break;
    }
    PROFILE_STOP();
}

EXTERNAL isize memfind_pattern_not(const void* ptr, uint64_t val, isize size)
{
    REQUIRE(size >= 0 && (ptr != NULL || size == 0));
    uint8_t* curr = (uint8_t*) ptr;
    uint8_t* end = curr + size;
    for(; end - curr >= 32; curr += 32) {
        uint64_t c[4]; memcpy(c, curr, 32);
        if(c[0] != val || c[1] != val || c[2] != val || c[3] != val) 
            break;
    }

    for(; end - curr >= 8; curr += 8) {
        uint64_t c; memcpy(&c, curr, 8);
        if(c != val)
            break; 
    }
    
    uint64_t val_copy = val;
    for(; end != curr; curr ++, val_copy >>= 8) {
        if(*curr != (uint8_t) val_copy)
            return curr - (uint8_t*) ptr;
    }

    return -1;
}

EXTERNAL isize memfind_pattern_last_not(const void* ptr, uint64_t val, isize size)
{
    REQUIRE(size >= 0 && (ptr != NULL || size == 0));
    uint8_t* curr = (uint8_t*) ptr + size;
    uint8_t* start = (uint8_t*) ptr;
    for(; curr - start >= 32; curr -= 32) {
        uint64_t c[4]; memcpy(c, curr - 32, 32);
        if(c[0] != val || c[1] != val || c[2] != val || c[3] != val) 
            break;
    }

    for(; curr - start >= 8; curr -= 8) {
        uint64_t c; memcpy(&c, curr - 8, 8);
        if(c != val)
            break; 
    }
    
    uint64_t val_copy = val;
    for(; start != curr; curr ++, val_copy <<= 8) {
        uint8_t hi_byte = val_copy >> (64 - 8);
        if(*curr != (uint8_t) val_copy)
            return curr - (uint8_t*) ptr;
    }

    return -1;
}

EXTERNAL isize memfind_last_swar(const void* ptr, uint8_t value, isize size)
{
    //This could be a lot faster using simd but I didnt 
    REQUIRE(size >= 0 && (ptr != NULL || size == 0));
    uint8_t* curr = (uint8_t*) ptr + size;
    uint8_t* start = (uint8_t*) ptr;

    uint64_t val = value*0x0101010101010101ull;
    for(; curr - start >= 32; curr -= 32) {
        uint64_t c[4]; memcpy(c, curr - 32, 32);
        uint64_t m[4];
        m[0] = mem_has_zero_byte(c[0] ^ val);
        m[1] = mem_has_zero_byte(c[1] ^ val);
        m[2] = mem_has_zero_byte(c[2] ^ val);
        m[3] = mem_has_zero_byte(c[3] ^ val);
        if(m[0] | m[1] | m[2] | m[3]) 
            break;
    }

    for(; curr - start >= 8; curr -= 8) {
        uint64_t c; memcpy(&c, curr - 8, 8);
        if(mem_has_zero_byte(val ^ c))
            break; 
    }
    
    for(; start != curr; curr ++) {
        if(*curr != value)
            return curr - (uint8_t*) ptr;
    }

    return -1;
}


EXTERNAL isize memfind(const void* ptr, uint8_t value, isize size)
{
    const void* found = memchr(ptr, value, (size_t) size);
    if(found == NULL)
        return -1;
    return (const uint8_t*) found - (const uint8_t*) ptr;
}

EXTERNAL isize memfind_not(const void* ptr, uint8_t value, isize size)      
{
    return memfind_pattern_not(ptr, value*0x0101010101010101ull, size); 
}
EXTERNAL isize memfind_last_not(const void* ptr, uint8_t value, isize size)
{
    return memfind_pattern_last_not(ptr, value*0x0101010101010101ull, size); 
}

#define MEM_FIND_SWAR 0
#define MEM_FIND_AVX2 1

#if !defined(MEM_FIND) || MEM_FIND == MEM_FIND_AVX2

#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__) && !defined(_M_ARM64EC) || defined(_M_CEE_PURE) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)

#if defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
    static int32_t _mem_find_last_set_bit64(uint64_t num)
    {
        unsigned long out = 0;
        _BitScanReverse64(&out, (unsigned long long) num);
        return (int32_t) out;
    }

    static inline void _mem_movsb(void* dest, const void* src, unsigned long long count)
    {
        __movsb(dest, src, count);
    }

    #define MEM_ALIGNAS(x) __declspec(align(x))
#elif defined(__GNUC__) || defined(__clang__)
    #include <x86intrin.h>
    static int32_t _mem_find_last_set_bit64(uint64_t num)
    {
        return 64 - __builtin_clzll((unsigned long long) num) - 1;
    }

    static inline void _mem_movsb(void* dest, const void* src, unsigned long long count)
    {
        __asm__ volatile(
            "movq %0, %%rdi;"
            "movq %1, %%rsi;"
            "movq %2, %%rcx;"
            "rep movsb;"
            :
            : "r"(dest), "r"(src), "r"(count)
            : "rdi", "rsi", "rcx", "memory"
        );
    }

    #define MEM_ALIGNAS(x) alignas(x)
#else
    #error unsupported compiler!
#endif

EXTERNAL isize memfind_last_avx2(const void* ptr, uint8_t value, isize size)
{
    __m256i pattern = _mm256_set1_epi8(value);
    uint8_t* curr = (uint8_t*) ptr + size;
    uint8_t* start = (uint8_t*) ptr;
    for(; curr - start >= 32; curr -= 32) {
        __m256i c = _mm256_loadu_si256((const __m256i*) curr);
        __m256i eq = _mm256_cmpeq_epi8(c, pattern);
        uint32_t mask = (uint32_t) _mm256_movemask_epi8(eq);
        if(mask != 0)
        {
            isize offset = _mem_find_last_set_bit64(mask);
            return curr - (uint8_t*) ptr + offset;
        } 
    }

    isize rem = curr - start; 
    if(rem != 0) {
        //there are faster ways to handle this but this one is very simple
        // and I dont care much so i use that.
        MEM_ALIGNAS(32) uint8_t local[32]; 
        _mm256_store_si256((__m256i*) local, pattern);
        _mem_movsb(local, start, rem);
        
        __m256i c = _mm256_load_si256((const __m256i*) local);
        __m256i eq = _mm256_cmpeq_epi8(c, pattern);
        uint32_t mask = (uint32_t) _mm256_movemask_epi8(eq);
        if(mask != 0)
        {
            isize offset = _mem_find_last_set_bit64(mask);
            return curr - (uint8_t*) ptr + offset;
        } 
    }
    return -1;
}
    #define MEM_FIND MEM_FIND_AVX2
#else

#endif
#endif

#ifndef MEM_FIND
    #define MEM_FIND MEM_FIND_SWAR
#endif

EXTERNAL isize memfind_last(const void* ptr, uint8_t value, isize size)
{
    #ifdef MEM_FIND == MEM_FIND_SWAR
        memfind_last_swar(ptr, value, size);
    #else
        memfind_last_avx2(ptr, value, size);
    #endif
}

#endif