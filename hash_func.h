#ifndef MODULE_HASH_FN
#define MODULE_HASH_FN

#include <stdint.h>
#include <string.h>

#ifndef HASH_FN_API
    #define HASH_FN_API static inline
    #define MODULE_IMPL_HASH_FN
#endif

//Hashes a 64 bit value to 64 bit hash.
//Note that this function is bijective meaning it can be reversed.
//In particular 0 maps to 0.
//source: https://stackoverflow.com/a/12996028
HASH_FN_API uint64_t hash64_bijective(uint64_t x);
HASH_FN_API uint64_t unhash64_bijective(uint64_t x);

//Hashes a 32 bit value to 32 bit hash.
//Note that this function is bijective meaning it can be reversed.
//In particular 0 maps to 0.
//source: https://stackoverflow.com/a/12996028
HASH_FN_API uint32_t hash32_bijective(uint32_t x); 
HASH_FN_API uint32_t unhash32_bijective(uint32_t x);

//Mixes two prevously hashed values into one. 
//Yileds good results even when hash1 and hash2 are hashes badly.
HASH_FN_API uint64_t hash64_mix(uint64_t hash1, uint64_t hash2);
HASH_FN_API uint32_t hash32_mix(uint32_t hash1, uint32_t hash2);

//Mixes hi and lo bits of hash to produce a 32 bit hash
HASH_FN_API uint32_t hash64_fold(uint64_t hash);
HASH_FN_API uint32_t hash64_fold_mix(uint64_t hash);

//Source: https://github.com/abrandoned/murmur2/blob/master/MurmurHash2.c (modified to not use unaligned pointers since those are UB)
HASH_FN_API uint32_t hash32_murmur(const void* key, int64_t size, uint32_t seed);
HASH_FN_API uint64_t hash64_murmur(const void* key, int64_t size, uint64_t seed);

//Source: https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
HASH_FN_API uint32_t hash32_fnv(const void* key, int64_t size, uint32_t seed);
HASH_FN_API uint64_t hash64_fnv(const void* key, int64_t size, uint64_t seed);

HASH_FN_API uint64_t xxhash64(const void* key, int64_t size, uint64_t seed);

#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_HASH_FN)) && !defined(MODULE_HAS_IMPL_HASH_FN)
#define MODULE_HAS_IMPL_HASH_FN

#ifndef REQUIRE
    #include <assert.h>
    #define REQUIRE(x) assert(x)
#endif

HASH_FN_API uint64_t hash64_bijective(uint64_t x) 
{
    x = (x ^ (x >> 30)) * (uint64_t) 0xbf58476d1ce4e5b9;
    x = (x ^ (x >> 27)) * (uint64_t) 0x94d049bb133111eb;
    x = x ^ (x >> 31);
    return x;
}

HASH_FN_API uint64_t unhash64_bijective(uint64_t x) 
{
    x = (x ^ (x >> 31) ^ (x >> 62)) * (uint64_t) 0x319642b2d24d8ec3;
    x = (x ^ (x >> 27) ^ (x >> 54)) * (uint64_t) 0x96de1b173f119089;
    x = x ^ (x >> 30) ^ (x >> 60);
    return x;
}

HASH_FN_API uint32_t hash32_bijective(uint32_t x) 
{
    x = ((x >> 16) ^ x) * 0x119de1f3;
    x = ((x >> 16) ^ x) * 0x119de1f3;
    x = (x >> 16) ^ x;
    return x;
}

HASH_FN_API uint32_t unhash32_bijective(uint32_t x) 
{
    x = ((x >> 16) ^ x) * 0x119de1f3;
    x = ((x >> 16) ^ x) * 0x119de1f3;
    x = (x >> 16) ^ x;
    return x;
}

HASH_FN_API uint64_t hash64_mix(uint64_t hash1, uint64_t hash2)
{
    hash1 ^= hash2 + 0x517cc1b727220a95 + (hash1 << 6) + (hash1 >> 2);
    return hash1;
}

