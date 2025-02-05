#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
// #include "string.h"

typedef int64_t    isize;

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;

typedef int8_t      i8;
typedef int16_t     i16;
typedef int32_t     i32;
typedef int64_t     i64;

typedef bool        b8;
typedef uint16_t    b16;
typedef uint32_t    b32;
typedef uint64_t    b64;

typedef float       f32;
typedef double      f64;

#define ASSERT(x, ...) assert(x)

//based on https://rxi.github.io/a_simple_serialization_system.html
typedef enum Ser_Type {
    SER_NULL = 0,
    
    //we include "recovery" lists/object. These act just like the regular ones
    // except also contain a tag - some magic number or string which allows
    // us to recover in case of file corruption. Whats nice about this is that
    // this mechanism can be made entirely transparent to the reader and very
    // hustle free for the writer.
    SER_LIST_BEGIN,
    SER_OBJECT_BEGIN,
    SER_RECOVERY_OBJECT_BEGIN,  //{u8 type, u8 size}[size bytes of tag]\0
    SER_RECOVERY_LIST_BEGIN,  //{u8 type, u8 size}[size bytes of tag]
    
    SER_LIST_END,
    SER_OBJECT_END,
    SER_RECOVERY_LIST_END,    //{u8 type, u8 size}[size bytes of tag]\0
    SER_RECOVERY_OBJECT_END,    //{u8 type, u8 size}[size bytes of tag]\0
    SER_ERROR, //"lexing" error. Is near the ENDers section so that we can check for ender or error efficiently 

    SER_STRING_0,  //{u8 type}
    SER_STRING_8,  //{u8 type, u8 size}[size bytes]\0
    SER_STRING_64, //{u8 type, u64 size}[size bytes]\0
    SER_BINARY,    //{u8 type, u64 size}[size bytes]

    SER_BOOL,

    SER_U8,
    SER_U16,
    SER_U32,
    SER_U64,
    
    SER_I8,
    SER_I16,
    SER_I32,
    SER_I64,
    
    SER_F8,
    SER_F16,
    SER_F32,
    SER_F64,

    SER_F32V2,
    SER_F32V3,
    SER_F32V4,
    
    SER_I32V2,
    SER_I32V3,
    SER_I32V4,


    //aliases
    SER_LIST = SER_LIST_BEGIN,
    SER_OBJECT = SER_OBJECT_BEGIN,
    SER_RECOVERY_LIST = SER_RECOVERY_LIST_BEGIN,
    SER_RECOVERY_OBJECT = SER_RECOVERY_OBJECT_BEGIN,
    SER_STRING = SER_STRING_64,
    SER_DYN_COUNT = 4,
} Ser_Type;

typedef struct Ser_Writer {
    uint8_t* data;
    isize depth;
    isize offset;
    isize capacity;
} Ser_Writer;

typedef struct Ser_Reader {
    const uint8_t* data;
    isize depth;
    isize offset;
    isize capacity;

    isize error_count;
    isize recovery_count;
    void (*error_log)(void* w, isize depth, isize offset, const char* fmt, ...);
    void* error_log_w;
} Ser_Reader;

#if 0
    typedef String Ser_String;
#else
    typedef struct Ser_String {
        const char* data;
        isize count;
    } Ser_String;

    #ifdef __cplusplus
        #define STRING(x) Ser_String{x, sizeof(x"") - 1}
    #else
        #define STRING(x) (Ser_String){x, sizeof(x"") - 1}
    #endif
#endif

typedef struct Ser_Value {
    Ser_Reader* w;

    isize depth;
    isize offset;
    Ser_Type exact_type;
    Ser_Type type;

    union {
        Ser_String binary;
        Ser_String string;
        bool vbool;

        f32 f64v4[4];
        i32 i64v4[4];
        f32 f32v4[4];
        i32 i32v4[4];

        u64 generic;
        i64 i64;
        u64 u64;
        f64 f64;
        f32 f32;
    };
} Ser_Value;

int my_size()
{
    return sizeof(Ser_Value);
}

//TODO: get rid of header part for simple parts and instead make them static inline!
//TODO: merge binary and string64 to the same rules thus simplifying parsing and writing code!

isize set_type_size(Ser_Type type);
const char* set_type_name(Ser_Type type);
Ser_Type ser_type_category(Ser_Type type);
bool ser_type_is_numeric(Ser_Type type);
bool ser_type_is_integer(Ser_Type type);
bool ser_type_is_signed_integer(Ser_Type type);
bool ser_type_is_unsigned_integer(Ser_Type type);
bool ser_type_is_float(Ser_Type type);

Ser_Writer ser_file_writer(FILE* file);
void ser_list_begin(Ser_Writer* w);
void ser_list_end(Ser_Writer* w);
void ser_object_begin(Ser_Writer* w);
void ser_object_end(Ser_Writer* w);

void ser_primitive(Ser_Writer* w, Ser_Type type, const void* ptr, isize size);
void ser_binary(Ser_Writer* w, const void* ptr, isize size);
void ser_string(Ser_Writer* w, const void* ptr, isize size);
void ser_null(Ser_Writer* w);
void ser_bool(Ser_Writer* w, bool val);

