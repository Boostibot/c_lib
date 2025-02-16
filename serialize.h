#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
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
// Individual primitive types are grouped into json-like lists and objects. Lists are 
// denoted by start and end type bytes. Everything between is inside the list. Objects
// are just like lists except the items are interpreted in pairs of two: the first is
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
// entire format to become corrupted and unvisualiseable. This is generally solved by application specific 
// magic numbers and checksums. I have taken a similar route which accounts very nicely for the generality and
// structure of the format. 
// 
// I provide recovery variants for list/object begin/end which behave just like their regular counterparts 
// except are followed by a user specified magic sequence. The writer is expected to use these a few times
// in the format around large blocks of data (since the magic sequences pose some overhead). The reader on the
// other hand doesnt have to know about these at all. When a parsing error is found within a recovery list/object
// the code attempts to automatically recover by finding the matching end magic sequence for the given list/object.  
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
    typedef struct Allocator Allocator;
    typedef struct Allocator_Error Allocator_Error;
    typedef void* (*Allocator_Func)(Allocator* alloc, isize new_size, void* old_ptr, isize old_size, isize align, Allocator_Error* error_or_null);

    typedef int64_t isize;
    typedef struct Ser_String {
        const char* data;
        isize count;
    } Ser_String;
#endif

#ifndef ASSERT
    #include <assert.h>
    #define ASSERT(x, ...) assert(x)
#endif

#ifndef EXTERNAL
    #define EXTERNAL 
#endif

#ifdef __cplusplus
    #define SER_CSTRING(x) Ser_String{x, sizeof(x"") - 1}
#else
    #define SER_CSTRING(x) (Ser_String){x, sizeof(x"") - 1}
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
    
    SER_LIST_BEGIN,             //{u8 type}
    SER_OBJECT_BEGIN,           //{u8 type}
    SER_RECOVERY_OBJECT_BEGIN,  //{u8 type, u8 size}[size bytes of tag]
    SER_RECOVERY_LIST_BEGIN,    //{u8 type, u8 size}[size bytes of tag]
    
    SER_LIST_END,               //{u8 type}
    SER_OBJECT_END,             //{u8 type}
    SER_RECOVERY_LIST_END,      //{u8 type, u8 size}[size bytes of tag]
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
    SER_LIST = SER_LIST_BEGIN,
    SER_OBJECT = SER_OBJECT_BEGIN,
    SER_RECOVERY_LIST = SER_RECOVERY_LIST_BEGIN,
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
EXTERNAL void ser_string(Ser_Writer* w, const void* ptr, isize size);
static inline void ser_cstring(Ser_Writer* w, const char* ptr) { ser_string(w, ptr, ptr ? strlen(ptr) : 0); }

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

static inline void ser_list_begin(Ser_Writer* w)        { ser_primitive(w, SER_LIST_BEGIN, NULL, 0); }
static inline void ser_list_end(Ser_Writer* w)          { ser_primitive(w, SER_LIST_END, NULL, 0); }
static inline void ser_object_begin(Ser_Writer* w)      { ser_primitive(w, SER_OBJECT_BEGIN, NULL, 0); }
static inline void ser_object_end(Ser_Writer* w)        { ser_primitive(w, SER_OBJECT_END, NULL, 0); }

EXTERNAL void ser_custom_recovery(Ser_Writer* w, Ser_Type type, const void* ptr, isize size, const void* ptr2, isize size2);
EXTERNAL void ser_custom_recovery_with_hash(Ser_Writer* w, Ser_Type type, const char* str);

static inline void ser_recovery_list_begin(Ser_Writer* w, const char* str)      { ser_custom_recovery_with_hash(w, SER_RECOVERY_LIST_BEGIN, str); }
static inline void ser_recovery_list_end(Ser_Writer* w, const char* str)        { ser_custom_recovery_with_hash(w, SER_RECOVERY_LIST_END, str); }
static inline void ser_recovery_object_begin(Ser_Writer* w, const char* str)    { ser_custom_recovery_with_hash(w, SER_RECOVERY_OBJECT_BEGIN, str); }
static inline void ser_recovery_object_end(Ser_Writer* w, const char* str)      { ser_custom_recovery_with_hash(w, SER_RECOVERY_OBJECT_END, str); }

