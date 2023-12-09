#ifndef JOT_SERIALIZE
#define JOT_SERIALIZE

// This file allows for simple and composable serialization/deserialization with minimum boilerplate.
// The main idea is to split writing and reading of files into two different parts:
//    1: Writing to LPF (or any other) file representation
//    2: Converting this file into final (text or binary) form 
//
// This distinction allows for reading and writing to be symetric: both are just reading/writing from
// a container. We can exploit this symetry by always pairing read and write functions together and
// selecting the appropiate one during runtime. This most of the time reduces size of the neeeded code
// by a factor of two. (See resources.h for example of high level and elegant use)
//
// Further the task of reading/writing is split into two more categories:
//    1: Locating of the entry - this means finding while reading or creating while writing of the appropriate entry
//    2: The actual reading/writing on the located entry
// This further separates the concept of retrieval/creation from the actual parsing and lead to very composable code.
// For code simplicity we allow the Lpf_Dyn_Entry* entry to be NULL (this is the case when localization fails). 
// In that case the serialize function fails.
// 
// Lastly we supply each function with 'def' default value to be used when the localization or reading would fail. 
// This is purely for conveniance and could be implemented caller side by:
// 
// if(serialize_(serialize_locate(entry, "my_val", action), &my_val, action) == false && action == READ)
//    my_val = def;
//
// @TODO: make base64 write directly into entry!

#include "format_lpf.h"
#include "parse.h"
#include "vformat.h"
#include "math.h"
#include "base64.h"
#include "guid.h"

typedef enum Read_Or_Write {
    SERIALIZE_READ = 0,
    SERIALIZE_WRITE = 1,
} Read_Or_Write;

//Used to serialize enums. 
//For example of usage see implementation of serialize_bool
typedef struct Serialize_Enum {
    String name;
    i64 value;
} Serialize_Enum;

#define SERIALIZE_ENUM_VALUE(ENUM_VALUE) BRACE_INIT(Serialize_Enum){STRING(#ENUM_VALUE), ENUM_VALUE}

EXPORT void base64_encode_append_into(String_Builder* into, const void* data, isize len, Base64_Encoding encoding);
EXPORT bool base64_decode_append_into(String_Builder* into, const void* data, isize len, Base64_Decoding decoding);
EXPORT void base64_encode_into(String_Builder* into, const void* data, isize len, Base64_Encoding encoding);
EXPORT bool base64_decode_into(String_Builder* into, const void* data, isize len, Base64_Decoding decoding);

//Attempts to locate an entry within children of into and return pointer to it. 
//If it cannot find it: returns NULL if action is read or creates it if action is write.
EXPORT Lpf_Dyn_Entry* serialize_locate_any(Lpf_Dyn_Entry* into, Lpf_Kind create_kind, Lpf_Kind kind, String label, String type, Read_Or_Write action);
EXPORT Lpf_Dyn_Entry* serialize_locate(Lpf_Dyn_Entry* into, const char* label, Read_Or_Write action);
EXPORT void           serialize_entry_set_value(Lpf_Dyn_Entry* entry, String type, String value);
EXPORT void           serialize_entry_set_identity(Lpf_Dyn_Entry* entry, String type, String value, Lpf_Kind kind, u16 format_flags);

//Explicit read/write interface. 
//This is mostly important for things requiring string or string builder 
//(often we have data in format compatible with string but not within string builder.
// This is the case for images, vertices etc.)
EXPORT bool serialize_write_raw(Lpf_Dyn_Entry* entry, String val, String type);
EXPORT bool serialize_read_raw(Lpf_Dyn_Entry* entry, String_Builder* val, String def);

EXPORT bool serialize_write_base64(Lpf_Dyn_Entry* entry, String val, String type);
EXPORT bool serialize_read_base64(Lpf_Dyn_Entry* entry, String_Builder* val, String def);

EXPORT bool serialize_write_string(Lpf_Dyn_Entry* entry, String val, String type);
EXPORT bool serialize_read_string(Lpf_Dyn_Entry* entry, String_Builder* val, String def);