void ser_i8(Ser_Writer* w, i8 val);
void ser_i16(Ser_Writer* w, i16 val);
void ser_i32(Ser_Writer* w, i32 val);
void ser_i64(Ser_Writer* w, i64 val);

void ser_u8(Ser_Writer* w, u8 val);
void ser_u16(Ser_Writer* w, u16 val);
void ser_u32(Ser_Writer* w, u32 val);
void ser_u64(Ser_Writer* w, u64 val);

void ser_f32(Ser_Writer* w, f32 val);
void ser_f64(Ser_Writer* w, f64 val);

void ser_i32v2(Ser_Writer* w, const i32 vals[2]);
void ser_i32v3(Ser_Writer* w, const i32 vals[3]);
void ser_i32v4(Ser_Writer* w, const i32 vals[4]);

void ser_f32v2(Ser_Writer* w, const f32 vals[2]);
void ser_f32v3(Ser_Writer* w, const f32 vals[3]);
void ser_f32v4(Ser_Writer* w, const f32 vals[4]);


//reading 
void deser_value(Ser_Reader* w, Ser_Value* out);

#define ser_cstring_eq(value, cstr) ser_string_eq(value, STRING(cstr))
bool ser_string_eq(Ser_Value value, Ser_String str);

bool deser_null(Ser_Value object);
bool deser_bool(Ser_Value object, bool* val);
bool deser_binary(Ser_Value object, Ser_String* val);
bool deser_string(Ser_Value object, Ser_String* val);

bool deser_f32(Ser_Value object, f32* val);
bool deser_f64(Ser_Value object, f64* val);

bool deser_i8(Ser_Value object, i8* val);  
bool deser_i16(Ser_Value object, i16* val);
bool deser_i32(Ser_Value object, i32* val);
bool deser_i64(Ser_Value object, i64* val);

bool deser_u8(Ser_Value object, u8* val);  
bool deser_u16(Ser_Value object, u16* val);
bool deser_u32(Ser_Value object, u32* val);
bool deser_u64(Ser_Value object, u64* val);

bool deser_i32v2(Ser_Value object, i32 vals[2]);
bool deser_i32v3(Ser_Value object, i32 vals[3]);
bool deser_i32v4(Ser_Value object, i32 vals[4]);

bool deser_f32v2(Ser_Value object, f32 vals[2]);
bool deser_f32v3(Ser_Value object, f32 vals[3]);
bool deser_f32v4(Ser_Value object, f32 vals[4]);

//IMPL ==============================
isize set_type_size(Ser_Type type)          {return 0;}
const char* set_type_name(Ser_Type type)    {return "";}

Ser_Type ser_type_category(Ser_Type type)
{
    switch (type)
    {
        case SER_U8: return SER_U64;
        case SER_U16: return SER_U64;
        case SER_U32: return SER_U64;
        case SER_U64: return SER_U64;
        
        case SER_I8: return SER_I64;
        case SER_I16: return SER_I64;
        case SER_I32: return SER_I64;
        case SER_I64: return SER_I64;
        
        case SER_F8: return SER_F64;
        case SER_F16: return SER_F64;
        case SER_F32: return SER_F64;
        case SER_F64: return SER_F64;
        default: return type;
    }
}

bool ser_type_is_numeric(Ser_Type type)             { return SER_U8 <= (int) type && (int) type <= SER_F64; }
bool ser_type_is_integer(Ser_Type type)             { return SER_U8 <= (int) type && (int) type <= SER_I64; }
bool ser_type_is_signed_integer(Ser_Type type)      { return SER_I8 <= (int) type && (int) type <= SER_I64; }
bool ser_type_is_unsigned_integer(Ser_Type type)    { return SER_U8 <= (int) type && (int) type <= SER_U64; }
bool ser_type_is_float(Ser_Type type)               { return SER_F8 <= (int) type && (int) type <= SER_F64; }


#if defined(_MSC_VER)
    #define ATTRIBUTE_INLINE_ALWAYS                                 __forceinline
    #define ATTRIBUTE_INLINE_NEVER                                  __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_INLINE_ALWAYS                                 __attribute__((always_inline)) inline
    #define ATTRIBUTE_INLINE_NEVER                                  __attribute__((noinline))
#else
    #define ATTRIBUTE_INLINE_ALWAYS                             inline /* Ensures function will get inlined. Applied before function declartion. */
    #define ATTRIBUTE_INLINE_NEVER                              /* Ensures function will not get inlined. Applied before function declartion. */
#endif

ATTRIBUTE_INLINE_NEVER 
static void _ser_writer_realloc(Ser_Writer* w, isize size)
{
    isize new_capacity = w->capacity*3/2 + 8;
    if(new_capacity < size)
        new_capacity = size;

    w->data = (uint8_t*) realloc(w->data, new_capacity); 
}

