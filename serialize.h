#ifndef MODULE_SERIALIZE
#define MODULE_SERIALIZE

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

//TLDR This is a "simple" serialization system roughly equivalent to json in binary.
//based on https://rxi.github.io/a_simple_serialization_system.html.
//
// The requirements for me are the following:
// 1. Self describing: 
//    It should be instantly translatable into some other format. 
//    The format shouldnt even be separated into info and payload sections.
// 2. Backwards and forwards compatible: 
//    It needs to seamlessly handle additions of new fields or changes to wider integer types etc.  
// 3. Require no escaping and provide escape hatches.
//    This property allows us to embed arbitrary other file formats inside, using it as a sort
//    of glue format, between big chunks of actual data.
//
// From 1. and 2. we get something like json. From 3 we get a binary format since 
// textual formats require escaping.
// 
// The general structure of everything is a type byte followed by some payload.
// We support all C basic types for both writing and reading as well as sized strings.
// Strings are null terminated (enforced by the parser - if not then error) which allows 
// for zero copy reading.
// 
// Individual primitive types are grouped into json-like arrays and objects. Lists are 
// denoted by start and end type bytes. Everything between is inside the array. Objects
// are just like arrays except the items are interpreted in pairs of two: the first is
// key, the second is value. If object contains odd number of primitives the last is skipped. Any type
// can be a key, although strings and integers are the most useful.
//
// The parsing code is written to be surprisingly general. In particular it doesnt really care about integer 
// (or floating point) types as long as the stored data within is compatible. For example its perfectly valid
// to ask to parse a value as uint32_t even though the number is stored as floating point - as long as that
// floating point is non-negative integer. This is done so that new parsers trivially support old data 
// of smaller/different types. Similarly old parsers can stay functioning for longer without changes.
// The conversion rules used are:
//   - float/double can be parsed from any integer type and float/double
//   - integer types can be parsed from any integer/float/double if the value fits exactly into that type 
//     (ie does not overflow/underflow, is not fractional when we can represent only whole)
// This conversion process sounds slow - and it is - but we can make it just as fast as simple validation of type
// followed by memcpy by separating out the hot path. Simply when the stored and asked for type are equal
// we just memcpy. When it is not, we would normally fail and print an error message. In the
// fail path we obviously dont care much about perf so doing any kind of funky float to integer conversion is fine.
//
// Lastly I have extended the above code to seamlessly handle data corruption or generally any other
// fault in the format. Binary formats have the unhandy property where even a slight change can cause the 
// entire format to become corrupted and not visualizable. This is generally solved by application specific 
// magic numbers and checksums. I have taken a similar route which accounts very nicely for the generality and
// structure of the format. 
// 
// I provide recovery variants for array/object begin/end which behave just like their regular counterparts 
// except are followed by a user specified magic sequence. The writer is expected to use these a few times
// in the format around large blocks of data (since the magic sequences pose some overhead). The reader on the
// other hand doesnt have to know about these at all. When a parsing error is found within a recovery array/object
// the code attempts to automatically recover by finding the matching end magic sequence for the given array/object.  
//
// A lot of the code is inside the header section because 
//  A) its very short so splitting it would duplicate large portions of this file
//  B) allows the compiler to inline those short functions regardless of compilation unit
//  C) its quite honest and self documenting

#ifdef MODULE_ALL_COUPLED
    #include "string.h"
    #include "allocator.h"
    #include "assert.h"
    typedef String Ser_String;
#else
    typedef struct Ser_String {
        const char* data;
        isize count;
    } Ser_String;
#endif

typedef int64_t isize;
typedef void* (*Allocator)(void* alloc, int mode, int64_t new_size, void* old_ptr, int64_t old_size, int64_t align, void* other);

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

#ifndef EXTERNAL
    #define EXTERNAL 
#endif

#if defined(_MSC_VER)
    #define ATTRIBUTE_INLINE_ALWAYS __forceinline
    #define ATTRIBUTE_INLINE_NEVER  __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_INLINE_ALWAYS __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER  __attribute__((noinline))
#else
    #define ATTRIBUTE_INLINE_ALWAYS inline 
    #define ATTRIBUTE_INLINE_NEVER                              
#endif