EXPORT bool serialize_write_name(Lpf_Dyn_Entry* entry, String val, String type);
EXPORT bool serialize_read_name(Lpf_Dyn_Entry* entry, String_Builder* val, String def);

//Serialize interface
EXPORT bool serialize_scope(Lpf_Dyn_Entry* entry, String type, u16 format_flags, Read_Or_Write action);
EXPORT bool serialize_blank(Lpf_Dyn_Entry* entry, isize count, Read_Or_Write action);
EXPORT bool serialize_comment(Lpf_Dyn_Entry* entry, String comment, Read_Or_Write action);

EXPORT bool serialize_raw_typed(Lpf_Dyn_Entry* entry, String_Builder* val, String def, String type, Read_Or_Write action);
EXPORT bool serialize_string_typed(Lpf_Dyn_Entry* entry, String_Builder* val, String def, String type, Read_Or_Write action);
EXPORT bool serialize_base64_typed(Lpf_Dyn_Entry* entry, String_Builder* val, String def, String type, Read_Or_Write action);
EXPORT bool serialize_int_typed(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, i64 def, String type, Read_Or_Write action);
EXPORT bool serialize_uint_typed(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, u64 def, String type, Read_Or_Write action);
EXPORT bool serialize_float_typed(Lpf_Dyn_Entry* entry, void* float_value, isize float_type_size, void* def, String type, Read_Or_Write action);
EXPORT bool serialize_int_count_typed(Lpf_Dyn_Entry* entry, void* value, isize value_type_size, const void* defs, isize count, String type, Read_Or_Write action);
EXPORT bool serialize_enum_typed(Lpf_Dyn_Entry* entry, void* enum_value, isize enum_type_size, i64 def, String type, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action);

EXPORT bool serialize_int(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, i64 def, Read_Or_Write action);
EXPORT bool serialize_uint(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, u64 def, Read_Or_Write action);
EXPORT bool serialize_float(Lpf_Dyn_Entry* entry, void* float_value, isize float_type_size, void* def, Read_Or_Write action);
EXPORT bool serialize_enum(Lpf_Dyn_Entry* entry, void* enum_value, isize enum_type_size, i64 def, const char* type, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action);

EXPORT bool serialize_bool(Lpf_Dyn_Entry* entry, bool* val, bool def, Read_Or_Write action);
EXPORT bool serialize_id(Lpf_Dyn_Entry* entry, Id* val, Id def, Read_Or_Write action);

EXPORT bool serialize_i64(Lpf_Dyn_Entry* entry, i64* val, i64 def, Read_Or_Write action);
EXPORT bool serialize_i32(Lpf_Dyn_Entry* entry, i32* val, i32 def, Read_Or_Write action);
EXPORT bool serialize_i16(Lpf_Dyn_Entry* entry, i16* val, i16 def, Read_Or_Write action);
EXPORT bool serialize_i8(Lpf_Dyn_Entry* entry, i8* val, i8 def, Read_Or_Write action);

EXPORT bool serialize_u64(Lpf_Dyn_Entry* entry, u64* val, u64 def, Read_Or_Write action);
EXPORT bool serialize_u32(Lpf_Dyn_Entry* entry, u32* val, u32 def, Read_Or_Write action);
EXPORT bool serialize_u16(Lpf_Dyn_Entry* entry, u16* val, u16 def, Read_Or_Write action);
EXPORT bool serialize_u8(Lpf_Dyn_Entry* entry, u8* val, u8 def, Read_Or_Write action);

EXPORT bool serialize_f64(Lpf_Dyn_Entry* entry, f64* val, f64 def, Read_Or_Write action);
EXPORT bool serialize_f32(Lpf_Dyn_Entry* entry, f32* val, f32 def, Read_Or_Write action);

