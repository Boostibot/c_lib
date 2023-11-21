#ifndef JOT_GUID
#define JOT_GUID

#include "defines.h"
#include "platform.h"
#include "hash.h"
#include "random.h"

//64 bit program-unique-identifier.
typedef enum {__ID_VAL__ = 0}* Id;  

// Ids are guranteed to be 100% unique within a single run of the program. 
// They should also be equally distributed across their values meening they dont need to be hashed.
// 
//We use pointer to a custom type for three reasons:
//  1: pointers are comparable with =, <, >, ... (unlike structs)
//  2: type is distinct and thus type safe (unlike typedef u64 Guid)
//  3: pointers are castable (Guid guid = (Guid) index)

//128 bit gloablly-unique-identifier. Is used mainly for identifying resources
typedef struct {
    u64 lo;
    u64 hi;
} Guid;

EXPORT Id id_generate();
EXPORT Guid guid_generate();
EXPORT u64 guid_hash(Guid guid);

#endif

#define JOT_ALL_IMPL


#if (defined(JOT_ALL_IMPL) || defined(JOT_GUID_IMPL)) && !defined(JOT_GUID_HAS_IMPL)
#define JOT_GUID_HAS_IMPL

EXPORT Id id_generate()
{
    //We generate the random values by doing atomic add on a counter and hashing the result.
    //We add salt to the hash to make the sequence random between program runs.
    //Note that the hash64 function is bijective and maps 0 -> 0

    static i64 salt = 0;
    static i64 counter = 0;
    if(salt == 0)
        salt = platform_perf_counter_startup(); 
    
    u64 ordered_id = platform_interlocked_increment64(&counter) + salt;
    u64 hashed_id = hash64(ordered_id);

    //In case we wrap around which shouldnt even happen...
    if(hashed_id == 0)
        return id_generate();
    
    ASSERT(hashed_id != 0);
    return (Id) hashed_id;
}

EXPORT Guid guid_generate()
{
    static THREAD_LOCAL Guid _rng_state = {0};
    Guid* rng_state = &_rng_state;
    if(rng_state->hi == 0)
    {
        rng_state->lo = platform_perf_counter() + (i64) rng_state;
        rng_state->hi = platform_perf_counter() + (i64) rng_state;
    }

    Guid out = {0};
    out.lo = random_splitmix(&rng_state->lo);
    out.hi = random_splitmix(&rng_state->hi);

    return out;
}

EXPORT u64 guid_hash64(Guid guid)
{
    return guid.lo*3 + guid.hi;
}

EXPORT u64 guid_hash32(Guid guid)
{
    return hash_fold64(guid_hash64(guid));
}


#endif