typedef enum Ser_Type {
    SER_NULL = 0,               //{u8 type}
    
    SER_ARRAY_BEGIN,             //{u8 type}
    SER_OBJECT_BEGIN,           //{u8 type}
    SER_RECOVERY_OBJECT_BEGIN,  //{u8 type, u8 size}[size bytes of tag]
    SER_RECOVERY_ARRAY_BEGIN,    //{u8 type, u8 size}[size bytes of tag]
    
    SER_ARRAY_END,               //{u8 type}
    SER_OBJECT_END,             //{u8 type}
    SER_RECOVERY_ARRAY_END,      //{u8 type, u8 size}[size bytes of tag]
    SER_RECOVERY_OBJECT_END,    //{u8 type, u8 size}[size bytes of tag]
    SER_ERROR, //"lexing" error. Is near the ENDers section so that we can check for ender with a single compare

    //We have 3 string types since short strings are extremely common
    // and using 8bytes for size *doubles* the space requirement for simple identifiers.
    //Likewise empty strings are very common and including the size and null terminator increases the size 3x.   
    SER_STRING_0,  //{u8 type}
    SER_STRING_8,  //{u8 type, u8 size}[size bytes]\0
    SER_STRING_64, //{u8 type, uint64_t size}[size bytes]\0
    SER_BINARY,    //{u8 type, uint64_t size}[size bytes]

    SER_BOOL, //{u8 type, bool val}

    SER_U8, SER_U16, SER_U32, SER_U64, //{u8 type}[sizeof(T) bytes]
    SER_I8, SER_I16, SER_I32, SER_I64, //{u8 type}[sizeof(T) bytes]
    SER_F8, SER_F16, SER_F32, SER_F64, //{u8 type}[sizeof(T) bytes]

    //aliases
    SER_ARRAY = SER_ARRAY_BEGIN,
    SER_OBJECT = SER_OBJECT_BEGIN,
    SER_RECOVERY_ARRAY = SER_RECOVERY_ARRAY_BEGIN,
    SER_RECOVERY_OBJECT = SER_RECOVERY_OBJECT_BEGIN,
    SER_STRING = SER_STRING_64,
    SER_COMPOUND_TYPES_COUNT = 4,
} Ser_Type;

typedef struct Ser_Writer {
    Allocator* alloc;
    uint8_t* data;
    isize offset;
    isize capacity;
    isize depth;
    bool has_user_buffer;
} Ser_Writer;


EXTERNAL void ser_writer_init(Ser_Writer* w, void* buffer_or_null, isize size, Allocator* alloc_or_null_if_malloc);
EXTERNAL void ser_writer_deinit(Ser_Writer* w);
static inline void ser_writer_write(Ser_Writer* w, const void* ptr, isize size);
static inline void ser_writer_reserve(Ser_Writer* w, isize size);
ATTRIBUTE_INLINE_NEVER 
EXTERNAL void ser_writer_grow(Ser_Writer* w, isize size);

EXTERNAL void ser_binary(Ser_Writer* w, const void* ptr, isize size);
EXTERNAL void ser_string_separate(Ser_Writer* w, const void* ptr, isize size);
static inline void ser_string(Ser_Writer* w, Ser_String string) { ser_string_separate(w, string.data, string.count); }
static inline void ser_cstring(Ser_Writer* w, const char* ptr) { ser_string_separate(w, ptr, ptr ? strlen(ptr) : 0); }

static inline void ser_primitive(Ser_Writer* w, Ser_Type type, const void* ptr, isize size);
static inline void ser_null(Ser_Writer* w)              { ser_primitive(w, SER_I8, NULL, 0); }
static inline void ser_bool(Ser_Writer* w, bool val)    { ser_primitive(w, SER_I8,  &val, sizeof val); }

static inline void ser_i8(Ser_Writer* w, int8_t val)    { ser_primitive(w, SER_I8,  &val, sizeof val); }
static inline void ser_i16(Ser_Writer* w, int16_t val)  { ser_primitive(w, SER_I16, &val, sizeof val); }
static inline void ser_i32(Ser_Writer* w, int32_t val)  { ser_primitive(w, SER_I32, &val, sizeof val); }
static inline void ser_i64(Ser_Writer* w, int64_t val)  { ser_primitive(w, SER_I64, &val, sizeof val); }

static inline void ser_u8(Ser_Writer* w, uint8_t val)   { ser_primitive(w, SER_U8,  &val, sizeof val); }
static inline void ser_u16(Ser_Writer* w, uint16_t val) { ser_primitive(w, SER_U16, &val, sizeof val); }
static inline void ser_u32(Ser_Writer* w, uint32_t val) { ser_primitive(w, SER_U32, &val, sizeof val); }
static inline void ser_u64(Ser_Writer* w, uint64_t val) { ser_primitive(w, SER_U64, &val, sizeof val); }

static inline void ser_f32(Ser_Writer* w, float val)    { ser_primitive(w, SER_F32, &val, sizeof val); }
static inline void ser_f64(Ser_Writer* w, double val)   { ser_primitive(w, SER_F64, &val, sizeof val); }

static inline void ser_array_begin(Ser_Writer* w)       { ser_primitive(w, SER_ARRAY_BEGIN, NULL, 0); }
static inline void ser_array_end(Ser_Writer* w)         { ser_primitive(w, SER_ARRAY_END, NULL, 0); }
static inline void ser_object_begin(Ser_Writer* w)      { ser_primitive(w, SER_OBJECT_BEGIN, NULL, 0); }
static inline void ser_object_end(Ser_Writer* w)        { ser_primitive(w, SER_OBJECT_END, NULL, 0); }

