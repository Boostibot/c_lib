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