static inline void ser_writer_reserve(Ser_Writer* w, isize size) {
    if(w->offset + size > w->capacity)
        ser_writer_grow(w, size);
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
EXTERNAL bool deser_iterate_list(const Ser_Value* list, Ser_Value* out_val);
EXTERNAL bool deser_iterate_object(const Ser_Value* object, Ser_Value* out_key, Ser_Value* out_val);
EXTERNAL void deser_skip_to_depth(Ser_Reader* r, isize depth);

ATTRIBUTE_INLINE_NEVER EXTERNAL bool deser_generic_num(Ser_Type type, uint64_t generic_num, Ser_Type target_type, void* out);

static inline bool deser_null(Ser_Value object)                    { return object.type == SER_NULL; }
static inline bool deser_bool(Ser_Value object, bool* val)         { if(object.type == SER_BOOL)   { *val = object.mbool; return true; } return false; }
static inline bool deser_binary(Ser_Value object, Ser_String* val) { if(object.type == SER_STRING) { *val = object.mstring; return true; } return false; }
static inline bool deser_string(Ser_Value object, Ser_String* val) { if(object.type == SER_BINARY) { *val = object.mbinary; return true; } return false; }

static inline bool deser_i64(Ser_Value val, int64_t* out)   { if(val.exact_type == SER_I64) {*out = (int64_t) val.mi64; return true;} return deser_generic_num(val.type, val.mu64, SER_I64, out); }
static inline bool deser_i32(Ser_Value val, int32_t* out)   { if(val.exact_type == SER_I32) {*out = (int32_t) val.mi64; return true;} return deser_generic_num(val.type, val.mu64, SER_I32, out); }
static inline bool deser_i16(Ser_Value val, int16_t* out)   { if(val.exact_type == SER_I16) {*out = (int16_t) val.mi64; return true;} return deser_generic_num(val.type, val.mu64, SER_I16, out); }
static inline bool deser_i8(Ser_Value val, int8_t* out)     { if(val.exact_type == SER_I8)  {*out = (int8_t)  val.mi64; return true;} return deser_generic_num(val.type, val.mu64, SER_I8, out); }

static inline bool deser_u64(Ser_Value val, uint64_t* out)  { if(val.exact_type == SER_U64) {*out = (uint64_t) val.mu64; return true;} return deser_generic_num(val.type, val.mu64, SER_U64, out); }
static inline bool deser_u32(Ser_Value val, uint32_t* out)  { if(val.exact_type == SER_U32) {*out = (uint32_t) val.mu64; return true;} return deser_generic_num(val.type, val.mu64, SER_U32, out); }
static inline bool deser_u16(Ser_Value val, uint16_t* out)  { if(val.exact_type == SER_U16) {*out = (uint16_t) val.mu64; return true;} return deser_generic_num(val.type, val.mu64, SER_U16, out); }
static inline bool deser_u8(Ser_Value val, uint8_t* out)    { if(val.exact_type == SER_U8)  {*out = (uint8_t)  val.mu64; return true;} return deser_generic_num(val.type, val.mu64, SER_U8, out); }

static inline bool deser_f64(Ser_Value val, double* out)    { if(val.exact_type == SER_F64) {*out = (double) val.mf64; return true;} return deser_generic_num(val.type, val.mu64, SER_F64, out); }
static inline bool deser_f32(Ser_Value val, float* out)     { if(val.exact_type == SER_F32) {*out = (float) val.mf32; return true;} return deser_generic_num(val.type, val.mu64, SER_F32, out); }

#define ser_cstring_eq(value, cstr) ser_string_eq(value, SER_CSTRING(cstr))
static inline bool ser_string_eq(Ser_Value value, Ser_String str) {
    return value.type == SER_STRING 
        && value.mstring.count == str.count 
        && memcmp(value.mstring.data, str.data, str.count) == 0;
}

//IMPL ==============================
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
            ((Allocator_Func) (void*) w->alloc)(w->alloc, NULL, w->data, w->capacity, 1, NULL);
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
        new_data = ((Allocator_Func) (void*) w->alloc)(w->alloc, new_capacity, old_buffer, old_capacity, 1, NULL);
    else
        new_data = realloc(old_buffer, new_capacity);

    if(w->has_user_buffer) {
        memcpy(new_data, w->data, w->capacity);
        w->has_user_buffer = false;
    }

    w->data = (uint8_t*) new_data;
    w->capacity = new_capacity;
}