EXPORT bool serialize_string(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_name(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_raw(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_base64(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action);

EXPORT bool serialize_vec2(Lpf_Dyn_Entry* entry, Vec2* val, Vec2 def, Read_Or_Write action);
EXPORT bool serialize_vec3(Lpf_Dyn_Entry* entry, Vec3* val, Vec3 def, Read_Or_Write action);
EXPORT bool serialize_vec4(Lpf_Dyn_Entry* entry, Vec4* val, Vec4 def, Read_Or_Write action);

EXPORT bool serialize_mat2(Lpf_Dyn_Entry* entry, Mat2* val, Mat2 def, Read_Or_Write action);
EXPORT bool serialize_mat3(Lpf_Dyn_Entry* entry, Mat3* val, Mat3 def, Read_Or_Write action);
EXPORT bool serialize_mat4(Lpf_Dyn_Entry* entry, Mat4* val, Mat4 def, Read_Or_Write action);

EXPORT bool serialize_quat(Lpf_Dyn_Entry* entry, Quat* val, Quat def, Read_Or_Write action);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_SERIALIZE_IMPL)) && !defined(JOT_SERIALIZE_HAS_IMPL)
#define JOT_SERIALIZE_HAS_IMPL



EXPORT void base64_encode_append_into(String_Builder* into, const void* data, isize len, Base64_Encoding encoding)
{
    isize size_before = into->size;
    isize needed = base64_encode_max_output_length(len);
    array_resize(into, size_before + needed);

    isize actual_size = base64_encode(into->data + size_before, data, len, encoding);
    array_resize(into, size_before + actual_size);
}

EXPORT bool base64_decode_append_into(String_Builder* into, const void* data, isize len, Base64_Decoding decoding)
{
    isize size_before = into->size;
    isize needed = base64_decode_max_output_length(len);
    array_resize(into, size_before + needed);
    
    isize error_at = 0;
    isize actual_size = base64_decode(into->data + size_before, data, len, decoding, &error_at);
    if(error_at == -1)
    {
        array_resize(into, size_before + actual_size);
        return true;
    }
    else
    {
        array_resize(into, size_before);
        return false;
    }
}

EXPORT void base64_encode_into(String_Builder* into, const void* data, isize len, Base64_Encoding encoding)
{
    array_clear(into);
    base64_encode_append_into(into, data, len, encoding);
}

EXPORT bool base64_decode_into(String_Builder* into, const void* data, isize len, Base64_Decoding decoding)
{
    array_clear(into);
    return base64_decode_append_into(into, data, len, decoding);
}


EXPORT Lpf_Dyn_Entry* serialize_locate_any(Lpf_Dyn_Entry* into, Lpf_Kind create_kind, Lpf_Kind kind, String label, String type, Read_Or_Write action)
{
    if(into == NULL)
        return NULL;

    isize found_i = lpf_find_index(*into, kind, label, type, 0);
    if(found_i != -1)
        return &into->children[found_i];

    if(action == SERIALIZE_READ)
    {
        return NULL;
    }
    else
    {
    
        Lpf_Entry entry = {create_kind};
        entry.label = label;
        entry.type = type;
        lpf_dyn_entry_push(into, entry);

        Lpf_Dyn_Entry* created = &into->children[into->children_size - 1];
        return created;
    }
}

EXPORT Lpf_Dyn_Entry* serialize_locate(Lpf_Dyn_Entry* into, const char* label, Read_Or_Write action)
{
    return serialize_locate_any(into, LPF_KIND_ENTRY, (Lpf_Kind) (-1), string_make(label), STRING(""), action);
}

EXPORT Lpf_Dyn_Entry* lpf_dyn_entry_add(Lpf_Dyn_Entry* into, Lpf_Kind kind, const char* label)
{
    Lpf_Entry entry = {kind};
    entry.label = string_make(label);
    lpf_dyn_entry_push(into, entry);

    Lpf_Dyn_Entry* created = &into->children[into->children_size - 1];
    return created;
}

EXPORT Lpf_Dyn_Entry* lpf_dyn_value_add(Lpf_Dyn_Entry* into, const char* label)
{
    return lpf_dyn_entry_add(into, LPF_KIND_ENTRY, label);
}

EXPORT void serialize_entry_set_value(Lpf_Dyn_Entry* entry, String type, String value)
{
    Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
    readable.value = value;
    readable.type = type;

    lpf_dyn_entry_from_entry(entry, readable);
}

EXPORT void serialize_entry_set_identity(Lpf_Dyn_Entry* entry, String type, String value, Lpf_Kind kind, u16 format_flags)
{
    serialize_entry_set_value(entry, type, value);
    entry->kind = kind;
    entry->format_flags |= format_flags;
}
EXPORT bool serialize_write_raw(Lpf_Dyn_Entry* entry, String val, String type)
{
    if(entry == NULL)
        return false;
        
    serialize_entry_set_identity(entry, type, val, LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_SENSITIVE);
    return true;
}
EXPORT bool serialize_read_raw(Lpf_Dyn_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }
        
    Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
    builder_assign(val, readable.value);
    return true;
}

