#ifndef JOT_HASH_STRING
#define JOT_HASH_STRING
#include "string.h"

typedef struct Hash_String {
    //Same trick as with String_Builder to make working with
    // hashed strings as easy as with regular ones.
    union {
        struct {
            const char* data;
            isize size;
        };
        String string;
    };
    u64 hash;
} Hash_String;

EXPORT Hash_String hash_string_from_cstring(const char* cstr);
EXPORT Hash_String hash_string_make(String string);
EXPORT u64 hash_string(String string);
EXPORT bool hash_string_is_equal(Hash_String a, Hash_String b);         //Compares two hash strings using hash, size and the contents of data.
EXPORT bool hash_string_is_equal_approx(Hash_String a, Hash_String b);  //Compares two hash strings using only hash and size. Can be forced to compare data by defining DISSABLE_APPROXIMATE_EQUAL

//Makes a hashed string out of string literal, with optimizations evaluating the hash at compile time (except on GCC). Fails for anything but string literals
#define HSTRING(string_literal) BRACE_INIT(Hash_String){string_literal "", sizeof(string_literal) - 1, hash64_fnv_inline(string_literal, sizeof(string_literal) - 1)}

//@NOTE: We use fnv because of its extreme simplicity making it very likely to be inlined
//       and thus for static strings be evaluated at compile time. Truly, both MSVC and 
//       CLANG evaluate it at compile time. GCC does not.
ATTRIBUTE_INLINE_ALWAYS static uint64_t _hash64_fnv_inline(const char* data, isize size)
{
    uint64_t hash = 0;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ (uint64_t) data[i];

    return hash;
}
#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_HASH_STRING_IMPL)) && !defined(JOT_HASH_STRING_HAS_IMPL)
#define JOT_HASH_STRING_HAS_IMPL

EXPORT Hash_String hash_string_make(String string)
{
    Hash_String out = {0};
    out.string = string;
    out.hash = hash_string(string);
    return out;
}

EXPORT Hash_String hash_string_from_cstring(const char* cstr)
{
    return hash_string_make(string_make(cstr));
}

EXPORT u64 hash_string(String string)
{
    return _hash64_fnv_inline(string.data, string.size);
}

EXPORT bool hash_string_is_equal(Hash_String a, Hash_String b)
{
    if(a.hash != b.hash || a.size != b.size)
        return false;

    return memcmp(a.data, b.data, a.size) == 0;
}

EXPORT bool hash_string_is_equal_approx(Hash_String a, Hash_String b)
{
    #ifndef DISSABLE_APPROXIMATE_EQUAL
        return hash_string_is_equal(a, b);
    #else
        return a.hash == b.hash && a.size == b.size;
    #endif
}
#endif