EXTERNAL void ser_string(Ser_Writer* w, const void* ptr, isize size)
{
    if(w->offset + size+10 > w->capacity)
        ser_writer_grow(w, size+10);

    if(size <= 0)
        w->data[w->offset] = (uint8_t) SER_STRING_0;
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
    uint8_t null = 0;
    uint8_t usize = (uint8_t) size;
    ser_primitive(w, type, &usize, size);
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

            case SER_LIST_END:
            case SER_OBJECT_END:    { out.mcompound.depth = r->depth; r->depth -= 1; } break;
            case SER_LIST_BEGIN:    
            case SER_OBJECT_BEGIN:  { out.mcompound.depth = r->depth; r->depth += 1; } break;

            case SER_RECOVERY_LIST_END:
            case SER_RECOVERY_OBJECT_END:    
            case SER_RECOVERY_LIST_BEGIN:    
            case SER_RECOVERY_OBJECT_BEGIN:  { 
                uint8_t size = 0;
                ok &= deser_read(r, &size, sizeof size);
                out.mcompound.recovery = r->data + r->offset;
                out.mcompound.recovery_len = size;
                out.mcompound.depth = 0;
                ok &= deser_skip(r, out.mstring.count);
                if(ok) {
                    if((uint32_t) type - SER_LIST_END < SER_COMPOUND_TYPES_COUNT)
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
    return (uint32_t) type - SER_LIST_END <= SER_COMPOUND_TYPES_COUNT;
}

EXTERNAL bool deser_iterate_list(const Ser_Value* list, Ser_Value* out_val)
{
    if(list->type != SER_LIST && list->type != SER_RECOVERY_LIST)
        return false;

    deser_skip_to_depth(list->r, list->mcompound.depth);
    deser_value(list->r, out_val);
    if(_ser_type_is_ender_or_error(out_val->type))
    {
        if(list->type != out_val->type - SER_COMPOUND_TYPES_COUNT)
            _deser_recover(list);
        return false;
    }

    return true;
}

EXTERNAL bool deser_iterate_object(const Ser_Value* object, Ser_Value* out_key, Ser_Value* out_val)
{
    if(object->type != SER_OBJECT && object->type != SER_RECOVERY_OBJECT)
        return false;

    deser_skip_to_depth(object->r, object->mcompound.depth);
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
    deser_skip_to_depth(object->r, object->mcompound.depth); 
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
    if(object->type == SER_LIST || object->type == SER_OBJECT)
        return false;

    Ser_Reader* reader = object->r;
    
    isize recovery_len = 1;
    char recovery_text[270];
    {
        isize i = 0;
        recovery_text[i++] = object->type == SER_RECOVERY_LIST ? SER_RECOVERY_LIST_END : SER_RECOVERY_OBJECT_END;
        recovery_text[i++] = (uint8_t) object->mstring.count;
        memcpy(recovery_text + i, object->mstring.data, object->mstring.count); i += object->mstring.count + 2;
        recovery_len = i;
    }

    Ser_String total = {(char*) reader->data, reader->capacity};
    Ser_String recovery = {recovery_text, recovery_len};
    isize recovered = _ser_find_first_or(total, recovery, reader->offset, -1);
    if(recovered != -1) {
        reader->offset = recovered;
        printf("recovered!\n");
    }

    return recovered != -1;
}

ATTRIBUTE_INLINE_NEVER
EXTERNAL bool deser_generic_num(Ser_Type type, uint64_t generic_num, Ser_Type target_type, void* out)
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
            case SER_F64: val = (float) object.mf64; state = !(val != object.mf64); break; //funny comparison for nans
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
            case SER_U64: val = (int64_t) object.mi64; break;
            case SER_I64: val = (int64_t) object.mu64; state = object.mu64 <= INT64_MAX; break;
            case SER_F64: val = (int64_t) object.mf64; state = val == object.mf64; break;
            case SER_F32: val = (int64_t) object.mf32; state = val == object.mf32; break;
            default: state = false; break;
        }

        if(state) {
            switch(target_type) {
                case SER_I64: state = INT64_MIN <= val && val <= INT64_MAX; if(state) *(int64_t*) out = val; break; 
                case SER_I32: state = INT32_MIN <= val && val <= INT32_MAX; if(state) *(int32_t*) out = val; break; 
                case SER_I16: state = INT16_MIN <= val && val <= INT16_MAX; if(state) *(int16_t*) out = val; break; 
                case SER_I8:  state = INT8_MIN  <= val && val <= INT8_MAX;  if(state) *(int8_t*)  out = val; break; 
                
                case SER_U64: state = 0 <= val && val <= UINT64_MAX; if(state) *(uint64_t*) out = val; break; 
                case SER_U32: state = 0 <= val && val <= UINT32_MAX; if(state) *(uint32_t*) out = val; break; 
                case SER_U16: state = 0 <= val && val <= UINT16_MAX; if(state) *(uint16_t*) out = val; break; 
                case SER_U8:  state = 0 <= val && val <= UINT8_MAX;  if(state) *(uint8_t*)  out = val; break; 

                default: state = false; break;
            }
        }
    }
    return state;
}


