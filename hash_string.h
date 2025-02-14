#ifndef MODULE_HASH_STRING
#define MODULE_HASH_STRING
#include "string.h"

typedef struct Hash_String {
    //Same trick as with String_Builder to make working with
    // hashed strings as easy as with regular ones.
    union {
        struct {
            const char* data;
            isize count;
        };
        String string;
    };
    uint64_t hash;
} Hash_String;

EXTERNAL Hash_String hash_string_from_cstring(const char* cstr);
EXTERNAL Hash_String hash_string_make(String string);
EXTERNAL uint64_t hash_string(String string);
EXTERNAL uint64_t hash_string_ptrs(const String* string);
EXTERNAL bool hash_string_is_equal(Hash_String a, Hash_String b);         //Compares two hash strings using hash, size and the contents of data.
EXTERNAL bool hash_string_is_equal_approx(Hash_String a, Hash_String b);  //Compares two hash strings using only hash and size. Can be forced to compare data by defining DISSABLE_APPROXIMATE_EQUAL

EXTERNAL Hash_String hash_string_allocate(Allocator* alloc, Hash_String hstring);
EXTERNAL void hash_string_deallocate(Allocator* alloc, Hash_String* hstring);

//Makes a hashed string out of string literal, with optimizations evaluating the hash at compile time (except on GCC). Fails for anything but string literals
#define HSTRING(string_literal) BINIT(Hash_String){string_literal "", sizeof(string_literal "") - 1, hash64_fnv_inline(string_literal "", sizeof(string_literal "") - 1)}
#define HSTRING_FMT "[%08llx]:'%.*s'" 
#define HSTRING_PRINT(hstring) (hstring).hash, (int) (hstring).count, (hstring).data

#if defined(_MSC_VER)
    #define ATTRIBUTE_INLINE_ALWAYS __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_INLINE_ALWAYS __attribute__((always_inline)) inline
#else
    #define ATTRIBUTE_INLINE_ALWAYS inline
#endif

//@NOTE: We use fnv because of its extreme simplicity making it very likely to be inlined
//       and thus for static strings be evaluated at compile time. 
//       Both MSVC and CLANG evaluate it at compile time, GCC does not.
ATTRIBUTE_INLINE_ALWAYS static uint64_t hash64_fnv_inline(const char* data, isize size)
{
    uint64_t hash = 0;
    for(int64_t i = 0; i < size; i++)
        hash = (hash * 0x100000001b3ULL) ^ (uint64_t) data[i];

    return hash;
}
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_HASH_STRING)) && !defined(MODULE_HAS_IMPL_HASH_STRING)
#define MODULE_HAS_IMPL_HASH_STRING

EXTERNAL Hash_String hash_string_make(String string)
{
    Hash_String out = {0};
    out.string = string;
    out.hash = hash_string(string);
    return out;
}

EXTERNAL Hash_String hash_string_from_cstring(const char* cstr)
{
    return hash_string_make(string_of(cstr));
}

EXTERNAL uint64_t hash_string(String string)
{
    return hash64_fnv_inline(string.data, string.count);
}

EXTERNAL uint64_t hash_string_ptrs(const String* string)
{
    return hash64_fnv_inline(string->data, string->count);
}

EXTERNAL bool hash_string_is_equal(Hash_String a, Hash_String b)
{
    if(a.hash != b.hash || a.count != b.count)
        return false;

    return memcmp(a.data, b.data, a.count) == 0;
}

EXTERNAL bool hash_string_is_equal_approx(Hash_String a, Hash_String b)
{
    #ifndef DISSABLE_APPROXIMATE_EQUAL
        return hash_string_is_equal(a, b);
    #else
        return a.hash == b.hash && a.count == b.count;
    #endif
}

EXTERNAL int hash_string_compare(Hash_String a, Hash_String b)
{
    if(a.hash < b.hash) return -1;
    if(a.hash > b.hash) return 1;
    else                return string_compare(a.string, b.string);
}

EXTERNAL Hash_String hash_string_allocate(Allocator* alloc, Hash_String hstring)
{
    Hash_String out = {0};
    out.string = string_allocate(alloc, hstring.string);
    out.hash = hstring.hash;
    return out;
}

EXTERNAL void hash_string_deallocate(Allocator* alloc, Hash_String* hstring)
{
    string_deallocate(alloc, &hstring->string);
    hstring->hash = 0;
}

#endif