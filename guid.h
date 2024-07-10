#ifndef JOT_GUID
#define JOT_GUID

#include "defines.h"
#include "platform.h"
#include "hash.h"
#include "random.h"

//64 bit program-unique-identifier.
typedef struct _Opaque_ID_Dummy* Id;  

// Ids are guaranteed to be 100% unique within a single run of the program. 
// They should also be equally distributed across their values meaning they dont need to be hashed.
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

EXTERNAL Id id_generate();
EXTERNAL Guid guid_generate();
EXTERNAL u64 guid_hash64(Guid guid);
EXTERNAL u64 guid_hash32(Guid guid);

#endif


#if (defined(JOT_ALL_IMPL) || defined(JOT_GUID_IMPL)) && !defined(JOT_GUID_HAS_IMPL)
#define JOT_GUID_HAS_IMPL

EXTERNAL Id id_generate()
{
    //We generate the random values by doing atomic add on a counter and hashing the result.
    //We add salt to the hash to make the sequence random between program runs.
    //Note that the hash64 function is bijective and maps 0 -> 0
    static i64 salt = 0;
    static i64 counter = 0;
    
    //This works even in multithreaded because the worst that could happen is we twice assign 
    // the salt to the same value. Even in partially filled states this branch will not run.
    if(salt == 0)
        salt = platform_perf_counter_startup(); 
    
    //... and the rest is atomic ...
    u64 ordered_id = platform_atomic_add64(&counter, 1) + salt;
    u64 hashed_id = hash64(ordered_id);

    //In case we wrap around (which will almost certainly never even happen)...
    if(hashed_id == 0)
        return id_generate();
    
    ASSERT(hashed_id != 0);
    return (Id) hashed_id;
}

EXTERNAL Guid guid_generate()
{
    static ATTRIBUTE_THREAD_LOCAL Guid _rng_state = {0};
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

EXTERNAL u64 guid_hash64(Guid guid)
{
    return guid.lo*3 + guid.hi;
}

EXTERNAL u64 guid_hash32(Guid guid)
{
    return hash_fold64(guid_hash64(guid));
}


#endif