#pragma once

#include "string.h"

#define HASH_STRING_CHECK_HASHES

typedef struct Hash_String {
    String string;
    u64 hash;
} Hash_String;

EXPORT Hash_String hash_string_from_cstring(const char* cstr);
EXPORT Hash_String hash_string_make(String string);

MODIFIER_FORCE_INLINE static uint64_t _hash64_fnv_inline(const char* data, isize size);

#define HSTRING(string_literal) BRACE_INIT(Hash_String){string_literal, sizeof(string_literal) - 1, hash64_fnv_inline(string_literal, sizeof(string_literal) - 1)}

EXPORT Hash_String hash_string_from_cstring(const char* cstr)
{
    return hash_string_make(string_make(cstr));
}

MODIFIER_FORCE_INLINE static uint64_t _hash64_fnv_inline(const char* data, isize size)
{
    uint64_t hash = 0;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ (uint64_t) data[i];

    return hash;
}

EXPORT Hash_String hash_string_make(String string)
{
    Hash_String out = {0};
    out.string = string;
    out.hash = _hash64_fnv_inline(string.data, string.size);
    return out;
}