HASH_FN_API uint32_t hash32_mix(uint32_t hash1, uint32_t hash2)
{
    hash1 ^= hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2);
    return hash1;
}

HASH_FN_API uint32_t hash64_fold_mix(uint64_t hash)
{
    return hash32_mix((uint32_t) hash, (uint32_t)(hash >> 32));
}

HASH_FN_API uint32_t hash64_fold(uint64_t hash)
{
    return (uint32_t) hash ^ (uint32_t)(hash >> 32);
}

HASH_FN_API uint32_t hash32_murmur(const void* key, int64_t size, uint32_t seed)
{
    uint32_t endian_check = 0x33221100;
    REQUIRE(*(uint8_t*) (void*) &endian_check == 0 && "Big endian machine detected! Please change this algorithm to suite your machine!");
    REQUIRE((key != NULL || size == 0) && size >= 0);

    const uint32_t magic = 0x5bd1e995;
    const int r = 24;

    uint32_t hash = seed;

    const uint8_t* data = (const uint8_t*)key;
    const uint8_t* end = data + size;
    for(; data < end - 3; data += 4)
    {
        uint32_t read = 0; 
        memcpy(&read, data, sizeof read);

        read *= magic;
        read ^= read >> r;
        read *= magic;

        hash *= magic;
        hash ^= read;
    }

    switch(size & 3)
    {
        case 3: hash ^= data[2] << 16;
        case 2: hash ^= data[1] << 8; 
        case 1: hash ^= data[0];       
        hash *= magic;
    };

    hash ^= hash >> 13;
    hash *= magic;
    hash ^= hash >> 15;
    return hash;
} 

HASH_FN_API uint64_t hash64_murmur(const void* key, int64_t size, uint64_t seed)
{
    uint32_t endian_check = 0x33221100;
    REQUIRE(*(uint8_t*) (void*) &endian_check == 0 && "Big endian machine detected! Please change this algorithm to suite your machine!");
    REQUIRE((key != NULL || size == 0) && size >= 0);

    const uint64_t magic = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t hash = seed ^ ((uint64_t) size * magic);

    const uint8_t* data = (const uint8_t *)key;
    const uint8_t* end = data + size;
    for(; data < end - 7; data += 8)
    {
        uint64_t read = 0; 
        memcpy(&read, data, sizeof read);

        read *= magic; 
        read ^= read >> r; 
        read *= magic; 
    
        hash ^= read;
        hash *= magic; 
    }
    
    switch(size & 7)
    {
        case 7: hash ^= ((uint64_t) data[6]) << 48;
        case 6: hash ^= ((uint64_t) data[5]) << 40;
        case 5: hash ^= ((uint64_t) data[4]) << 32;
        case 4: hash ^= ((uint64_t) data[3]) << 24;
        case 3: hash ^= ((uint64_t) data[2]) << 16;
        case 2: hash ^= ((uint64_t) data[1]) << 8; 
        case 1: hash ^= ((uint64_t) data[0]);       
        hash *= magic;
    };
 
    hash ^= hash >> r;
    hash *= magic;
    hash ^= hash >> r;
    return hash;
} 

#define XXHASH_FN64_PRIME_1  0x9E3779B185EBCA87ULL
#define XXHASH_FN64_PRIME_2  0xC2B2AE3D27D4EB4FULL
#define XXHASH_FN64_PRIME_3  0x165667B19E3779F9ULL
#define XXHASH_FN64_PRIME_4  0x85EBCA77C2B2AE63ULL
#define XXHASH_FN64_PRIME_5  0x27D4EB2F165667C5ULL

HASH_FN_API inline uint64_t _xxhash64_rotate_left(uint64_t x, uint8_t bits)
{
    return (x << bits) | (x >> (64 - bits));
}

HASH_FN_API inline uint64_t _xxhash64_process_single(uint64_t previous, uint64_t input)
{
    return _xxhash64_rotate_left(previous + input * XXHASH_FN64_PRIME_2, 31) * XXHASH_FN64_PRIME_1;
}