EXTERNAL void ser_custom_recovery(Ser_Writer* w, Ser_Type type, const void* ptr, isize size, const void* ptr2, isize size2);
EXTERNAL void ser_custom_recovery_with_hash(Ser_Writer* w, Ser_Type type, const char* str);

static inline void ser_recovery_array_begin(Ser_Writer* w, const char* str)     { ser_custom_recovery_with_hash(w, SER_RECOVERY_ARRAY_BEGIN, str); }
static inline void ser_recovery_array_end(Ser_Writer* w, const char* str)       { ser_custom_recovery_with_hash(w, SER_RECOVERY_ARRAY_END, str); }
static inline void ser_recovery_object_begin(Ser_Writer* w, const char* str)    { ser_custom_recovery_with_hash(w, SER_RECOVERY_OBJECT_BEGIN, str); }
static inline void ser_recovery_object_end(Ser_Writer* w, const char* str)      { ser_custom_recovery_with_hash(w, SER_RECOVERY_OBJECT_END, str); }

static inline void ser_writer_reserve(Ser_Writer* w, isize size) {
    if(w->offset + size > w->capacity)
        ser_writer_grow(w, w->offset + size);
}
static inline void ser_writer_write(Ser_Writer* w, const void* ptr, isize size) {
    ser_writer_reserve(w, size);
    memcpy(w->data + w->offset, ptr, size);
    w->offset += size;
}

static inline void ser_primitive(Ser_Writer* w, Ser_Type type, const void* ptr, isize size) {
    ser_writer_reserve(w, size+1);
    w->data[w->offset] = (uint8_t) type;
    memcpy(&w->data[w->offset + 1], ptr, size);
    w->offset += size+1;
}

//reading 
typedef struct Ser_Reader {
    const uint8_t* data;
    isize offset;
    isize capacity;
    isize depth;
} Ser_Reader;

typedef struct Ser_Value {
    Ser_Reader* r;
    Ser_Type exact_type;
    Ser_Type type;
    union {
        Ser_String mbinary;
        Ser_String mstring;
        int64_t    mi64;
        uint64_t   mu64;
        double     mf64;
        float      mf32;
        bool       mbool;
        struct {
            const uint8_t* recovery;
            uint32_t recovery_len;
            uint32_t depth;
        } mcompound;
    };
} Ser_Value;

static inline Ser_Reader ser_reader_make(const void* data, isize size)  {Ser_Reader r = {(const uint8_t*) data, 0, size}; return r;}
static inline Ser_Reader ser_reader_make_from_string(Ser_String string) {return ser_reader_make(string.data, string.count);}

EXTERNAL bool deser_value(Ser_Reader* r, Ser_Value* out);
EXTERNAL bool deser_iterate_array(const Ser_Value* array, Ser_Value* out_val);
EXTERNAL bool deser_iterate_object(const Ser_Value* object, Ser_Value* out_key, Ser_Value* out_val);
EXTERNAL void deser_skip_to_depth(Ser_Reader* r, isize depth);

ATTRIBUTE_INLINE_NEVER EXTERNAL bool ser_convert_generic_num(Ser_Type type, uint64_t generic_num, Ser_Type target_type, void* out);

static inline bool deser_null(Ser_Value object)                    { return object.type == SER_NULL; }
static inline bool deser_bool(Ser_Value object, bool* val)         { if(object.type == SER_BOOL)   { *val = object.mbool; return true; } return false; }
static inline bool deser_binary(Ser_Value object, Ser_String* val) { if(object.type == SER_BINARY) { *val = object.mbinary; return true; } return false; }
static inline bool deser_string(Ser_Value object, Ser_String* val) { if(object.type == SER_STRING) { *val = object.mstring; return true; } return false; }

static inline bool deser_i64(Ser_Value val, int64_t* out)   { if(val.exact_type == SER_I64) {*out = (int64_t) val.mi64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_I64, out); }
static inline bool deser_i32(Ser_Value val, int32_t* out)   { if(val.exact_type == SER_I32) {*out = (int32_t) val.mi64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_I32, out); }
static inline bool deser_i16(Ser_Value val, int16_t* out)   { if(val.exact_type == SER_I16) {*out = (int16_t) val.mi64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_I16, out); }
static inline bool deser_i8(Ser_Value val, int8_t* out)     { if(val.exact_type == SER_I8)  {*out = (int8_t)  val.mi64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_I8, out); }