//START of test
bool deser_f32v3(const Ser_Value* object, float out[3])
{
    if(object->type == SER_LIST)
    {
        int count = 0;
        for(Ser_Value val = {0}; deser_iterate_list(object, &val); ) {
            count += deser_f32(val, &out[count]);
            if(count >= 3)
                break;
        }

        return count >= 3;
    }
    else if(object->type == SER_OBJECT)
    {
        int parts = 0;
        for(Ser_Value key = {0}, val = {0}; deser_iterate_object(object, &key, &val); ) 
        {
                 if(ser_cstring_eq(key, "x")) parts |= deser_f32(val, &out[0]) << 0;
            else if(ser_cstring_eq(key, "y")) parts |= deser_f32(val, &out[1]) << 1;
            else if(ser_cstring_eq(key, "z")) parts |= deser_f32(val, &out[2]) << 2;
        }

        return parts == 7;
    }
    return false;    
}

void ser_f32v3(Ser_Writer* w, const float vals[3]) { 
    uint8_t data[32];
    int i = 0, c = 0;
    data[i++] = SER_LIST_BEGIN;
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
    data[i++] = SER_LIST_END;
    ser_writer_write(w, data, i);
}

typedef union Vec3 {
    struct {
        float x;
        float y;
        float z;
    };
    float floats[3];
} Vec3;

typedef enum Map_Scale_Filter {
    MAP_SCALE_FILTER_BILINEAR = 0,
    MAP_SCALE_FILTER_TRILINEAR,
    MAP_SCALE_FILTER_NEAREST,
} Map_Scale_Filter;

typedef enum Map_Repeat {
    MAP_REPEAT_REPEAT = 0,
    MAP_REPEAT_MIRRORED_REPEAT,
    MAP_REPEAT_CLAMP_TO_EDGE,
    MAP_REPEAT_CLAMP_TO_BORDER
} Map_Repeat;

#define MAX_CHANNELS 4
typedef struct Map_Info {
    Vec3 offset;                
    Vec3 scale; //default to 1 1 1
    Vec3 resolution;

    int32_t channels_count; 
    int32_t channels_indices1[MAX_CHANNELS]; 

    Map_Scale_Filter filter_minify;
    Map_Scale_Filter filter_magnify;
    Map_Repeat repeat_u;
    Map_Repeat repeat_v;
    Map_Repeat repeat_w;

    float gamma;          //default 2.2
    float brightness;     //default 0
    float contrast;       //default 0
} Map_Info;

bool deser_map_repeat(const Ser_Value* val, Map_Repeat* repeat)
{
    if(0) {}
    else if(ser_cstring_eq(*val, "repeat"))          *repeat = MAP_REPEAT_REPEAT;
    else if(ser_cstring_eq(*val, "mirrored"))        *repeat = MAP_REPEAT_MIRRORED_REPEAT;
    else if(ser_cstring_eq(*val, "clamp_to_edge"))   *repeat = MAP_REPEAT_CLAMP_TO_EDGE;
    else if(ser_cstring_eq(*val, "clamp_to_border")) *repeat = MAP_REPEAT_CLAMP_TO_BORDER;
    else return false; //log here
    return true;
}

bool deser_map_scale_filter(const Ser_Value* val, Map_Scale_Filter* filter)
{
    if(0) {}
    else if(ser_cstring_eq(*val, "bilinear")) *filter = MAP_SCALE_FILTER_BILINEAR;
    else if(ser_cstring_eq(*val, "trilinear"))*filter = MAP_SCALE_FILTER_TRILINEAR;
    else if(ser_cstring_eq(*val, "nearest"))  *filter = MAP_SCALE_FILTER_NEAREST;
    else return false; //log here
    return true;
}