HASH_FN_API uint64_t xxhash64(const void* key, int64_t size, uint64_t seed)
{
    uint32_t endian_check = 0x33221100;
    REQUIRE(*(uint8_t*) (void*) &endian_check == 0 && "Big endian machine detected! Please change this algorithm to suite your machine!");
    REQUIRE((key != NULL || size == 0) && size >= 0);

    uint8_t* data = (uint8_t*) (void*) key;
    uint8_t* end = data + size;
    
    //Bulk computation
    uint64_t hash = seed + XXHASH_FN64_PRIME_5;
    if (size >= 32)
    {
        uint64_t state[4] = {0};
        uint64_t block[4] = {0};
        state[0] = seed + XXHASH_FN64_PRIME_1 + XXHASH_FN64_PRIME_2;
        state[1] = seed + XXHASH_FN64_PRIME_2;
        state[2] = seed;
        state[3] = seed - XXHASH_FN64_PRIME_1;
        
        for(; data < end - 31; data += 32)
        {
            memcpy(block, data, 32);
            state[0] = _xxhash64_process_single(state[0], block[0]);
            state[1] = _xxhash64_process_single(state[1], block[1]);
            state[2] = _xxhash64_process_single(state[2], block[2]);
            state[3] = _xxhash64_process_single(state[3], block[3]);
        }

        hash = _xxhash64_rotate_left(state[0], 1)
            + _xxhash64_rotate_left(state[1], 7)
            + _xxhash64_rotate_left(state[2], 12)
            + _xxhash64_rotate_left(state[3], 18);
        hash = (hash ^ _xxhash64_process_single(0, state[0])) * XXHASH_FN64_PRIME_1 + XXHASH_FN64_PRIME_4;
        hash = (hash ^ _xxhash64_process_single(0, state[1])) * XXHASH_FN64_PRIME_1 + XXHASH_FN64_PRIME_4;
        hash = (hash ^ _xxhash64_process_single(0, state[2])) * XXHASH_FN64_PRIME_1 + XXHASH_FN64_PRIME_4;
        hash = (hash ^ _xxhash64_process_single(0, state[3])) * XXHASH_FN64_PRIME_1 + XXHASH_FN64_PRIME_4;
    }
    hash += (uint64_t) size;

    //Consume last <32 Bytes
    for (; data + 8 <= end; data += 8)
    {
        uint64_t read = 0; memcpy(&read, data, sizeof read);
        hash = _xxhash64_rotate_left(hash ^ _xxhash64_process_single(0, read), 27) * XXHASH_FN64_PRIME_1 + XXHASH_FN64_PRIME_4;
    }

    if (data + 4 <= end)
    {
        uint32_t read = 0; memcpy(&read, data, sizeof read);
        hash = _xxhash64_rotate_left(hash ^ read * XXHASH_FN64_PRIME_1, 23) * XXHASH_FN64_PRIME_2 + XXHASH_FN64_PRIME_3;
        data += 4;
    }

    while (data < end)
        hash = _xxhash64_rotate_left(hash ^ (*data++) * XXHASH_FN64_PRIME_5, 11) * XXHASH_FN64_PRIME_1;
        
    // Avalanche
    hash ^= hash >> 33;
    hash *= XXHASH_FN64_PRIME_2;
    hash ^= hash >> 29;
    hash *= XXHASH_FN64_PRIME_3;
    hash ^= hash >> 32;
    return hash;
}

HASH_FN_API uint32_t hash32_fnv(const void* key, int64_t size, uint32_t seed)
{
    REQUIRE((key != NULL || size == 0) && size >= 0);

    uint32_t hash = seed ^ 2166136261UL;
    const uint8_t* data = (const uint8_t*) key;
    for(int64_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 16777619;
    }
    return hash;
}

HASH_FN_API uint64_t hash64_fnv(const void* key, int64_t size, uint64_t seed)
{
    REQUIRE((key != NULL || size == 0) && size >= 0);

    const uint8_t* data = (const uint8_t*) key;
    uint64_t hash = seed ^ 0x27D4EB2F165667C5ULL;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ data[i];

    return hash;
}

#endif