static inline bool deser_u64(Ser_Value val, uint64_t* out)  { if(val.exact_type == SER_U64) {*out = (uint64_t) val.mu64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_U64, out); }
static inline bool deser_u32(Ser_Value val, uint32_t* out)  { if(val.exact_type == SER_U32) {*out = (uint32_t) val.mu64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_U32, out); }
static inline bool deser_u16(Ser_Value val, uint16_t* out)  { if(val.exact_type == SER_U16) {*out = (uint16_t) val.mu64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_U16, out); }
static inline bool deser_u8(Ser_Value val, uint8_t* out)    { if(val.exact_type == SER_U8)  {*out = (uint8_t)  val.mu64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_U8, out); }

static inline bool deser_f64(Ser_Value val, double* out)    { if(val.exact_type == SER_F64) {*out = (double) val.mf64; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_F64, out); }
static inline bool deser_f32(Ser_Value val, float* out)     { if(val.exact_type == SER_F32) {*out = (float) val.mf32; return true;} return ser_convert_generic_num(val.type, val.mu64, SER_F32, out); }

static inline bool ser_string_eq(Ser_Value value, Ser_String str) {
    return value.type == SER_STRING 
        && value.mstring.count == str.count 
        && memcmp(value.mstring.data, str.data, str.count) == 0;
}

static inline bool ser_cstring_eq(Ser_Value value, const char* str) {
    isize len = str ? strlen(str) : 0;
    return value.type == SER_STRING 
        && value.mstring.count == len 
        && memcmp(value.mstring.data, str, len) == 0;
}

EXTERNAL bool ser_write_json(Ser_Writer* w, Ser_Value val, isize indent_or_negative, isize max_recursion);
EXTERNAL bool ser_write_json_read(Ser_Writer* w, Ser_Reader* r, isize indent_or_negative, isize max_recursion);
#endif

#if (defined(MODULE_IMPL_ALL) || defined(MODULE_IMPL_SERIALIZE)) && !defined(MODULE_HAS_IMPL_SERIALIZE)
#define MODULE_HAS_IMPL_SERIALIZE

EXTERNAL void ser_writer_init(Ser_Writer* w, void* buffer_or_null, isize size, Allocator* alloc_or_null_if_malloc)
{
    ser_writer_deinit(w);
    w->alloc = alloc_or_null_if_malloc;
    if(buffer_or_null) {
        w->has_user_buffer = true;
        w->data = (uint8_t*) buffer_or_null;
        w->capacity = size;
    }
}

EXTERNAL void ser_writer_deinit(Ser_Writer* w)
{
    if(w->has_user_buffer == false) {
        if(w->alloc) 
            (*w->alloc)(w->alloc, 0, 0, w->data, w->capacity, 1, NULL);
        else
            free(w->data);
    }
    memset(w, 0, sizeof *w);
}

ATTRIBUTE_INLINE_NEVER 
EXTERNAL void ser_writer_grow(Ser_Writer* w, isize size)
{
    isize new_capacity = w->capacity*3/2 + 8;
    if(new_capacity < size)
        new_capacity = size;

    void* new_data = NULL;
    void* old_buffer = w->has_user_buffer ? 0 : w->data;
    isize old_capacity = w->has_user_buffer ? 0 : w->capacity;
    if(w->alloc) 
        new_data = (*w->alloc)(w->alloc, 0, new_capacity, old_buffer, old_capacity, 1, NULL);
    else
        new_data = realloc(old_buffer, new_capacity);

    if(w->has_user_buffer) {
        memcpy(new_data, w->data, w->capacity);
        w->has_user_buffer = false;
    }
    memset((uint8_t*) new_data + old_capacity, 0, new_capacity - old_capacity);
    w->data = (uint8_t*) new_data;
    w->capacity = new_capacity;
}

EXTERNAL void ser_string_separate(Ser_Writer* w, const void* ptr, isize size)
{
    if(w->offset + size+10 > w->capacity)
        ser_writer_grow(w, size+10);

    if(size <= 0)
        w->data[w->offset++] = (uint8_t) SER_STRING_0;
    else
    {
        if(size >= 256) {
            w->data[w->offset++] = (uint8_t) SER_STRING_64;
            memcpy(w->data + w->offset, &size, sizeof size); w->offset += sizeof(size);
        }
        else {
            w->data[w->offset++] = (uint8_t) SER_STRING_8;
            w->data[w->offset++] = (uint8_t) size;
        }
        
        memcpy(w->data + w->offset, ptr, size); w->offset += size;
        w->data[w->offset++] = 0;
    }
}

EXTERNAL void ser_binary(Ser_Writer* w, const void* ptr, isize size)
{
    ser_writer_reserve(w, size+9);
    w->data[w->offset] = (uint8_t) SER_BINARY;       w->offset += 1;
    memcpy(w->data + w->offset, &size, sizeof size); w->offset += sizeof(size);
    memcpy(w->data + w->offset, ptr, size);          w->offset += size;
}

