#pragma once

#include "string.h"
#include "hash.h"

#define HASH_STRING_CHECK_HASHES

typedef struct Hash_String {
    const char* data;
    isize size;
    u64 hash;
} Hash_String;

EXPORT Hash_String hash_string_make(const char* cstr);
EXPORT Hash_String hash_string_from_string(String string);
EXPORT Hash_String hash_string_from_builder(String_Builder builder);
EXPORT String string_from_hashed(Hash_String hash);

#define HSTRING(char_literal) hash_string_make(char_literal)

EXPORT Hash_String hash_string_make(const char* cstr)
{
    return hash_string_from_string(string_make(cstr));
}

EXPORT Hash_String hash_string_from_string(String string)
{
    Hash_String out = {0};
    out.data = string.data;
    out.size = string.size;
    out.hash = hash64_murmur(out.data, out.size, 0);
    return out;
}

EXPORT Hash_String hash_string_from_builder(String_Builder builder)
{
    return hash_string_from_string(string_from_builder(builder));
}

EXPORT String string_from_hashed(Hash_String hash)
{
    String out = {hash.data, hash.size};
    return out;
}

