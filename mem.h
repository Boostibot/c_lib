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

//find first/last byte or not-byte. Return its index if found, -1 if not. 
//These functions are about 8x faster than the naive (which makes sense considering they are doing 8B at the time).
EXTERNAL isize memfind(const void* ptr, uint8_t value, isize size);
EXTERNAL isize memfind_last(const void* ptr, uint8_t value, isize size);
EXTERNAL isize memfind_not(const void* ptr, uint8_t value, isize size);
EXTERNAL isize memfind_last_not(const void* ptr, uint8_t value, isize size);

//finds the first byte not matching the given 8B pattern (assumed to be in little endian).
//Is intended to be used in conjuction with mem_broadcast8, mem_broadcast16, mem_broadcast32 to create the mask
EXTERNAL isize memfind_pattern_not(const void* ptr, uint64_t value, isize size);
EXTERNAL isize memfind_pattern_last_not(const void* ptr, uint64_t value, isize size); //Same thing as memfind_pattern_not except in reverse

#endif

#define MODULE_IMPL_ALL

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
    char temp[64] = {0};
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
    for(; curr - start >= 1; curr -= 1, val_copy <<= 8) {
        uint8_t hi_byte = (uint8_t) (val_copy >> (64 - 8));
        if(*(curr - 1) != (uint8_t) hi_byte)
            return curr - start - 1;
    }

    return -1;
}

#if defined(_MSC_VER)
    #include <intrin.h>
    inline static int32_t mem_swar_find_last_set(uint64_t num)
    {
        unsigned long out = 0;
        _BitScanReverse64(&out, (unsigned long long) num);
        return (int32_t) out;
    }
#elif defined(__GNUC__) || defined(__clang__)
    inline static int32_t mem_swar_find_last_set(uint64_t num)
    {
        return 64 - __builtin_clzll((unsigned long long) num) - 1;
    }
#else
    #error unsupported compiler!
#endif

//SWAR programming utils (taken from bit twiddling hacks)
// https://graphics.stanford.edu/~seander/bithacks.html
static inline uint64_t mem_swar_has_zero_byte(uint64_t val)  
{
    return (val - 0x0101010101010101ull) & ~val & 0x8080808080808080ull; 
}

//https://stackoverflow.com/a/68701617
static inline uint64_t mem_swar_compare_eq_sign(uint64_t x, uint64_t y) 
{
    uint64_t xored = x ^ y;
    uint64_t mask = ((((xored >> 1) | 0x8080808080808080ull) - xored) & 0x8080808080808080ull);
    return mask;
}

EXTERNAL isize memfind_last(const void* ptr, uint8_t value, isize size)
{
    //This could be a lot faster using simd but I didnt 
    REQUIRE(size >= 0 && (ptr != NULL || size == 0));
    uint8_t* curr = (uint8_t*) ptr + size;
    uint8_t* start = (uint8_t*) ptr;

    uint64_t p = value*0x0101010101010101ull;
    for(; curr - start >= 8; curr -= 8) {
        uint64_t c; memcpy(&c, curr - 8, 8);
        uint64_t diff = p ^ c;
        if(mem_swar_has_zero_byte(diff))
        {
            uint64_t matching = mem_swar_compare_eq_sign(c, p);
            uint64_t index = 8 - mem_swar_find_last_set(matching)/8;
            return curr - (uint8_t*) ptr - index;
        }
    }
    
    for(; curr - start >= 1; curr -= 1) {
        if(*(curr - 1) == value)
            return curr - start - 1;
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
#endif