EXTERNAL void ser_custom_recovery(Ser_Writer* w, Ser_Type type, const void* ptr, isize size, const void* ptr2, isize size2)
{
    uint8_t usize = (uint8_t) (size + size2);
    ser_primitive(w, type, &usize, sizeof usize);
    ser_writer_write(w, ptr, size);
    ser_writer_write(w, ptr2, size2);
}

EXTERNAL void ser_custom_recovery_with_hash(Ser_Writer* w, Ser_Type type, const char* str)
{
    uint32_t hash = 2166136261UL;
    const uint8_t* data = (const uint8_t*) str;
    int64_t len = 0;
    for(; len < 254 && data[len] != 0; len++)
    {
        hash ^= data[len];
        hash *= 16777619;
    }
    ser_custom_recovery(w, type, str, len + 1, &hash, sizeof hash);
}

ATTRIBUTE_INLINE_ALWAYS
static bool deser_read(Ser_Reader* r, void* ptr, isize size)
{
    if(r->offset + size > r->capacity)
        return false;

    memcpy(ptr, r->data + r->offset, size);
    r->offset += size;
    return true;
}

ATTRIBUTE_INLINE_ALWAYS
static bool deser_skip(Ser_Reader* r, isize size)
{
    if(r->offset + size > r->capacity)
        return false;

    r->offset += size;
    return true;
}

EXTERNAL bool deser_value(Ser_Reader* r, Ser_Value* out_val)
{
    Ser_Value out = {0};
    out.type = SER_ERROR;
    out.exact_type = SER_ERROR;
    out.r = r;
    isize offset_before = r->offset; 

    uint8_t uncast_type = 0; 
    uint8_t ok = true;
    if(deser_read(r, &uncast_type, sizeof uncast_type))
    {
        Ser_Type type = (Ser_Type) uncast_type;
        out.exact_type = type;
        out.type = type;
        switch (type)
        {
            case SER_NULL: { out.type = SER_NULL; } break;
            case SER_BOOL: { ok = deser_read(r, &out.mbool, 1); } break;

            case SER_U8:  { ok = deser_read(r, &out.mu64, 1); out.type = SER_I64; } break;
            case SER_U16: { ok = deser_read(r, &out.mu64, 2); out.type = SER_I64; } break;
            case SER_U32: { ok = deser_read(r, &out.mu64, 4); out.type = SER_I64; } break;
            case SER_U64: { ok = deser_read(r, &out.mu64, 8); out.type = SER_U64; } break;
            
            case SER_I8:  { int8_t  val = 0; ok = deser_read(r, &val, 1); out.mi64 = val; out.type = SER_I64; } break;
            case SER_I16: { int16_t val = 0; ok = deser_read(r, &val, 2); out.mi64 = val; out.type = SER_I64; } break;
            case SER_I32: { int32_t val = 0; ok = deser_read(r, &val, 4); out.mi64 = val; out.type = SER_I64; } break;
            case SER_I64: { int64_t val = 0; ok = deser_read(r, &val, 8); out.mi64 = val; out.type = SER_I64; } break;
            
            case SER_F8:  { uint8_t  val = 0; ok = deser_read(r, &val, 1); out.mu64 = val; } break;
            case SER_F16: { uint16_t val = 0; ok = deser_read(r, &val, 2); out.mu64 = val; } break;
            case SER_F32: { float    val = 0; ok = deser_read(r, &val, 4); out.mf32 = val; } break;
            case SER_F64: { double   val = 0; ok = deser_read(r, &val, 8); out.mf64 = val; } break;

            case SER_ARRAY_END:
            case SER_OBJECT_END:    { out.mcompound.depth = (uint32_t) r->depth; r->depth -= 1; } break;
            case SER_ARRAY_BEGIN:    
            case SER_OBJECT_BEGIN:  { out.mcompound.depth = (uint32_t) r->depth; r->depth += 1; } break;

            case SER_RECOVERY_ARRAY_END:
            case SER_RECOVERY_OBJECT_END:    
            case SER_RECOVERY_ARRAY_BEGIN:    
            case SER_RECOVERY_OBJECT_BEGIN:  { 
                uint8_t size = 0;
                ok &= deser_read(r, &size, sizeof size);
                out.mcompound.recovery = r->data + r->offset;
                out.mcompound.recovery_len = size;
                out.mcompound.depth = (uint32_t) r->depth;

                ok &= deser_skip(r, out.mstring.count);
                if(ok) {
                    if((uint32_t) type - SER_ARRAY_END < SER_COMPOUND_TYPES_COUNT) 
                        r->depth -= 1; 
                    else 
                        r->depth += 1; 
                }
            } break;

            case SER_STRING_0:  { 
                out.type = SER_STRING; 
                out.mstring.data = "";
                out.mstring.count = 0;
            } break;
            case SER_STRING_8:
            case SER_STRING_64:  { 
                uint8_t null = 0;
                uint8_t size = 0;
                out.type = SER_STRING;
                if(type == SER_STRING_64) 
                    ok &= deser_read(r, &out.mstring.count, sizeof out.mstring.count);
                else {
                    ok &= deser_read(r, &size, sizeof size);
                    out.mstring.count = size;
                }
                
                out.mstring.data = (char*) (void*) (r->data + r->offset);
                
                ok &= deser_skip(r, out.mstring.count);
                ok &= deser_read(r, &null, sizeof null);
                ok &= null == 0;
            } break;

            case SER_BINARY:  { 
                out.type = SER_BINARY;
                ok &= deser_read(r, &out.mbinary.count, sizeof out.mbinary.count);
                out.mbinary.data = (char*) (void*) (r->data + r->offset);
                ok &= deser_skip(r, out.mbinary.count);
            } break;
            
            default: { ok = false; } break;
        }
    }

    if(ok == false) {
        out.type = SER_ERROR;
        r->offset = offset_before;
    }

    *out_val = out;
    return ok;
}

