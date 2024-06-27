#ifndef JOT_HASH
#define JOT_HASH

#include <stdint.h>

#ifndef JOT_HASH_API
    #define JOT_HASH_API static inline
#endif

//Hashes a 64 bit value to 64 bit hash.
//Note that this function is bijective meaning it can be reversed.
//In particular 0 maps to 0.
JOT_HASH_API uint64_t hash64(uint64_t value) 
{
    //source: https://stackoverflow.com/a/12996028
    uint64_t hash = value;
    hash = (hash ^ (hash >> 30)) * (uint64_t) 0xbf58476d1ce4e5b9;
    hash = (hash ^ (hash >> 27)) * (uint64_t) 0x94d049bb133111eb;
    hash = hash ^ (hash >> 31);
    return hash;
}

//Hashes a 32 bit value to 32 bit hash.
//Note that this function is bijective meaning it can be reversed.
//In particular 0 maps to 0.
JOT_HASH_API uint32_t hash32(uint32_t value) 
{
    //source: https://stackoverflow.com/a/12996028
    uint32_t hash = value;
    hash = ((hash >> 16) ^ hash) * 0x119de1f3;
    hash = ((hash >> 16) ^ hash) * 0x119de1f3;
    hash = (hash >> 16) ^ hash;
    return hash;
}

//Mixes two prevously hashed values into one. 
//Yileds good results even when hash1 and hash2 are hashes badly.
JOT_HASH_API uint64_t hash_mix64(uint64_t hash1, uint64_t hash2)
{
    hash1 ^= hash2 + 0x517cc1b727220a95 + (hash1 << 6) + (hash1 >> 2);
    return hash1;
}

JOT_HASH_API uint32_t hash_mix32(uint32_t hash1, uint32_t hash2)
{
    hash1 ^= hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2);
    return hash1;
}

JOT_HASH_API uint32_t hash_fold64(uint64_t hash)
{
    return hash_mix32((uint32_t) hash, (uint32_t)(hash >> 32));
}

JOT_HASH_API uint32_t hash64_to32(uint64_t value) 
{
    return hash_fold64(hash64(value));
}

JOT_HASH_API uint32_t hash32_murmur(const void* key, int64_t size, uint32_t seed)
{
    //source https://github.com/abrandoned/murmur2/blob/master/MurmurHash2.c
    // 'm' and 'r' are mixing constants generated offline.
    // They're not really 'magic', they just happen to work well. 
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    // Initialize the hash to a 'random' value
    uint32_t h = seed ^ (uint32_t) size;

    // Mix 4 bytes at a time into the hash
    const uint8_t* data = (const uint8_t*)key;

    while(size >= 4)
    {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        size -= 4;
    }

    // Handle the last few bytes of the input array 
    switch(size)
    {
        case 3: h ^= data[2] << 16;
        case 2: h ^= data[1] << 8; 
        case 1: h ^= data[0];       
        h *= m;
    };

    // Do a few final mixes of the hash to ensure the last few
    // bytes are well-incorporated. 

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
} 

JOT_HASH_API uint64_t hash64_murmur(const void* key, int64_t size, uint64_t seed)
{
    //source https://github.com/abrandoned/murmur2/blob/master/MurmurHash2.c
    //& big endian support: https://github.com/niklas-ourmachinery/bitsquid-foundation/blob/master/murmur_hash.cpp
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ ((uint64_t) size * m);

    const uint64_t * data = (const uint64_t *)key;
    const uint64_t * end = data + ((uint64_t) size/8);

    bool is_big_endian = false;
    #ifdef PLATFORM_HAS_ENDIAN_BIG
    is_big_endian = true;
    #endif

    while(data != end)
    {
		uint64_t k = *data++;
        if(is_big_endian)
        {
			char *p = (char *)&k;
			char c;
			c = p[0]; p[0] = p[7]; p[7] = c;
			c = p[1]; p[1] = p[6]; p[6] = c;
			c = p[2]; p[2] = p[5]; p[5] = c;
			c = p[3]; p[3] = p[4]; p[4] = c;
        }

        k *= m; 
        k ^= k >> r; 
        k *= m; 
    
        h ^= k;
        h *= m; 
    }
    
    const uint8_t* data2 = (const uint8_t*)key;
    switch(size & 7)
    {
        case 7: h ^= ((uint64_t) data2[6]) << 48;
        case 6: h ^= ((uint64_t) data2[5]) << 40;
        case 5: h ^= ((uint64_t) data2[4]) << 32;
        case 4: h ^= ((uint64_t) data2[3]) << 24;
        case 3: h ^= ((uint64_t) data2[2]) << 16;
        case 2: h ^= ((uint64_t) data2[1]) << 8; 
        case 1: h ^= ((uint64_t) data2[0]);       
        h *= m;
    };
 
    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
} 