bool deser_map_info(Ser_Value object, Map_Info* out_map_info)
{
    Map_Info out = {0};
    Vec3 scale = {1,1,1};
    out.scale = scale;
    out.gamma = 2.2f;

    for(Ser_Value key, val; deser_iterate_object(&object, &key, &val); )
    {
        /**/ if(ser_cstring_eq(key, "offset"))          deser_f32v3(&val, out.offset.floats);
        else if(ser_cstring_eq(key, "scale"))           deser_f32v3(&val, out.scale.floats);
        else if(ser_cstring_eq(key, "resolution"))      deser_f32v3(&val, out.resolution.floats);
        else if(ser_cstring_eq(key, "filter_minify"))   deser_map_scale_filter(&val, &out.filter_minify);
        else if(ser_cstring_eq(key, "filter_magnify"))  deser_map_scale_filter(&val, &out.filter_magnify);
        else if(ser_cstring_eq(key, "repeat_u"))        deser_map_repeat(&val, &out.repeat_u); //log here
        else if(ser_cstring_eq(key, "repeat_v"))        deser_map_repeat(&val, &out.repeat_v);
        else if(ser_cstring_eq(key, "repeat_w"))        deser_map_repeat(&val, &out.repeat_w);
        else if(ser_cstring_eq(key, "gamma"))           deser_f32(val, &out.gamma);
        else if(ser_cstring_eq(key, "brightness"))      deser_f32(val, &out.brightness);
        else if(ser_cstring_eq(key, "contrast"))        deser_f32(val, &out.contrast);
        else if(ser_cstring_eq(key, "channels_count"))  deser_i32(val, &out.channels_count);
        else if(ser_cstring_eq(key, "channels_indices1"))
        {
            int i = 0;
            for(Ser_Value item; deser_iterate_list(&val, &item); )
                i += deser_i32(item, &out.channels_indices1[i]); //log here
        }
    }

    *out_map_info = out;
    return true;
}

ATTRIBUTE_INLINE_NEVER
void ser_map_repeat(Ser_Writer* w, Map_Repeat repeat)
{
    switch(repeat)
    {
        case MAP_REPEAT_REPEAT:             ser_cstring(w, "repeat"); break;
        case MAP_REPEAT_MIRRORED_REPEAT:    ser_cstring(w, "mirrored"); break;
        case MAP_REPEAT_CLAMP_TO_EDGE:      ser_cstring(w, "clamp_to_edge"); break;
        case MAP_REPEAT_CLAMP_TO_BORDER:    ser_cstring(w, "clamp_to_border"); break;
        default:                            ser_cstring(w, "invalid"); break;
    }
}

ATTRIBUTE_INLINE_NEVER
void ser_map_scale_filter(Ser_Writer* w, Map_Scale_Filter filter)
{
    switch(filter)
    {
        case MAP_SCALE_FILTER_BILINEAR:     ser_cstring(w, "bilinear"); break;
        case MAP_SCALE_FILTER_TRILINEAR:    ser_cstring(w, "trilinear"); break;
        case MAP_SCALE_FILTER_NEAREST:      ser_cstring(w, "nearest"); break; 
        default:                            ser_cstring(w, "invalid"); break; 
    }
}

void ser_map_info(Ser_Writer* w, Map_Info info)
{
    ser_recovery_object_begin(w, "Map_Info");
    ser_cstring(w, "offset");          ser_f32v3(w, info.offset.floats);
    ser_cstring(w, "scale");           ser_f32v3(w, info.scale.floats);
    ser_cstring(w, "resolution");      ser_f32v3(w, info.resolution.floats);
    ser_cstring(w, "filter_minify");   ser_map_scale_filter(w, info.filter_minify);
    ser_cstring(w, "filter_magnify");  ser_map_scale_filter(w, info.filter_magnify);
    ser_cstring(w, "repeat_u");        ser_map_repeat(w, info.repeat_u);
    ser_cstring(w, "repeat_v");        ser_map_repeat(w, info.repeat_v);
    ser_cstring(w, "repeat_w");        ser_map_repeat(w, info.repeat_w);
    ser_cstring(w, "gamma");           ser_f32(w, info.gamma);
    ser_cstring(w, "brightness");      ser_f32(w, info.brightness);
    ser_cstring(w, "contrast");        ser_f32(w, info.contrast);
    ser_cstring(w, "channels_count");  ser_i32(w, info.channels_count);
    ser_cstring(w, "channels_indices1");
    ser_list_begin(w);
    for(int i = 0; i < MAX_CHANNELS; i++)
        ser_i32(w, info.channels_indices1[i]);
    ser_list_end(w);
    ser_recovery_object_end(w, "Map_Info");
}