EXTERNAL void deser_skip_to_depth(Ser_Reader* r, isize depth)
{
    Ser_Value val = {0};
    while(r->depth != depth && val.type != SER_ERROR)
        deser_value(r, &val);
}

ATTRIBUTE_INLINE_NEVER static bool _deser_recover(const Ser_Value* object);

inline static bool _ser_type_is_ender_or_error(Ser_Type type)
{
    return (uint32_t) type - SER_ARRAY_END <= SER_COMPOUND_TYPES_COUNT;
}

EXTERNAL bool deser_iterate_array(const Ser_Value* array, Ser_Value* out_val)
{
    if(array->type != SER_ARRAY && array->type != SER_RECOVERY_ARRAY)
        return false;

    deser_skip_to_depth(array->r, array->mcompound.depth + 1);
    deser_value(array->r, out_val);
    if(_ser_type_is_ender_or_error(out_val->type))
    {
        if(array->type != out_val->type - SER_COMPOUND_TYPES_COUNT)
            _deser_recover(array);
        return false;
    }

    return true;
}

EXTERNAL bool deser_iterate_object(const Ser_Value* object, Ser_Value* out_key, Ser_Value* out_val)
{
    if(object->type != SER_OBJECT && object->type != SER_RECOVERY_OBJECT)
        return false;

    deser_skip_to_depth(object->r, object->mcompound.depth + 1);
    deser_value(object->r, out_key);
    if(_ser_type_is_ender_or_error(out_key->type)) 
    {
        //if the ending type does not correspond to the object type
        if(object->type != out_key->type - SER_COMPOUND_TYPES_COUNT)
            goto recover;
        return false;
    }

    //NOTE: can be removed if we disallow dynamic as keys
    // then this case will just full under error.
    deser_skip_to_depth(object->r, object->mcompound.depth + 1); 
    deser_value(object->r, out_val);
    if(_ser_type_is_ender_or_error(out_key->type))
        goto recover;

    return true;

    recover:
    _deser_recover(object);
    return false;
}

static isize _ser_find_first_or(Ser_String in_str, Ser_String search_for, isize from, isize if_not_found)
{
    ASSERT(from >= 0);
    if(from + search_for.count > in_str.count)
        return if_not_found;
    
    if(search_for.count == 0)
        return from;

    if(search_for.count == 1)
    {
        const char* found = (const char*) memchr(in_str.data + from, search_for.data[0], (size_t) in_str.count - from);
        if(found == NULL)
            return if_not_found;
        return found - in_str.data;
    }

    const char* found = in_str.data + from;
    char last_char = search_for.data[search_for.count - 1];
    char first_char = search_for.data[0];

    while (true)
    {
        isize remaining_length = in_str.count - (found - in_str.data) - search_for.count + 1;
        ASSERT(remaining_length >= 0);

        found = (const char*) memchr(found, first_char, (size_t) remaining_length);
        if(found == NULL)
            return if_not_found;
            
        char last_char_of_found = found[search_for.count - 1];
        if (last_char_of_found == last_char)
            if (memcmp(found + 1, search_for.data + 1, (size_t) search_for.count - 2) == 0)
                return found - in_str.data;

        found += 1;
    }

    return if_not_found;
}

ATTRIBUTE_INLINE_NEVER
static bool _deser_recover(const Ser_Value*  object)
{
    if(object->type == SER_ARRAY || object->type == SER_OBJECT)
        return false;

    Ser_Reader* reader = object->r;
    
    isize recovery_len = 1;
    char recovery_text[270];
    {
        isize i = 0;
        recovery_text[i++] = object->type == SER_RECOVERY_ARRAY ? SER_RECOVERY_ARRAY_END : SER_RECOVERY_OBJECT_END;
        recovery_text[i++] = (uint8_t) object->mstring.count;
        memcpy(recovery_text + i, object->mstring.data, object->mstring.count); i += object->mstring.count + 2;
        recovery_len = i;
    }

    Ser_String total = {(char*) reader->data, reader->capacity};
    Ser_String recovery = {recovery_text, recovery_len};
    isize recovered = _ser_find_first_or(total, recovery, reader->offset, -1);
    if(recovered != -1) 
        reader->offset = recovered;

    return recovered != -1;
}