#define XXHASH64_PRIME_1  0x9E3779B185EBCA87ULL
#define XXHASH64_PRIME_2  0xC2B2AE3D27D4EB4FULL
#define XXHASH64_PRIME_3  0x165667B19E3779F9ULL
#define XXHASH64_PRIME_4  0x85EBCA77C2B2AE63ULL
#define XXHASH64_PRIME_5  0x27D4EB2F165667C5ULL

static inline uint64_t _xxhash64_rotate_left(uint64_t x, uint8_t bits)
{
    return (x << bits) | (x >> (64 - bits));
}

static inline uint64_t _xxhash64_process_single(uint64_t previous, uint64_t input)
{
    return _xxhash64_rotate_left(previous + input * XXHASH64_PRIME_2, 31) * XXHASH64_PRIME_1;
}

static inline uint64_t xxhash64(const void* input, uint64_t length, uint64_t seed)
{
    uint32_t endian_check = 0x33221100;
    ASSERT(*(uint8_t*) (void*) &endian_check == 0 && "Big endian machine detected! Please change this algorithm to suite your machine!");
    ASSERT((input == NULL) == (length == 0) && length >= 0);

    uint8_t* data = (uint8_t*) (void*) input;
    uint8_t* end = data + length;
    
    //Bulk computation
    uint64_t hash = seed + XXHASH64_PRIME_5;
    if (length >= 32)
    {
        uint64_t state[4] = {0};
        uint64_t block[4] = {0};
        state[0] = seed + XXHASH64_PRIME_1 + XXHASH64_PRIME_2;
        state[1] = seed + XXHASH64_PRIME_2;
        state[2] = seed;
        state[3] = seed - XXHASH64_PRIME_1;
        
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
        hash = (hash ^ _xxhash64_process_single(0, state[0])) * XXHASH64_PRIME_1 + XXHASH64_PRIME_4;
        hash = (hash ^ _xxhash64_process_single(0, state[1])) * XXHASH64_PRIME_1 + XXHASH64_PRIME_4;
        hash = (hash ^ _xxhash64_process_single(0, state[2])) * XXHASH64_PRIME_1 + XXHASH64_PRIME_4;
        hash = (hash ^ _xxhash64_process_single(0, state[3])) * XXHASH64_PRIME_1 + XXHASH64_PRIME_4;
    }
    hash += length;

    //Consume last <32 Bytes
    for (; data + 8 <= end; data += 8)
    {
        uint64_t read = 0; memcpy(&read, data, sizeof read);
        hash = _xxhash64_rotate_left(hash ^ _xxhash64_process_single(0, read), 27) * XXHASH64_PRIME_1 + XXHASH64_PRIME_4;
    }

    if (data + 4 <= end)
    {
        uint32_t read = 0; memcpy(&read, data, sizeof read);
        hash = _xxhash64_rotate_left(hash ^ read * XXHASH64_PRIME_1, 23) * XXHASH64_PRIME_2 + XXHASH64_PRIME_3;
        data += 4;
    }

    while (data < end)
        hash = _xxhash64_rotate_left(hash ^ (*data++) * XXHASH64_PRIME_5, 11) * XXHASH64_PRIME_1;
        
    // Avalanche
    hash ^= hash >> 33;
    hash *= XXHASH64_PRIME_2;
    hash ^= hash >> 29;
    hash *= XXHASH64_PRIME_3;
    hash ^= hash >> 32;
    return hash;
}

JOT_HASH_API uint32_t hash32_fnv_one_at_a_time(const void* key, int64_t size, uint32_t seed)
{
    // Source: https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
    uint32_t hash = seed;
    hash ^= 2166136261UL;
    const uint8_t* data = (const uint8_t*)key;
    for(int64_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 16777619;
    }
    return hash;
}



#endif