void ser_write(Ser_Writer* w, const void* ptr, isize size)
{
    if(w->offset + size > w->capacity)
        _ser_writer_realloc(w, size);

    memcpy(w->data + w->offset, ptr, size);
    w->offset += size;
}

void ser_primitive(Ser_Writer* w, Ser_Type type, const void* ptr, isize size)
{
    struct Temp {
        uint8_t type;
        uint8_t values[16];          
    } temp;
    
    temp.type = (uint8_t) type;
    memcpy(temp.values, ptr, size);
    ser_write(w, &temp, 1 + sizeof size);
}

void ser_string(Ser_Writer* w, const void* ptr, isize size)
{
    if(size <= 0)
        ser_primitive(w, SER_STRING_0, NULL, 0);
    else
    {
        if(size >= 256) 
            ser_primitive(w, SER_STRING_64, &size, sizeof size);
        else {
            uint8_t _size = (uint8_t) size;
            ser_primitive(w, SER_STRING_8, &_size, sizeof _size);
        }

        ser_write(w, ptr, size);
        uint8_t null = 0;
        ser_write(w, &null, sizeof null);
    }
}

void ser_binary(Ser_Writer* w, const void* ptr, isize size)
{
    ser_primitive(w, SER_STRING_64, &size, sizeof size);
    ser_write(w, ptr, size);
}

void ser_custom_recovery(Ser_Writer* w, Ser_Type type, const void* ptr, isize size, const void* ptr2, isize size2)
{
    uint8_t null = 0;
    uint8_t usize = (uint8_t) size;
    ser_primitive(w, type, &usize, size);
    ser_write(w, ptr, size);
    ser_write(w, ptr2, size2);
}

void ser_custom_recovery_with_hash(Ser_Writer* w, Ser_Type type, const char* str)
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

void ser_null(Ser_Writer* w)           { ser_primitive(w, SER_I8, NULL, 0); }
void ser_bool(Ser_Writer* w, bool val) { ser_primitive(w, SER_I8,  &val, sizeof val); }

void ser_i8(Ser_Writer* w, i8 val)     { ser_primitive(w, SER_I8,  &val, sizeof val); }
void ser_i16(Ser_Writer* w, i16 val)   { ser_primitive(w, SER_I16, &val, sizeof val); }
void ser_i32(Ser_Writer* w, i32 val)   { ser_primitive(w, SER_I32, &val, sizeof val); }
void ser_i64(Ser_Writer* w, i64 val)   { ser_primitive(w, SER_I64, &val, sizeof val); }

void ser_u8(Ser_Writer* w, u8 val)     { ser_primitive(w, SER_U8,  &val, sizeof val); }
void ser_u16(Ser_Writer* w, u16 val)   { ser_primitive(w, SER_U16, &val, sizeof val); }
void ser_u32(Ser_Writer* w, u32 val)   { ser_primitive(w, SER_U32, &val, sizeof val); }
void ser_u64(Ser_Writer* w, u64 val)   { ser_primitive(w, SER_U64, &val, sizeof val); }

void ser_f32(Ser_Writer* w, f32 val)   { ser_primitive(w, SER_F32, &val, sizeof val); }
void ser_f64(Ser_Writer* w, f64 val)   { ser_primitive(w, SER_F64, &val, sizeof val); }

void ser_list_begin(Ser_Writer* w)     { ser_primitive(w, SER_LIST_BEGIN, NULL, 0); }
void ser_list_end(Ser_Writer* w)       { ser_primitive(w, SER_LIST_END, NULL, 0); }
void ser_object_begin(Ser_Writer* w)   { ser_primitive(w, SER_OBJECT_BEGIN, NULL, 0); }
void ser_object_end(Ser_Writer* w)     { ser_primitive(w, SER_OBJECT_END, NULL, 0); }

void ser_recovery_list_begin(Ser_Writer* w, const char* str)      { ser_custom_recovery_with_hash(w, SER_RECOVERY_LIST_BEGIN, str); }
void ser_recovery_list_end(Ser_Writer* w, const char* str)        { ser_custom_recovery_with_hash(w, SER_RECOVERY_LIST_END, str); }
void ser_recovery_object_begin(Ser_Writer* w, const char* str)    { ser_custom_recovery_with_hash(w, SER_RECOVERY_OBJECT_BEGIN, str); }
void ser_recovery_object_end(Ser_Writer* w, const char* str)      { ser_custom_recovery_with_hash(w, SER_RECOVERY_OBJECT_END, str); }

void ser_i32v2(Ser_Writer* w, const i32 vals[2]) { ser_primitive(w, SER_I32V2, &vals, sizeof vals); }
void ser_i32v3(Ser_Writer* w, const i32 vals[3]) { ser_primitive(w, SER_I32V3, &vals, sizeof vals); }
void ser_i32v4(Ser_Writer* w, const i32 vals[4]) { ser_primitive(w, SER_I32V4, &vals, sizeof vals); }