ATTRIBUTE_INLINE_NEVER
EXTERNAL bool ser_convert_generic_num(Ser_Type type, uint64_t generic_num, Ser_Type target_type, void* out)
{
    union {
        uint64_t mu64;
        int64_t mi64;
        double mf64;
        float mf32;
    } object = {generic_num};
    
    bool state = true;
    if(target_type == SER_F32) {
        float val = 0;
        switch(type) {
            case SER_F64: {
                val = (float) object.mf64; 
                //strange comparison becase of nans
                double back = val; state = memcmp(&back, &object.mf64, sizeof back) == 0;
            } break; 
            case SER_F32: val = (float) object.mf32; break;
            case SER_I64: val = (float) object.mi64; state = (int64_t) val == object.mi64; break;
            case SER_U64: val = (float) object.mu64; state = (uint64_t) val == object.mu64; break;
            default: state = false; break;
        }

        if(state)
            *(float*) out = val;
    }
    else if(target_type == SER_F64) {
        double val = 0;
        switch(type) {
            case SER_F64: val = (double) object.mf64; break;
            case SER_F32: val = (double) object.mf32; break;
            case SER_I64: val = (double) object.mi64; state = (int64_t) val == object.mi64; break;
            case SER_U64: val = (double) object.mu64; state = (uint64_t) val == object.mu64; break;
            default: state = false; break;
        }

        if(state)
            *(double*) out = val;
    }
    else if(target_type == SER_U64 && type == SER_U64) {
        *(uint64_t*) out = object.mu64;
    }
    else
    {
        int64_t val = 0;
        switch(type) {
            case SER_U64: val = (int64_t) object.mu64; state = object.mu64 <= INT64_MAX; break;
            case SER_I64: val = (int64_t) object.mi64; break;
            case SER_F64: val = (int64_t) object.mf64; state = val == object.mf64; break;
            case SER_F32: val = (int64_t) object.mf32; state = val == object.mf32; break;
            default: state = false; break;
        }

        if(state) {
            switch(target_type) {
                case SER_I64: state = INT64_MIN <= val && val <= INT64_MAX; if(state) *(int64_t*) out = (int64_t) val; break; 
                case SER_I32: state = INT32_MIN <= val && val <= INT32_MAX; if(state) *(int32_t*) out = (int32_t) val; break; 
                case SER_I16: state = INT16_MIN <= val && val <= INT16_MAX; if(state) *(int16_t*) out = (int16_t) val; break; 
                case SER_I8:  state = INT8_MIN  <= val && val <= INT8_MAX;  if(state) *(int8_t*)  out = (int8_t) val; break; 
                
                case SER_U64: state = 0 <= val && val <= UINT64_MAX; if(state) *(uint64_t*) out = (uint64_t) val; break; 
                case SER_U32: state = 0 <= val && val <= UINT32_MAX; if(state) *(uint32_t*) out = (uint32_t) val; break; 
                case SER_U16: state = 0 <= val && val <= UINT16_MAX; if(state) *(uint16_t*) out = (uint16_t) val; break; 
                case SER_U8:  state = 0 <= val && val <= UINT8_MAX;  if(state) *(uint8_t*)  out = (uint8_t) val; break; 

                default: state = false; break;
            }
        }
    }
    return state;
}

static void ser_write_newline(Ser_Writer* w, isize indent_or_negative, isize depth)
{
    if(indent_or_negative >= 0) {
        ser_writer_reserve(w, depth*indent_or_negative + 1);
        w->data[w->offset++] = '\n';
        for(isize i = 0; i < depth*indent_or_negative; i++)
            w->data[w->offset++] = ' ';
    }
}