EXPORT bool serialize_write_string(Lpf_Dyn_Entry* entry, String val, String type)
{
    if(entry == NULL)
        return false;
        
    serialize_entry_set_identity(entry, type, val, LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_POSTFIX_AGNOSTIC | LPF_FLAG_WHITESPACE_PREFIX_AGNOSTIC);
    return true;
}

EXPORT bool serialize_read_string(Lpf_Dyn_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }
        
    Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
    String trimmed = string_trim_whitespace(readable.value);
    builder_assign(val, trimmed);
    return true;
}


EXPORT bool serialize_write_name(Lpf_Dyn_Entry* entry, String val, String type)
{
    if(entry == NULL)
        return false;
        
    serialize_entry_set_identity(entry, type, val, LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
    return true;

}
EXPORT bool serialize_read_name(Lpf_Dyn_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }
        
    Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
    String trimmed = string_trim_whitespace(readable.value);

    isize name_end = 0;
    bool state = match_name(trimmed, &name_end);
    if(state)
    {
        String name = string_head(trimmed, name_end);
        builder_assign(val, name);
    }
    return state;
}

EXPORT bool serialize_write_base64(Lpf_Dyn_Entry* entry, String val, String type)
{
    //@TODO: this can be done directly int entry string since the size is exact!
    //       implement entry_set_texts_capacity or something similar
    String_Builder encoded = {0};
    array_init_backed(&encoded, allocator_get_scratch(), 256);

    isize max_size = base64_encode_max_output_length(val.size);
    array_resize(&encoded, max_size); 
        
    isize actual_size = base64_encode(encoded.data, val.data, val.size, base64_encoding_url());
    array_resize(&encoded, actual_size); 

    serialize_entry_set_identity(entry, type, string_from_builder(encoded), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
    array_deinit(&encoded);

    return true;
}

EXPORT bool serialize_read_base64(Lpf_Dyn_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }

    Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
    String trimmed = string_trim_whitespace(readable.value);
    isize max_size = base64_decode_max_output_length(trimmed.size);
    array_resize(val, max_size); 
        
    isize actual_size = base64_decode(val->data, trimmed.data, trimmed.size, base64_decoding_universal(), NULL);
    array_resize(val, actual_size); 
    return true;
}

EXPORT bool serialize_scope(Lpf_Dyn_Entry* entry, String type, u16 format_flags, Read_Or_Write action)
{
    if(entry == NULL)
        return false;
        
    if(action == SERIALIZE_WRITE)
    {
        serialize_entry_set_identity(entry, type, STRING(""), LPF_KIND_SCOPE_START, format_flags);
        return true;
    }
    else
    {
        return entry->kind == LPF_KIND_SCOPE_START;
    }
}