void ser_f32v2(Ser_Writer* w, const f32 vals[2]) { ser_primitive(w, SER_F32V2, &vals, sizeof vals); }
void ser_f32v3(Ser_Writer* w, const f32 vals[3]) { ser_primitive(w, SER_F32V3, &vals, sizeof vals); }
void ser_f32v4(Ser_Writer* w, const f32 vals[4]) { ser_primitive(w, SER_F32V4, &vals, sizeof vals); }

void ser_cstring(Ser_Writer* w, const char* ptr)            { ser_string(w, ptr, ptr ? strlen(ptr) : 0); }

//@TODO: inspect assembly. Perhaps can be more efficient if we use pointers instead of offsets?
ATTRIBUTE_INLINE_ALWAYS
static bool deser_read(Ser_Reader* w, void* ptr, isize size)
{
    if(w->offset + size > w->capacity)
        return false;

    memcpy(ptr, w->data + w->offset, size);
    w->offset += size;
    return true;
}

ATTRIBUTE_INLINE_ALWAYS
static bool deser_skip(Ser_Reader* w, isize size)
{
    if(w->offset + size > w->capacity)
        return false;

    w->offset += size;
    return true;
}

void deser_value(Ser_Reader* w, Ser_Value* out_val)
{
    Ser_Value out = {0};
    out.type = SER_ERROR;
    out.exact_type = SER_ERROR;
    out.offset = w->offset;
    out.depth = w->depth;

    uint8_t uncast_type = 0; 
    uint8_t ok = true;
    if(deser_read(w, &uncast_type, sizeof uncast_type))
    {
        Ser_Type type = (Ser_Type) uncast_type;
        out.exact_type = type;
        switch (type)
        {
            case SER_NULL: { out.type = SER_NULL; } break;
            case SER_BOOL: { ok = deser_read(w, &out.vbool, 1); out.type = type; } break;

            case SER_U8:  { ok = deser_read(w, &out.u64, 1); out.type = SER_I64; } break;
            case SER_U16: { ok = deser_read(w, &out.u64, 2); out.type = SER_I64; } break;
            case SER_U32: { ok = deser_read(w, &out.u64, 4); out.type = SER_I64; } break;
            case SER_U64: { ok = deser_read(w, &out.u64, 8); out.type = SER_I64; } break;
            
            case SER_I8:  { i8  val = 0; ok = deser_read(w, &val, 1); out.i64 = val; out.type = SER_I64; } break;
            case SER_I16: { i16 val = 0; ok = deser_read(w, &val, 2); out.i64 = val; out.type = SER_I64; } break;
            case SER_I32: { i32 val = 0; ok = deser_read(w, &val, 4); out.i64 = val; out.type = SER_I64; } break;
            case SER_I64: { i64 val = 0; ok = deser_read(w, &val, 8); out.i64 = val; out.type = SER_I64; } break;
            
            case SER_F8:  { u8  val = 0; ok = deser_read(w, &val, 1); out.u64 = val; out.type = SER_F8; } break;
            case SER_F16: { u16 val = 0; ok = deser_read(w, &val, 2); out.u64 = val; out.type = SER_F16; } break;
            case SER_F32: { f32 val = 0; ok = deser_read(w, &val, 4); out.f32 = val; out.type = SER_F32; } break;
            case SER_F64: { f64 val = 0; ok = deser_read(w, &val, 8); out.f64 = val; out.type = SER_F64; } break;

            case SER_F32V2: { ok = deser_read(w, &out.f32v4, 2*sizeof out.f32); out.type = type; } break;
            case SER_F32V3: { ok = deser_read(w, &out.f32v4, 3*sizeof out.f32); out.type = type; } break;
            case SER_F32V4: { ok = deser_read(w, &out.f32v4, 4*sizeof out.f32); out.type = type; } break;
            
            case SER_I32V2: { ok = deser_read(w, &out.i32v4, 8); out.type = type; } break;
            case SER_I32V3: { ok = deser_read(w, &out.i32v4, 12); out.type = type; } break;
            case SER_I32V4: { ok = deser_read(w, &out.i32v4, 16); out.type = type; } break;

            case SER_LIST_END:
            case SER_OBJECT_END:    { out.type = type; w->depth -= 1; } break;

            case SER_LIST_BEGIN:    
            case SER_OBJECT_BEGIN:  { out.type = type; w->depth += 1; } break;

            case SER_RECOVERY_LIST_END:
            case SER_RECOVERY_OBJECT_END:    
            case SER_RECOVERY_LIST_BEGIN:    
            case SER_RECOVERY_OBJECT_BEGIN:  { 
                uint8_t size = 0;
                ok &= deser_read(w, &size, sizeof size);
                out.string.data = (char*) (void*) (w->data + w->offset);
                out.string.count = size;
                ok &= deser_skip(w, out.string.count);
                out.type = type; 
                if(ok) {
                    if((uint32_t) type - SER_LIST_END < SER_DYN_COUNT)
                        w->depth -= 1; 
                    else
                        w->depth += 1; 
                }
            } break;

            case SER_STRING_0:  { 
                out.type = SER_STRING; 
                out.string.data = "";
                out.string.count = 0;
            } break;
            case SER_STRING_8:
            case SER_STRING_64:  { 
                uint8_t null = 0;
                uint8_t size = 0;
                out.type = SER_STRING;
                if(type == SER_STRING_64) 
                    ok &= deser_read(w, &out.string.count, sizeof out.string.count);
                else {
                    ok &= deser_read(w, &size, sizeof size);
                    out.string.count = size;
                }
                
                out.string.data = (char*) (void*) (w->data + w->offset);
                
                ok &= deser_skip(w, out.string.count);
                ok &= deser_read(w, &null, sizeof null);
                ok &= null == 0;
            } break;

            case SER_BINARY:  { 
                out.type = SER_BINARY;
                ok &= deser_read(w, &out.binary.count, sizeof out.binary.count);
                out.binary.data = (char*) (void*) (w->data + w->offset);
                ok &= deser_skip(w, out.binary.count);
            } break;
            
            default: {
                ok = false;
                //do error
            } break;
        }

    }

    if(ok == false) {
        out.type = SER_ERROR;
        w->offset = out.offset;
    }

    *out_val = out;
}