static bool _ser_write_json(Ser_Writer* w, Ser_Value val, isize indent_or_negative, isize max_recursion, isize depth)
{
    //TODO: how do I handle this better???
    if(depth > max_recursion)
        return false;

    switch(val.type) {
        case SER_OBJECT: 
        case SER_RECOVERY_OBJECT: {
            isize object_len = 0;
            ser_writer_write(w, "{", 1);
            for(Ser_Value key = {0}, value = {0}; deser_iterate_object(&val, &key, &value); object_len ++) {
                if(object_len > 0)
                    ser_writer_write(w, ",", 1);
                ser_write_newline(w, indent_or_negative, depth + 1);

                _ser_write_json(w, key, indent_or_negative, max_recursion, depth + 1);
                ser_writer_write(w, ": ", 1 + (indent_or_negative >= 0));
                _ser_write_json(w, value, indent_or_negative, max_recursion, depth + 1);
            }

            if(object_len > 0)
                ser_write_newline(w, indent_or_negative, depth);
            ser_writer_write(w, "}", 1);
        } break; 
        
        case SER_ARRAY: 
        case SER_RECOVERY_ARRAY: {
            isize array_len = 0;
            ser_writer_write(w, "[", 1);
            for(Ser_Value value = {0}; deser_iterate_array(&val, &value); array_len++) {
                if(array_len > 0)
                    ser_writer_write(w, ",", 1);
                ser_write_newline(w, indent_or_negative, depth + 1);
                    
                _ser_write_json(w, value, indent_or_negative, max_recursion, depth + 1);
            }
            
            if(array_len > 0)
                ser_write_newline(w, indent_or_negative, depth);
            ser_writer_write(w, "]", 1);
        } break; 

        case SER_NULL: {
            ser_writer_write(w, "null", 4); 
        } break; 

        case SER_BOOL: {
            val.mbool ? ser_writer_write(w, "true", 4) : ser_writer_write(w, "false", 5); 
        } break;
            
        case SER_I64: {
            char buffer[64] = {0};
            int count = snprintf(buffer, sizeof buffer, "%lli", (long long) val.mi64);
            ser_writer_write(w, buffer, count); 
        } break;
            
        case SER_U64: {
            char buffer[64] = {0};
            int count = snprintf(buffer, sizeof buffer, "%llu", (unsigned long long) val.mi64);
            ser_writer_write(w, buffer, count); 
        } break;

        case SER_F64:
        case SER_F32: {
            char buffer[64] = {0};
            double cast = val.type == SER_F64 ? val.mf64 : val.mf32;
            int count = 0;
                
            //do a strange lower bound to find the minimum number of significant digits to print
		    int digits_low_i = 0;
		    int digits_count = val.type == SER_F64 ? 14 : 10;
		    while (digits_count > 0)
		    {
			    int step = digits_count / 2;
			    int curr = digits_low_i + step;
                count = snprintf(buffer, sizeof buffer, "%.*lf", digits_count, cast);
                double reconstructed = strtod(buffer, NULL);
			    if(reconstructed != cast)
			    {
				    digits_low_i = curr + 1;
				    digits_count -= step + 1;
			    }
			    else
				    digits_count = step;
		    }
            
            if(digits_count == 0)
                count = snprintf(buffer, sizeof buffer, "%.*lf", digits_count, cast);
            ser_writer_write(w, buffer, count); 
        } break;
            
        case SER_BINARY: {
            const char* hex = "0123456789ABCDEF";
            ser_writer_reserve(w, val.mbinary.count*2 + 2);

            w->data[w->offset++] = '"';
            for(isize i = 0; i < val.mbinary.count; i++)
            {
                uint8_t c = (uint8_t) val.mbinary.data[i];
                w->data[w->offset++] = hex[c >> 4];
                w->data[w->offset++] = hex[c & 7];
            }
            w->data[w->offset++] = '"';
        } break;

        case SER_STRING: {
            ser_writer_reserve(w, val.mstring.count + 2);
                
            w->data[w->offset++] = '"';
            for(isize i = 0; i < val.mbinary.count; i++)
            {
                //properly escape the json string
                uint8_t c = (uint8_t) val.mbinary.data[i];
                switch(c)
                {
                    case '"':  ser_writer_write(w, "\\\"", 2); break;
                    case '\\': ser_writer_write(w, "\\\\", 2); break;
                    case '\b': ser_writer_write(w, "\\b", 2); break;
                    case '\f': ser_writer_write(w, "\\f", 2); break;
                    case '\n': ser_writer_write(w, "\\n", 2); break;
                    case '\r': ser_writer_write(w, "\\r", 2); break;
                    case '\t': ser_writer_write(w, "\\t", 2); break;
                    default: {
                        if(c > 0x001F)
                            ser_writer_write(w, &c, 1); 
                        else {
                            char buffer[16] = {0};
                            int count = snprintf(buffer, sizeof buffer, "\\u%04x", (unsigned) c);
                            ser_writer_write(w, buffer, count); 
                        }
                    } break;
                }
            }
            ser_writer_write(w, "\"", 1); 
        } break;
    }

    ser_writer_reserve(w, 1);
    w->data[w->offset] = '\0';
    return true;
}

EXTERNAL bool ser_write_json(Ser_Writer* w, Ser_Value val, isize indent_or_negative, isize max_recursion)
{
    return _ser_write_json(w, val, indent_or_negative, max_recursion, 0);
}

EXTERNAL bool ser_write_json_read(Ser_Writer* w, Ser_Reader* r, isize indent_or_negative, isize max_recursion)
{
    Ser_Value val = {0};
    if(deser_value(r, &val))
        return _ser_write_json(w, val, indent_or_negative, max_recursion, 0);
    return false;
}
#endif