EXPORT bool serialize_blank(Lpf_Dyn_Entry* entry, isize count, Read_Or_Write action)
{
    if(entry == NULL)
        return false;
        
    if(action == SERIALIZE_WRITE)
    {
        for(isize i = 0; i < count; i++)
        {
            Lpf_Entry added = {LPF_KIND_BLANK};
            lpf_dyn_entry_push(entry, added);
        }
    }

    return true;
}
EXPORT bool serialize_comment(Lpf_Dyn_Entry* entry, String comment, Read_Or_Write action)
{
    if(entry == NULL)
        return false;
        
    if(action == SERIALIZE_WRITE)
    {
        Lpf_Entry added = {LPF_KIND_COMMENT};
        added.comment = comment;
        lpf_dyn_entry_push(entry, added);
    }

    return true;
}

EXPORT bool serialize_raw_typed(Lpf_Dyn_Entry* entry, String_Builder* val, String def, String type, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_raw(entry, val, def);
    else
        return serialize_write_raw(entry, string_from_builder(*val), type);
}

EXPORT bool serialize_string_typed(Lpf_Dyn_Entry* entry, String_Builder* val, String def, String type, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_string(entry, val, def);
    else
        return serialize_write_string(entry, string_from_builder(*val), type);
}

EXPORT bool serialize_base64_typed(Lpf_Dyn_Entry* entry, String_Builder* val, String def, String type, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_base64(entry, val, def);
    else
        return serialize_write_base64(entry, string_from_builder(*val), type);
}

void set_variable_sized_int(void* integer, isize integer_type_size, i64 value)
{
    switch(integer_type_size)
    {
        case 1: *(i8*) (integer) = (i8) value; return;
        case 2: *(i16*) (integer) = (i16) value; return;
        case 4: *(i32*) (integer) = (i32) value; return;
        case 8: *(i64*) (integer) = (i64) value; return;
        default: ASSERT_MSG(false, "Strange type size submitted!");
    }
}

i64 get_variable_sized_int(void* integer, isize integer_type_size)
{
    switch(integer_type_size)
    {
        case 1: return *(i8*) (integer);
        case 2: return *(i16*) (integer);
        case 4: return *(i32*) (integer);
        case 8: return *(i64*) (integer);
        default: ASSERT_MSG(false, "Strange type size submitted!"); return 0;
    }
}

EXPORT bool serialize_int_count_typed(Lpf_Dyn_Entry* entry, void* value, isize value_type_size, const void* defs, isize count, String type, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);

        isize index = 0;
        bool state = true;

        for(isize i = 0; i < count; i ++)
        {
            match_whitespace(readable.value, &index);
            i64 concrete_value = 0;
            state = match_decimal_i64(readable.value, &index, &concrete_value);
            if(state)
                set_variable_sized_int((u8*) value + i*value_type_size, value_type_size, concrete_value);
            else 
                break;
        }

        if(state == false)
            memmove(value, defs, value_type_size*count);

        return state;
    }
    else
    {
        if(count == 1)
        {
            i64 concrete_value = get_variable_sized_int(value, value_type_size);
            serialize_entry_set_identity(entry, type, format_ephemeral("%lli", concrete_value), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        }
        else
        {
            String_Builder formatted = {0};
            array_init_backed(&formatted, allocator_get_scratch(), 256);

            for(isize i = 0; i < count; i ++)
            {
                if(i != 0)
                    array_push(&formatted, ' ');
                    
                i64 concrete_value = get_variable_sized_int((u8*) value + i*value_type_size, value_type_size);
                format_append_into(&formatted, "%lli", concrete_value);
            }
            array_deinit(&formatted);
        }

        return true;
    }
}