//@TODO: optimize
void deser_skip_to_depth(Ser_Reader* w, isize depth)
{
    Ser_Value val = {0};
    while(val.type != SER_ERROR && w->depth != depth)
        deser_value(w, &val);
}

static bool _deser_recover(const Ser_Value* object);

bool ser_type_is_ender(Ser_Type type)
{
    return (uint32_t) type - SER_LIST_END < SER_DYN_COUNT;
}

bool ser_type_is_ender_or_error(Ser_Type type)
{
    return (uint32_t) type - SER_LIST_END <= SER_DYN_COUNT;
}

bool deser_iterate_list(Ser_Value list, Ser_Value* out_val)
{
    ASSERT(list.type == SER_LIST || list.type == SER_RECOVERY_LIST);
    deser_skip_to_depth(list.w, list.depth);
    deser_value(list.w, out_val);
    if(ser_type_is_ender_or_error(out_val->type))
    {
        if(list.type != out_val->type - SER_DYN_COUNT)
            _deser_recover(&list);
        return false;
    }

    return true;
}


bool deser_iterate_object(Ser_Value object, Ser_Value* out_key, Ser_Value* out_val)
{
    ASSERT(object.type == SER_OBJECT || object.type == SER_RECOVERY_OBJECT);

    deser_skip_to_depth(object.w, object.depth);
    deser_value(object.w, out_key);
    if(ser_type_is_ender_or_error(out_key->type)) 
    {
        //if the ending type does not correspond to the object type
        if(object.type != out_key->type - SER_DYN_COUNT)
            goto recover;
        return false;
    }

    //NOTE: can be removed if we disallow dynamic as keys
    // then this case will just full under error.
    deser_skip_to_depth(object.w, object.depth); 
    deser_value(object.w, out_val);
    if(ser_type_is_ender_or_error(out_key->type))
        goto recover;

    return true;

    recover:
    _deser_recover(&object);
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

static bool _deser_recover(const Ser_Value*  object)
{
    Ser_Reader* reader = object->w;
    
    isize recovery_len = 1;
    char recovery_text[270];
    if(object->type == SER_LIST)
        recovery_text[0] = SER_LIST_END;
    else if(object->type == SER_OBJECT)
        recovery_text[0] = SER_OBJECT_END;
    else if(object->type == SER_RECOVERY_LIST || object->type == SER_RECOVERY_OBJECT)
    {
        isize i = 0;
        recovery_text[i++] = object->type == SER_RECOVERY_LIST ? SER_RECOVERY_LIST_END : SER_RECOVERY_OBJECT_END;
        recovery_text[i++] = (uint8_t) object->string.count;
        memcpy(recovery_text + 2, object->string.data, object->string.count); i += object->string.count + 2;
        recovery_text[i++] = '\0';
        recovery_len = i;
    }
    else
        ASSERT(false);

    Ser_String total = {(char*) reader->data, reader->capacity};
    Ser_String recovery = {recovery_text, recovery_len};
    isize recovered = _ser_find_first_or(total, recovery, reader->offset, -1);
    if(recovered != -1) {
        reader->offset = recovered;
        printf("recovered!\n");
    }

    return recovered != -1;
}

bool ser_string_eq(Ser_Value value, Ser_String str)
{
    return value.type == SER_STRING && value.string.count == str.count && memcmp(value.string.data, str.data, str.count) == 0;
}

bool deser_null(Ser_Value object)                    { return object.type == SER_NULL; }
bool deser_bool(Ser_Value object, bool* val)         { if(object.type == SER_BOOL)   { *val = object.vbool; return true; } return false; }
bool deser_binary(Ser_Value object, Ser_String* val) { if(object.type == SER_STRING) { *val = object.string; return true; } return false; }
bool deser_string(Ser_Value object, Ser_String* val) { if(object.type == SER_BINARY) { *val = object.binary; return true; } return false; }

#if 0
ATTRIBUTE_INLINE_ALWAYS
static bool _deser_i64_inline(Ser_Type type, i64 union_num, i64* val)
{
    union {
        i64 i64;
        f64 f64;
        f32 f32;
    } object = {union_num};
    
    if(type == SER_I64) {
        *val = object.i64;
        return true;
    }
    //slow path!
    bool state = false;
    i64 out = 0;
    if(type == SER_F64) {
        out = (i64) object.f64;
        state = out == object.f64;
    }    
    else if(type == SER_F32) {
        out = (i64) object.f32;
        state = out == object.f32;
    }

    if(state)
        *val = out;
    return state;
}

#define DEFINE_DESER_INT_TYPE(int, SER_TYPE, MIN, MAX)      \
    ATTRIBUTE_INLINE_NEVER static                           \
    bool _deser_##int##_slow(Ser_Type type, i64 union_num, int* val)\
    {                                                       \
        i64 out = 0;                                        \
        if(_deser_i64_inline(type, union_num, &out))    \
            if(MIN <= out && out <= MAX) {                  \
                *val = (int) out;                           \
                return true;                                \
            }                                               \
        return false;                                       \
    }                                                       \
    bool deser_##int(Ser_Value object, int* val)      \
    {                                                       \
        if(object.exact_type == SER_TYPE) {                 \
            *val = (int) object.i64;                        \
            return true;                                    \
        }                                                   \
        return _deser_##int##_slow(object.type, object.i64, val);        \
    }                                                       \

ATTRIBUTE_INLINE_NEVER
static bool _deser_i64_noinline(Ser_Type type, i64 union_num, i64* val)
{
    return _deser_i64_inline(type, union_num, val);
}
bool deser_i64(Ser_Value object, i64* val)
{
    if(object.type == SER_I64) {
        *val = object.i64;
        return true;
    }
    return _deser_i64_noinline(object.type, object.i64, val);
}

DEFINE_DESER_INT_TYPE(i32, SER_I32, INT32_MIN, INT32_MAX)
DEFINE_DESER_INT_TYPE(i16, SER_I16, INT16_MIN, INT16_MAX)
DEFINE_DESER_INT_TYPE(i8, SER_I8, INT8_MIN, INT8_MAX)

DEFINE_DESER_INT_TYPE(u64, SER_U64, 0, UINT64_MAX)
DEFINE_DESER_INT_TYPE(u32, SER_U32, 0, UINT32_MAX)
DEFINE_DESER_INT_TYPE(u16, SER_U16, 0, UINT16_MAX)
DEFINE_DESER_INT_TYPE(u8, SER_U8, 0, UINT8_MAX)


#if 0
bool deser_f64(Ser_Value object, f64* val)
{
    if(object.type == SER_F64) 
        *val = object.f64;
    else if(object.type == SER_F32) 
        *val = object.f32;
    else if(object.type == SER_I64) {
        if(object.exact_type == SER_U64)
            *val = object.u64;
        else
            *val = object.i64;
    }
    else 
        return false;

    return true;
}

bool deser_f32(Ser_Value object, f32* val)
{
    if(object.type == SER_F32) 
        *val = object.f32;
    else if(object.type == SER_F64) 
        *val = object.f64;
    else if(object.type == SER_I64) {
        if(object.exact_type == SER_U64)
            *val = object.u64;
        else
            *val = object.i64;
    }
    else 
        return false;

    return true;
}
#else
typedef union {
    i64 i64;
    u64 u64;
    f64 f64;
    f32 f32;
} _Ser_Numeric_Val;

ATTRIBUTE_INLINE_NEVER
static bool _deser_f64_slow(Ser_Type type, Ser_Type exact_type, i64 val64, f64* val)
{
    _Ser_Numeric_Val object = {val64};
    if(type == SER_F32) 
        *val = object.f32;
    else if(type == SER_I64) {
        if(exact_type == SER_U64)
            *val = object.u64;
        else
            *val = object.i64;
    }
    else 
        return false;

    return true;
}

ATTRIBUTE_INLINE_NEVER
static bool _deser_f32_slow(Ser_Type type, Ser_Type exact_type, i64 val64, f32* val)
{
    _Ser_Numeric_Val object = {val64};
    if(type == SER_F64) 
        *val = (f32) object.f64;
    else if(type == SER_I64) {
        if(exact_type == SER_U64)
            *val = object.u64;
        else
            *val = object.i64;
    }
    else 
        return false;

    return true;
}

bool deser_f64(Ser_Value object, f64* val)
{
    if(object.type == SER_F64) {
        *val = object.f64;
        return true;
    }
    return _deser_f64_slow(object.type, object.exact_type, object.i64, val);
}

bool deser_f32(Ser_Value object, f32* val)
{
    if(object.type == SER_F32) {
        *val = object.f32;
        return true;
    }
    return _deser_f32_slow(object.type, object.exact_type, object.i64, val);
}

#endif
#else

ATTRIBUTE_INLINE_NEVER
static bool _deser_generic_convert(Ser_Type type, u64 generic_num, Ser_Type target_type, void* out)
{
    union {
        u64 u64;
        i64 i64;
        f64 f64;
        f32 f32;
    } object = {generic_num};
    
    bool state = true;
    if(target_type == SER_F32) {
        switch(type) {
            case SER_F64: *(f32*)out = object.f64; break;
            case SER_F32: *(f32*)out = object.f32; break;
            case SER_I64: *(f32*)out = object.i64; break;
            case SER_U64: *(f32*)out = object.u64; break;
            default: state = false; break;
        }
    }
    else if(target_type == SER_F64) {
        switch(type) {
            case SER_F64: *(f64*)out = object.f64; break;
            case SER_F32: *(f64*)out = object.f32; break;
            case SER_I64: *(f64*)out = object.i64; break;
            case SER_U64: *(f64*)out = object.u64; break;
            default: state = false; break;
        }
    }
    else if(target_type == SER_U64 && type == SER_U64) {
        *(u64*) out = object.u64;
    }
    else
    {
        i64 val = 0;
        if(type == SER_I64) 
            val = object.i64;
        else if(type == SER_U64) {
            val = (i64) object.u64;
            state = object.u64 <= INT64_MAX;
        } 
        else if(type == SER_F64) {
            val = (i64) object.f64;
            state = val == object.f64;
        }    
        else if(type == SER_F32) {
            val = (i64) object.f32;
            state = val == object.f32;
        }
        else
            state = false;

        if(state) {
            switch(target_type) {
                case SER_I64: state = INT64_MIN <= val && val <= INT64_MAX; if(state) *(i64*) out = val; break; 
                case SER_I32: state = INT32_MIN <= val && val <= INT32_MAX; if(state) *(i32*) out = val; break; 
                case SER_I16: state = INT16_MIN <= val && val <= INT16_MAX; if(state) *(i16*) out = val; break; 
                case SER_I8:  state = INT8_MIN  <= val && val <= INT8_MAX;  if(state) *(i8*)  out = val; break; 
                
                case SER_U64: state = 0 <= val && val <= UINT64_MAX; if(state) *(u64*) out = val; break; 
                case SER_U32: state = 0 <= val && val <= UINT32_MAX; if(state) *(u32*) out = val; break; 
                case SER_U16: state = 0 <= val && val <= UINT16_MAX; if(state) *(u16*) out = val; break; 
                case SER_U8:  state = 0 <= val && val <= UINT8_MAX;  if(state) *(u8*)  out = val; break; 

                default: state = false; break;
            }
        }
    }
    return state;
}

bool deser_i64(Ser_Value val, i64* out) { if(val.exact_type == SER_I64) {*out = (i64) val.i64; return true;} return _deser_generic_convert(val.type, val.generic, SER_I64, out); }
bool deser_i32(Ser_Value val, i32* out) { if(val.exact_type == SER_I32) {*out = (i32) val.i64; return true;} return _deser_generic_convert(val.type, val.generic, SER_I32, out); }
bool deser_i16(Ser_Value val, i16* out) { if(val.exact_type == SER_I16) {*out = (i16) val.i64; return true;} return _deser_generic_convert(val.type, val.generic, SER_I16, out); }
bool deser_i8(Ser_Value val, i8* out)   { if(val.exact_type == SER_I8)  {*out = (i8) val.i64; return true;}  return _deser_generic_convert(val.type, val.generic, SER_I8, out); }

bool deser_u64(Ser_Value val, u64* out) { if(val.exact_type == SER_U64) {*out = (u64) val.u64; return true;} return _deser_generic_convert(val.type, val.generic, SER_U64, out); }
bool deser_u32(Ser_Value val, u32* out) { if(val.exact_type == SER_U32) {*out = (u32) val.u64; return true;} return _deser_generic_convert(val.type, val.generic, SER_U32, out); }
bool deser_u16(Ser_Value val, u16* out) { if(val.exact_type == SER_U16) {*out = (u16) val.u64; return true;} return _deser_generic_convert(val.type, val.generic, SER_U16, out); }
bool deser_u8(Ser_Value val, u8* out)   { if(val.exact_type == SER_U8)  {*out = (u8) val.u64; return true;}  return _deser_generic_convert(val.type, val.generic, SER_U8, out); }

bool deser_f64(Ser_Value val, f64* out) { if(val.exact_type == SER_F64) {*out = (f64) val.f64; return true;} return _deser_generic_convert(val.type, val.generic, SER_F64, out); }
bool deser_f32(Ser_Value val, f32* out) { if(val.exact_type == SER_F32) {*out = (f32) val.f32; return true;} return _deser_generic_convert(val.type, val.generic, SER_F32, out); }

#endif

bool deser_f32v3(Ser_Value object, float out[3])
{
    if(object.type == SER_F32V3 || object.type == SER_F32V4)
    {
        out[0] = object.f32v4[0];
        out[1] = object.f32v4[1];
        out[2] = object.f32v4[2]; 
        return true;
    }
    else if(object.type == SER_LIST)
    {
        int count = 0;
        for(Ser_Value val = {0}; deser_iterate_list(object, &val); ) {
            count += deser_f32(val, &out[count]);
            if(count >= 3)
                break;
        }

        return count >= 3;
    }
    else if(object.type == SER_OBJECT)
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


void _ser_f32v3(Ser_Writer* w, const f32 vals[3]) { 
    uint8_t data[32];
    int i = 0, c = 0;
    data[i++] = SER_LIST_BEGIN;
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(f32)); i += sizeof(f32);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(f32)); i += sizeof(f32);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(f32)); i += sizeof(f32);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(f32)); i += sizeof(f32);
    data[i++] = SER_LIST_END;

    ser_write(w, data, i);
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

    i32 channels_count; //the number of channels this texture should have. Is in range [0, MAX_CHANNELS] 
    i32 channels_indices1[MAX_CHANNELS]; //One based indices into the image channels. 

    Map_Scale_Filter filter_minify;
    Map_Scale_Filter filter_magnify;
    Map_Repeat repeat_u;
    Map_Repeat repeat_v;
    Map_Repeat repeat_w;

    f32 gamma;          //default 2.2
    f32 brightness;     //default 0
    f32 contrast;       //default 0
} Map_Info;

bool deser_map_repeat(Ser_Value val, Map_Repeat* repeat)
{
    if(0) {}
    else if(ser_cstring_eq(val, "repeat"))          *repeat = MAP_REPEAT_REPEAT;
    else if(ser_cstring_eq(val, "mirrored"))        *repeat = MAP_REPEAT_MIRRORED_REPEAT;
    else if(ser_cstring_eq(val, "clamp_to_edge"))   *repeat = MAP_REPEAT_CLAMP_TO_EDGE;
    else if(ser_cstring_eq(val, "clamp_to_border")) *repeat = MAP_REPEAT_CLAMP_TO_BORDER;
    else return false; //log here
    return true;
}

bool deser_map_scale_filter(Ser_Value val, Map_Scale_Filter* filter)
{
    if(0) {}
    else if(ser_cstring_eq(val, "bilinear")) *filter = MAP_SCALE_FILTER_BILINEAR;
    else if(ser_cstring_eq(val, "trilinear"))*filter = MAP_SCALE_FILTER_TRILINEAR;
    else if(ser_cstring_eq(val, "nearest"))  *filter = MAP_SCALE_FILTER_NEAREST;
    else return false; //log here
    return true;
}

bool deser_map_info(Ser_Value object, Map_Info* out_map_info)
{
    Map_Info out = {0};
    Vec3 scale = {1,1,1};
    out.scale = scale;
    out.gamma = 2.2f;

    if(object.type != SER_OBJECT) 
        return false;

    for(Ser_Value key = {0}, val = {0}; deser_iterate_object(object, &key, &val); )
    {
        /**/ if(ser_cstring_eq(key, "offset"))          deser_f32v3(val, out.offset.floats);
        else if(ser_cstring_eq(key, "scale"))           deser_f32v3(val, out.scale.floats);
        else if(ser_cstring_eq(key, "resolution"))      deser_f32v3(val, out.resolution.floats);
        else if(ser_cstring_eq(key, "filter_minify"))   deser_map_scale_filter(val, &out.filter_minify);
        else if(ser_cstring_eq(key, "filter_magnify"))  deser_map_scale_filter(val, &out.filter_magnify);
        else if(ser_cstring_eq(key, "repeat_u"))        deser_map_repeat(val, &out.repeat_u); //log here
        else if(ser_cstring_eq(key, "repeat_v"))        deser_map_repeat(val, &out.repeat_v);
        else if(ser_cstring_eq(key, "repeat_w"))        deser_map_repeat(val, &out.repeat_w);
        else if(ser_cstring_eq(key, "gamma"))           deser_f32(val, &out.gamma);
        else if(ser_cstring_eq(key, "brightness"))      deser_f32(val, &out.brightness);
        else if(ser_cstring_eq(key, "contrast"))        deser_f32(val, &out.contrast);
        else if(ser_cstring_eq(key, "channels_count"))  deser_i32(val, &out.channels_count);
        else if(ser_cstring_eq(key, "channels_indices1"))
        {
            if(val.type == SER_LIST_BEGIN)
            {
                int i = 0;
                for(Ser_Value item = {0}; deser_iterate_list(val, &item); )
                    i += deser_i32(item, &out.channels_indices1[i]); //log here
            }
        }
    }

    *out_map_info = out;
    return true;
}


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

bool ser_map_scale_filter(Ser_Writer* w, Map_Scale_Filter filter)
{
    switch(filter)
    {
        case MAP_SCALE_FILTER_BILINEAR:     ser_cstring(w, "bilinear"); break;
        case MAP_SCALE_FILTER_TRILINEAR:    ser_cstring(w, "trilinear"); break;
        case MAP_SCALE_FILTER_NEAREST:      ser_cstring(w, "nearest"); break; 
        default:                            ser_cstring(w, "invalid"); break; 
    }
}

bool ser_map_info(Ser_Writer* w, Map_Info info)
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