EXPORT bool serialize_int_typed(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, i64 def, String type, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    i64 value = 0;
    if(action == SERIALIZE_READ)
    {
        isize index = 0;
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        String trimmed = string_trim_prefix_whitespace(readable.value);
        bool state = match_decimal_i64(trimmed, &index, &value);
        if(state == false)
            value = def;
        set_variable_sized_int(int_value, int_type_size, value);
        return state;
    }
    else
    {
        value = get_variable_sized_int(int_value, int_type_size);
        serialize_entry_set_identity(entry, type, format_ephemeral("%lli", value), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        return true;
    }
}

EXPORT bool serialize_uint_typed(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, u64 def, String type, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    u64 value = 0;
    if(action == SERIALIZE_READ)
    {
        isize index = 0;
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        String trimmed = string_trim_prefix_whitespace(readable.value);
        bool state = match_decimal_u64(trimmed, &index, &value);
        if(state == false)
            value = def;
        set_variable_sized_int(int_value, int_type_size, (i64) value);
        return state;
    }
    else
    {
        value = (u64) get_variable_sized_int(int_value, int_type_size);
        serialize_entry_set_identity(entry, type, format_ephemeral("%llu", value), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        return true;
    }
}

EXPORT bool serialize_float_typed(Lpf_Dyn_Entry* entry, void* float_value, isize float_type_size, void* def, String type, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    ASSERT_MSG(float_type_size == 4 || float_type_size == 8, "Invalid floating point format!");
    if(action == SERIALIZE_READ)
    {
        isize index = 0;
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        String trimmed = string_trim_prefix_whitespace(readable.value);

        bool state = true;
        if(float_type_size == 8)
        {
            f64 value = 0;
            state = match_decimal_f64(trimmed, &index, &value);
            if(state == false)
                value = *(f64*) def;;

            *(f64*) float_value = value;
        }
        else
        {
            f32 value = 0;
            state = match_decimal_f32(trimmed, &index, &value);
            if(state == false)
                value = *(f32*) def;;

            *(f32*) float_value = value;
        }

        return state;
    }
    else
    {
        if(float_type_size == 8)
            serialize_entry_set_identity(entry, type, format_ephemeral("%lf", *(f64*) float_value), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        else
            serialize_entry_set_identity(entry, type, format_ephemeral("%f", *(f32*) float_value), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
            
        return true;
    }
}

EXPORT bool serialize_enum_typed(Lpf_Dyn_Entry* entry, void* enum_value, isize enum_type_size, i64 def, String type, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        String just_value = string_trim_whitespace(readable.value);

        for(isize i = 0; i < enums_size; i++)
        {
            Serialize_Enum enum_entry = enums[i];
            if(string_is_equal(just_value, enum_entry.name))
            {
                set_variable_sized_int(enum_value, enum_type_size, enum_entry.value);
                return true;
            }
        }
        
        set_variable_sized_int(enum_value, enum_type_size, def);
        return false;
    }
    else
    {
        isize enum_value_int = get_variable_sized_int(enum_value, enum_type_size);
        for(isize i = 0; i < enums_size; i++)
        {
            Serialize_Enum enum_entry = enums[i];
            if(enum_value_int == enum_entry.value)
            {
                serialize_entry_set_identity(entry, type, enum_entry.name, LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
                return true;
            }
        }

        return false;
    }
}

EXPORT bool serialize_enum(Lpf_Dyn_Entry* entry, void* enum_value, isize enum_type_size, i64 def, const char* type, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action)
{
    return serialize_enum_typed(entry, enum_value, enum_type_size, def, string_make(type), enums, enums_size, action);
}

EXPORT bool serialize_bool(Lpf_Dyn_Entry* entry, bool* val, bool def, Read_Or_Write action)
{
    Serialize_Enum enums[] = {
        SERIALIZE_ENUM_VALUE(false),
        SERIALIZE_ENUM_VALUE(true),
        SERIALIZE_ENUM_VALUE(0),
        SERIALIZE_ENUM_VALUE(1),
    };

    return serialize_enum_typed(entry, val, sizeof *val, (isize) def, STRING("b"), enums, STATIC_ARRAY_SIZE(enums), action);
}

EXPORT bool serialize_int(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, i64 def, Read_Or_Write action)
{
    return serialize_int_typed(entry, int_value, int_type_size, def, STRING("i"), action);
}

EXPORT bool serialize_uint(Lpf_Dyn_Entry* entry, void* int_value, isize int_type_size, u64 def, Read_Or_Write action)
{
    return serialize_uint_typed(entry, int_value, int_type_size, def, STRING("u"), action);
}

EXPORT bool serialize_float(Lpf_Dyn_Entry* entry, void* float_value, isize float_type_size, void* def, Read_Or_Write action)
{
    return serialize_float_typed(entry, float_value, float_type_size, def, STRING("f"), action);
}

EXPORT bool serialize_id(Lpf_Dyn_Entry* entry, Id* val, Id def, Read_Or_Write action)
{
    return serialize_uint_typed(entry, val, sizeof *val, (u64) def, STRING("id"), action);
}

EXPORT bool serialize_i64(Lpf_Dyn_Entry* entry, i64* val, i64 def, Read_Or_Write action) { return serialize_int(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_i32(Lpf_Dyn_Entry* entry, i32* val, i32 def, Read_Or_Write action) { return serialize_int(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_i16(Lpf_Dyn_Entry* entry, i16* val, i16 def, Read_Or_Write action) { return serialize_int(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_i8(Lpf_Dyn_Entry* entry, i8* val, i8 def, Read_Or_Write action)   { return serialize_int(entry, val, sizeof *val, def, action); }

EXPORT bool serialize_u64(Lpf_Dyn_Entry* entry, u64* val, u64 def, Read_Or_Write action) { return serialize_uint(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_u32(Lpf_Dyn_Entry* entry, u32* val, u32 def, Read_Or_Write action) { return serialize_uint(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_u16(Lpf_Dyn_Entry* entry, u16* val, u16 def, Read_Or_Write action) { return serialize_uint(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_u8(Lpf_Dyn_Entry* entry, u8* val, u8 def, Read_Or_Write action)   { return serialize_uint(entry, val, sizeof *val, def, action); }

EXPORT bool serialize_f64(Lpf_Dyn_Entry* entry, f64* val, f64 def, Read_Or_Write action) { return serialize_float(entry, val, sizeof *val, &def, action); }
EXPORT bool serialize_f32(Lpf_Dyn_Entry* entry, f32* val, f32 def, Read_Or_Write action) { return serialize_float(entry, val, sizeof *val, &def, action); }

EXPORT bool serialize_string(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    return serialize_string_typed(entry, val, def, STRING("s"), action);
}
EXPORT bool serialize_name(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_name(entry, val, def);
    else
        return serialize_write_name(entry, string_from_builder(*val), STRING("n"));
}
EXPORT bool serialize_raw(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    return serialize_raw_typed(entry, val, def, STRING("raw"), action);
}
EXPORT bool serialize_base64(Lpf_Dyn_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    return serialize_base64_typed(entry, val, def, STRING("base64"), action);
}


EXPORT bool serialize_vec2(Lpf_Dyn_Entry* entry, Vec2* val, Vec2 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        
        isize index = 0;
        match_whitespace(readable.value, &index);
        
        bool state = true;
        state = state && match_decimal_f32(readable.value, &index, &val->x);
        state = state && match_whitespace(readable.value, &index);
        state = state && match_decimal_f32(readable.value, &index, &val->y);
        if(state == false)
            *val = def;

        return state;
    }
    else
    {
        serialize_entry_set_identity(entry, STRING("2f"), format_ephemeral("%f %f", val->x, val->y), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        return true;
    }
}
EXPORT bool serialize_vec3(Lpf_Dyn_Entry* entry, Vec3* val, Vec3 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        
        isize index = 0;
        match_whitespace(readable.value, &index);
        
        bool state = true;
        state = state && match_decimal_f32(readable.value, &index, &val->x);
        state = state && match_whitespace(readable.value, &index);
        state = state && match_decimal_f32(readable.value, &index, &val->y);
        state = state && match_whitespace(readable.value, &index);
        state = state && match_decimal_f32(readable.value, &index, &val->z);
        if(state == false)
            *val = def;

        return state;
    }
    else
    {
        serialize_entry_set_identity(entry, STRING("3f"), format_ephemeral("%f %f %f", val->x, val->y, val->z), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        return true;
    }
}

EXPORT bool serialize_vec4_typed(Lpf_Dyn_Entry* entry, Vec4* val, Vec4 def, String type, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        
        isize index = 0;
        match_whitespace(readable.value, &index);
        
        bool state = true;
        state = state && match_decimal_f32(readable.value, &index, &val->x);
        state = state && match_whitespace(readable.value, &index);
        state = state && match_decimal_f32(readable.value, &index, &val->y);
        state = state && match_whitespace(readable.value, &index);
        state = state && match_decimal_f32(readable.value, &index, &val->z);
        state = state && match_whitespace(readable.value, &index);
        state = state && match_decimal_f32(readable.value, &index, &val->w);
        if(state == false)
            *val = def;

        return state;
    }
    else
    {
        serialize_entry_set_identity(entry, type, format_ephemeral("%f %f %f %f", val->x, val->y, val->z, val->w), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);
        return true;
    }
}
EXPORT bool serialize_vec4(Lpf_Dyn_Entry* entry, Vec4* val, Vec4 def, Read_Or_Write action)
{
    return serialize_vec4_typed(entry, val, def, STRING("4f"), action);
}

EXPORT bool serialize_quat(Lpf_Dyn_Entry* entry, Quat* val, Quat def, Read_Or_Write action)
{
    return serialize_vec4_typed(entry, (Vec4*) (void*) val, *(Vec4*) (void*) &def, STRING("4f"), action);
}

EXPORT bool serialize_mat2(Lpf_Dyn_Entry* entry, Mat2* val, Mat2 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        
        isize index = 0;
        match_whitespace(readable.value, &index);
        
        bool state = true;
        for(isize y = 0; y < 2; y++)
            for(isize x = 0; x < 2; x++)
            {
                state = state && match_decimal_f32(readable.value, &index, &val->m[x][y]);
                match_whitespace(readable.value, &index);
            }
            
        if(state == false)
            *val = def;
        return state;
    }
    else
    {
        serialize_entry_set_identity(entry, STRING("4f"), format_ephemeral(
            "%f %f\n"
            "%f %f", 
            val->m11, val->m12,
            val->m21, val->m22
            ), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);

        return true;
    }
}

EXPORT bool serialize_mat3(Lpf_Dyn_Entry* entry, Mat3* val, Mat3 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        
        isize index = 0;
        match_whitespace(readable.value, &index);
        
        bool state = true;
        for(isize y = 0; y < 3; y++)
            for(isize x = 0; x < 3; x++)
            {
                state = state && match_decimal_f32(readable.value, &index, &val->m[x][y]);
                match_whitespace(readable.value, &index);
            }
            
        if(state == false)
            *val = def;
        return state;
    }
    else
    {
        serialize_entry_set_identity(entry, STRING("9f"), format_ephemeral(
            "%f %f %f\n"
            "%f %f %f\n"
            "%f %f %f",
            val->m11, val->m12, val->m13,
            val->m21, val->m22, val->m23,
            val->m31, val->m32, val->m33
            ), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);

        return true;
    }
}

EXPORT bool serialize_mat4(Lpf_Dyn_Entry* entry, Mat4* val, Mat4 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = lpf_entry_from_dyn_entry(*entry);
        
        isize index = 0;
        match_whitespace(readable.value, &index);
        
        bool state = true;
        for(isize y = 0; y < 4; y++)
            for(isize x = 0; x < 4; x++)
            {
                state = state && match_decimal_f32(readable.value, &index, &val->m[x][y]);
                match_whitespace(readable.value, &index);
            }
            
        if(state == false)
            *val = def;
        return state;
    }
    else
    {
        serialize_entry_set_identity(entry, STRING("16f"), format_ephemeral(
            "%f %f %f %f\n"
            "%f %f %f %f\n"
            "%f %f %f %f\n"
            "%f %f %f %f", 
            val->m11, val->m12, val->m13, val->m14,
            val->m21, val->m22, val->m23, val->m24,
            val->m31, val->m32, val->m33, val->m34,
            val->m41, val->m42, val->m43, val->m44 
            ), LPF_KIND_ENTRY, LPF_FLAG_WHITESPACE_AGNOSTIC);

        return true;
    }
}

#endif