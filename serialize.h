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
// For code simplicity we allow the Lpf_Entry* entry to be NULL (this is the case when localization fails). 
// In that case the serialize function fails.
// 
// Lastly we supply each function with 'def' default value to be used when the localization or reading would fail. 
// This is purely for conveniance and could be implemented caller side by:
// 
// if(serialize_(serialize_locate(entry, "my_val", action), &my_val, action) == false && action == READ)
//    my_val = def;

#include "lpf.h"
#include "parse.h"
#include "vformat.h"
#include "math.h"
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

EXPORT void base16_encode_append_into(String_Builder* into, const void* data, isize len);
EXPORT isize base16_decode_append_into(String_Builder* into, const void* data, isize len);
EXPORT void base16_encode_into(String_Builder* into, const void* data, isize len);
EXPORT isize base16_decode_into(String_Builder* into, const void* data, isize len);

//Attempts to locate an entry within children of into and return pointer to it. 
//If it cannot find it: returns NULL if action is read or creates it if action is write.
EXPORT Lpf_Entry* serialize_locate_any(Lpf_Entry* into, Lpf_Kind create_kind, Lpf_Kind kind, String label, Read_Or_Write action);
EXPORT Lpf_Entry* serialize_locate(Lpf_Entry* into, const char* label, Read_Or_Write action);
EXPORT void       serialize_entry_set_identity(Lpf_Entry* entry, String value, Lpf_Kind kind);

//Explicit read/write interface. 
//This is mostly important for things requiring string or string builder 
//(often we have data in format compatible with string but not within string builder.
// This is the case for images, vertices etc.)
EXPORT bool serialize_write_raw(Lpf_Entry* entry, String val);
EXPORT bool serialize_read_raw(Lpf_Entry* entry, String_Builder* val, String def);

EXPORT bool serialize_write_base16(Lpf_Entry* entry, String val);
EXPORT bool serialize_read_base16(Lpf_Entry* entry, String_Builder* val, String def);

EXPORT bool serialize_write_string(Lpf_Entry* entry, String val);
EXPORT bool serialize_read_string(Lpf_Entry* entry, String_Builder* val, String def);

EXPORT bool serialize_write_name(Lpf_Entry* entry, String val);
EXPORT bool serialize_read_name(Lpf_Entry* entry, String_Builder* val, String def);

//Serialize interface
EXPORT bool serialize_scope(Lpf_Entry* entry, Read_Or_Write action);
EXPORT bool serialize_comment(Lpf_Entry* entry, String comment, Read_Or_Write action);

EXPORT bool serialize_raw_typed(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_string_typed(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_base16_typed(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_int_typed(Lpf_Entry* entry, void* int_value, isize int_type_size, i64 def, Read_Or_Write action);
EXPORT bool serialize_uint_typed(Lpf_Entry* entry, void* int_value, isize int_type_size, u64 def, Read_Or_Write action);
EXPORT bool serialize_float_typed(Lpf_Entry* entry, void* float_value, isize float_type_size, void* def, Read_Or_Write action);
EXPORT bool serialize_int_count_typed(Lpf_Entry* entry, void* value, isize value_type_size, const void* defs, isize count, Read_Or_Write action);
EXPORT bool serialize_enum_typed(Lpf_Entry* entry, void* enum_value, isize enum_type_size, i64 def, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action);

EXPORT bool serialize_int(Lpf_Entry* entry, void* int_value, isize int_type_size, i64 def, Read_Or_Write action);
EXPORT bool serialize_uint(Lpf_Entry* entry, void* int_value, isize int_type_size, u64 def, Read_Or_Write action);
EXPORT bool serialize_float(Lpf_Entry* entry, void* float_value, isize float_type_size, void* def, Read_Or_Write action);
EXPORT bool serialize_enum(Lpf_Entry* entry, void* enum_value, isize enum_type_size, i64 def, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action);

EXPORT bool serialize_bool(Lpf_Entry* entry, bool* val, bool def, Read_Or_Write action);
EXPORT bool serialize_id(Lpf_Entry* entry, Id* val, Id def, Read_Or_Write action);

EXPORT bool serialize_i64(Lpf_Entry* entry, i64* val, i64 def, Read_Or_Write action);
EXPORT bool serialize_i32(Lpf_Entry* entry, i32* val, i32 def, Read_Or_Write action);
EXPORT bool serialize_i16(Lpf_Entry* entry, i16* val, i16 def, Read_Or_Write action);
EXPORT bool serialize_i8(Lpf_Entry* entry, i8* val, i8 def, Read_Or_Write action);

EXPORT bool serialize_u64(Lpf_Entry* entry, u64* val, u64 def, Read_Or_Write action);
EXPORT bool serialize_u32(Lpf_Entry* entry, u32* val, u32 def, Read_Or_Write action);
EXPORT bool serialize_u16(Lpf_Entry* entry, u16* val, u16 def, Read_Or_Write action);
EXPORT bool serialize_u8(Lpf_Entry* entry, u8* val, u8 def, Read_Or_Write action);

EXPORT bool serialize_f64(Lpf_Entry* entry, f64* val, f64 def, Read_Or_Write action);
EXPORT bool serialize_f32(Lpf_Entry* entry, f32* val, f32 def, Read_Or_Write action);

EXPORT bool serialize_string(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_name(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_raw(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);
EXPORT bool serialize_base16(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action);

EXPORT bool serialize_vec2(Lpf_Entry* entry, Vec2* val, Vec2 def, Read_Or_Write action);
EXPORT bool serialize_vec3(Lpf_Entry* entry, Vec3* val, Vec3 def, Read_Or_Write action);
EXPORT bool serialize_vec4(Lpf_Entry* entry, Vec4* val, Vec4 def, Read_Or_Write action);

EXPORT bool serialize_mat2(Lpf_Entry* entry, Mat2* val, Mat2 def, Read_Or_Write action);
EXPORT bool serialize_mat3(Lpf_Entry* entry, Mat3* val, Mat3 def, Read_Or_Write action);
EXPORT bool serialize_mat4(Lpf_Entry* entry, Mat4* val, Mat4 def, Read_Or_Write action);

EXPORT bool serialize_quat(Lpf_Entry* entry, Quat* val, Quat def, Read_Or_Write action);

#endif

#if (defined(JOT_ALL_IMPL) || defined(JOT_SERIALIZE_IMPL)) && !defined(JOT_SERIALIZE_HAS_IMPL)
#define JOT_SERIALIZE_HAS_IMPL



EXPORT void base16_encode_append_into(String_Builder* into, const void* data, isize len)
{
    isize size_before = into->size;
    builder_resize(into, size_before + 2*len);
    
    for(isize i = 0; i < len; i++)
    {
        u8 val = ((u8*) data)[i];

        u8 lo = val & 0xF;
        u8 hi = val >> 4;

        //Just think how much faster everything could have been if ascii
        // was layed out like 0-9 : a-z : A-Z. We would be truly living 
        // in a different and better wold
        char lo_num = '0' + lo;
        char hi_num = '0' + hi;
        
        char lo_letter = 'a' + lo - 9;
        char hi_letter = 'a' + hi - 9;

        char lo_char = lo <= 9 ? lo_num : lo_letter;
        char hi_char = hi <= 9 ? hi_num : hi_letter;

        into->data[size_before + 2*i] = hi_char;
        into->data[size_before + 2*i + 1] = lo_char;
    }
}

EXPORT isize base16_decode_append_into(String_Builder* into, const void* data, isize len)
{
    isize size_before = into->size;
    builder_resize(into, size_before + len/2);
    
    for(isize i = 0; i < len; i += 2)
    {
        char hi_char = ((char*) data)[i];
        char lo_char = ((char*) data)[i + 1];

        u8 lo_val1 = lo_char - '9';
        u8 lo_val2 = lo_char - 'a' + 9;
        u8 lo_val3 = lo_char - 'A' + 9;
        
        u8 hi_val1 = hi_char - '9';
        u8 hi_val2 = hi_char - 'a' + 9;
        u8 hi_val3 = hi_char - 'A' + 9;

        u8 lo_val = MIN(MIN(lo_val1, lo_val2), lo_val3);
        u8 hi_val = MIN(MIN(hi_val1, hi_val2), hi_val3);

        if(lo_val > 15 || hi_val > 15)
            return i + 1;

        u8 val = hi_val << 4 | lo_val;

        into->data[size_before + i] = (char) val;
    }

    return 0;
}

EXPORT void base16_encode_into(String_Builder* into, const void* data, isize len)
{
    builder_clear(into);
    base16_encode_append_into(into, data, len);
}

EXPORT isize base16_decode_into(String_Builder* into, const void* data, isize len)
{
    builder_clear(into);
    return base16_decode_append_into(into, data, len);
}

EXPORT Lpf_Entry* serialize_locate_any(Lpf_Entry* into, Lpf_Kind create_kind, Lpf_Kind kind, String label, Read_Or_Write action)
{
    if(into == NULL)
        return NULL;

    isize found_i = -1;
    for(isize i = 0; i < into->children_count; i++)
    {
        Lpf_Entry entry = into->children[i];
        if(string_is_equal(entry.label, label) && entry.kind == kind)
        {
            found_i = i;
            break;
        }
    }

    if(found_i != -1)
        return &into->children[found_i];

    if(action == SERIALIZE_READ)
        return NULL;
    else
    {
        Lpf_Entry entry = {create_kind};
        entry.label = label;

        //@TODO
        Lpf_Entry* created = lpf_entry_push_child(NULL, into, entry);
        return created;
    }
}

EXPORT Lpf_Entry* serialize_locate(Lpf_Entry* into, const char* label, Read_Or_Write action)
{
    return serialize_locate_any(into, LPF_ENTRY, (Lpf_Kind) (-1), string_make(label), action);
}

EXPORT Lpf_Entry* lpf_dyn_entry_add(Lpf_Entry* into, Lpf_Kind kind, const char* label)
{
    Lpf_Entry entry = {kind};
    entry.label = string_make(label);
    Lpf_Entry* created = lpf_entry_push_child(NULL, into, entry);
    return created;
}

EXPORT Lpf_Entry* lpf_dyn_value_add(Lpf_Entry* into, const char* label)
{
    return lpf_dyn_entry_add(into, LPF_ENTRY, label);
}

EXPORT void serialize_entry_set_identity(Lpf_Entry* entry, String value, Lpf_Kind kind)
{
    entry->value = lpf_string_duplicate(NULL, value);
    entry->kind = kind;
}
EXPORT bool serialize_write_raw(Lpf_Entry* entry, String val)
{
    if(entry == NULL)
        return false;
        
    serialize_entry_set_identity(entry, val, LPF_ENTRY);
    return true;
}
EXPORT bool serialize_read_raw(Lpf_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }
        
    Lpf_Entry readable = *entry;
    builder_assign(val, readable.value);
    return true;
}

EXPORT bool serialize_write_string(Lpf_Entry* entry, String val)
{
    if(entry == NULL)
        return false;
        
    serialize_entry_set_identity(entry, val, LPF_ENTRY);
    return true;
}

EXPORT bool serialize_read_string(Lpf_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }
        
    Lpf_Entry readable = *entry;
    String trimmed = string_trim_whitespace(readable.value);
    builder_assign(val, trimmed);
    return true;
}


EXPORT bool serialize_write_name(Lpf_Entry* entry, String val)
{
    if(entry == NULL)
        return false;
        
    serialize_entry_set_identity(entry, val, LPF_ENTRY);
    return true;

}
EXPORT bool serialize_read_name(Lpf_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }
        
    Lpf_Entry readable = *entry;
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

EXPORT bool serialize_write_base16(Lpf_Entry* entry, String val)
{
    Arena arena = scratch_arena_acquire();
    {
        String_Builder encoded = {&arena.allocator};
        base16_encode_append_into(&encoded, val.data, val.size);

        serialize_entry_set_identity(entry, encoded.string, LPF_ENTRY);
    }
    arena_release(&arena);

    return true;
}

EXPORT bool serialize_read_base16(Lpf_Entry* entry, String_Builder* val, String def)
{
    if(entry == NULL)
    {
        builder_assign(val, def);
        return false;
    }

    Lpf_Entry readable = *entry;
    String trimmed = string_trim_whitespace(readable.value);
    isize broke_at = base16_decode_into(val, trimmed.data, trimmed.size);
    if(broke_at != 0);
        builder_assign(val, def);

    return broke_at == 0;
}

EXPORT bool serialize_scope(Lpf_Entry* entry, Read_Or_Write action)
{
    if(entry == NULL)
        return false;
        
    if(action == SERIALIZE_WRITE)
    {
        serialize_entry_set_identity(entry, STRING(""), LPF_COLLECTION);
        return true;
    }
    else
    {
        return entry->kind == LPF_COLLECTION;
    }
}

EXPORT bool serialize_comment(Lpf_Entry* entry, String comment, Read_Or_Write action)
{
    if(entry == NULL)
        return false;
        
    if(action == SERIALIZE_WRITE)
    {
        Lpf_Entry added = {LPF_COMMENT};
        added.value = lpf_string_duplicate(NULL, comment);
        lpf_entry_push_child(NULL, entry, added);
    }

    return true;
}

EXPORT bool serialize_raw_typed(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_raw(entry, val, def);
    else
        return serialize_write_raw(entry, val->string);
}

EXPORT bool serialize_string_typed(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_string(entry, val, def);
    else
        return serialize_write_string(entry, val->string);
}

EXPORT bool serialize_base16_typed(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_base16(entry, val, def);
    else
        return serialize_write_base16(entry, val->string);
}

void set_variable_sized_int(void* integer, isize integer_type_size, i64 value)
{
    switch(integer_type_size)
    {
        case 1: *(i8*) (integer) = (i8) value; return;
        case 2: *(i16*) (integer) = (i16) value; return;
        case 4: *(i32*) (integer) = (i32) value; return;
        case 8: *(i64*) (integer) = (i64) value; return;
        default: ASSERT(false, "Strange type size submitted!");
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
        default: ASSERT(false, "Strange type size submitted!"); return 0;
    }
}

EXPORT bool serialize_int_count_typed(Lpf_Entry* entry, void* value, isize value_type_size, const void* defs, isize count, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;

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
            serialize_entry_set_identity(entry, format_ephemeral("%lli", concrete_value), LPF_ENTRY);
        }
        else
        {
            Arena arena = scratch_arena_acquire();
            {
                String_Builder formatted = builder_make(&arena.allocator, 256);
                for(isize i = 0; i < count; i ++)
                {
                    if(i != 0)
                        builder_push(&formatted, ' ');
                        
                    i64 concrete_value = get_variable_sized_int((u8*) value + i*value_type_size, value_type_size);
                    format_append_into(&formatted, "%lli", concrete_value);
                }
            }
            arena_release(&arena);
        }

        return true;
    }
}

EXPORT bool serialize_int_typed(Lpf_Entry* entry, void* int_value, isize int_type_size, i64 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    i64 value = 0;
    if(action == SERIALIZE_READ)
    {
        isize index = 0;
        Lpf_Entry readable = *entry;
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
        serialize_entry_set_identity(entry, format_ephemeral("%lli", value), LPF_ENTRY);
        return true;
    }
}

EXPORT bool serialize_uint_typed(Lpf_Entry* entry, void* int_value, isize int_type_size, u64 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    u64 value = 0;
    if(action == SERIALIZE_READ)
    {
        isize index = 0;
        Lpf_Entry readable = *entry;
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
        serialize_entry_set_identity(entry, format_ephemeral("%llu", value), LPF_ENTRY);
        return true;
    }
}

EXPORT bool serialize_float_typed(Lpf_Entry* entry, void* float_value, isize float_type_size, void* def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    ASSERT(float_type_size == 4 || float_type_size == 8, "Invalid floating point format!");
    if(action == SERIALIZE_READ)
    {
        isize index = 0;
        Lpf_Entry readable = *entry;
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
            serialize_entry_set_identity(entry, format_ephemeral("%lf", *(f64*) float_value), LPF_ENTRY);
        else
            serialize_entry_set_identity(entry, format_ephemeral("%f", *(f32*) float_value), LPF_ENTRY);
            
        return true;
    }
}

EXPORT bool serialize_enum_typed(Lpf_Entry* entry, void* enum_value, isize enum_type_size, i64 def, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
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
                serialize_entry_set_identity(entry, enum_entry.name, LPF_ENTRY);
                return true;
            }
        }

        return false;
    }
}

EXPORT bool serialize_enum(Lpf_Entry* entry, void* enum_value, isize enum_type_size, i64 def, const Serialize_Enum* enums, isize enums_size, Read_Or_Write action)
{
    return serialize_enum_typed(entry, enum_value, enum_type_size, def, enums, enums_size, action);
}

EXPORT bool serialize_bool(Lpf_Entry* entry, bool* val, bool def, Read_Or_Write action)
{
    Serialize_Enum enums[] = {
        SERIALIZE_ENUM_VALUE(false),
        SERIALIZE_ENUM_VALUE(true),
        SERIALIZE_ENUM_VALUE(0),
        SERIALIZE_ENUM_VALUE(1),
    };

    return serialize_enum_typed(entry, val, sizeof *val, (isize) def, enums, STATIC_ARRAY_SIZE(enums), action);
}

EXPORT bool serialize_int(Lpf_Entry* entry, void* int_value, isize int_type_size, i64 def, Read_Or_Write action)
{
    return serialize_int_typed(entry, int_value, int_type_size, def, action);
}

EXPORT bool serialize_uint(Lpf_Entry* entry, void* int_value, isize int_type_size, u64 def, Read_Or_Write action)
{
    return serialize_uint_typed(entry, int_value, int_type_size, def, action);
}

EXPORT bool serialize_float(Lpf_Entry* entry, void* float_value, isize float_type_size, void* def, Read_Or_Write action)
{
    return serialize_float_typed(entry, float_value, float_type_size, def, action);
}

EXPORT bool serialize_id(Lpf_Entry* entry, Id* val, Id def, Read_Or_Write action)
{
    return serialize_uint_typed(entry, val, sizeof *val, (u64) def, action);
}

EXPORT bool serialize_i64(Lpf_Entry* entry, i64* val, i64 def, Read_Or_Write action) { return serialize_int(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_i32(Lpf_Entry* entry, i32* val, i32 def, Read_Or_Write action) { return serialize_int(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_i16(Lpf_Entry* entry, i16* val, i16 def, Read_Or_Write action) { return serialize_int(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_i8(Lpf_Entry* entry, i8* val, i8 def, Read_Or_Write action)   { return serialize_int(entry, val, sizeof *val, def, action); }

EXPORT bool serialize_u64(Lpf_Entry* entry, u64* val, u64 def, Read_Or_Write action) { return serialize_uint(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_u32(Lpf_Entry* entry, u32* val, u32 def, Read_Or_Write action) { return serialize_uint(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_u16(Lpf_Entry* entry, u16* val, u16 def, Read_Or_Write action) { return serialize_uint(entry, val, sizeof *val, def, action); }
EXPORT bool serialize_u8(Lpf_Entry* entry, u8* val, u8 def, Read_Or_Write action)   { return serialize_uint(entry, val, sizeof *val, def, action); }

EXPORT bool serialize_f64(Lpf_Entry* entry, f64* val, f64 def, Read_Or_Write action) { return serialize_float(entry, val, sizeof *val, &def, action); }
EXPORT bool serialize_f32(Lpf_Entry* entry, f32* val, f32 def, Read_Or_Write action) { return serialize_float(entry, val, sizeof *val, &def, action); }

EXPORT bool serialize_string(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    return serialize_string_typed(entry, val, def, action);
}
EXPORT bool serialize_name(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    if(action == SERIALIZE_READ)
        return serialize_read_name(entry, val, def);
    else
        return serialize_write_name(entry, val->string);
}
EXPORT bool serialize_raw(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    return serialize_raw_typed(entry, val, def, action);
}
EXPORT bool serialize_base16(Lpf_Entry* entry, String_Builder* val, String def, Read_Or_Write action)
{
    return serialize_base16_typed(entry, val, def, action);
}


EXPORT bool serialize_vec2(Lpf_Entry* entry, Vec2* val, Vec2 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
        
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
        serialize_entry_set_identity(entry, format_ephemeral("%f %f", val->x, val->y), LPF_ENTRY);
        return true;
    }
}
EXPORT bool serialize_vec3(Lpf_Entry* entry, Vec3* val, Vec3 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
        
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
        serialize_entry_set_identity(entry, format_ephemeral("%f %f %f", val->x, val->y, val->z), LPF_ENTRY);
        return true;
    }
}

EXPORT bool serialize_vec4_typed(Lpf_Entry* entry, Vec4* val, Vec4 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
        
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
        serialize_entry_set_identity(entry, format_ephemeral("%f %f %f %f", val->x, val->y, val->z, val->w), LPF_ENTRY);
        return true;
    }
}
EXPORT bool serialize_vec4(Lpf_Entry* entry, Vec4* val, Vec4 def, Read_Or_Write action)
{
    return serialize_vec4_typed(entry, val, def, action);
}

EXPORT bool serialize_quat(Lpf_Entry* entry, Quat* val, Quat def, Read_Or_Write action)
{
    return serialize_vec4_typed(entry, (Vec4*) (void*) val, *(Vec4*) (void*) &def, action);
}

EXPORT bool serialize_mat2(Lpf_Entry* entry, Mat2* val, Mat2 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
        
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
        serialize_entry_set_identity(entry, format_ephemeral(
            "%f %f\n"
            "%f %f", 
            val->m11, val->m12,
            val->m21, val->m22
            ), LPF_ENTRY);

        return true;
    }
}

EXPORT bool serialize_mat3(Lpf_Entry* entry, Mat3* val, Mat3 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
        
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
        serialize_entry_set_identity(entry, format_ephemeral(
            "%f %f %f\n"
            "%f %f %f\n"
            "%f %f %f",
            val->m11, val->m12, val->m13,
            val->m21, val->m22, val->m23,
            val->m31, val->m32, val->m33
            ), LPF_ENTRY);

        return true;
    }
}

EXPORT bool serialize_mat4(Lpf_Entry* entry, Mat4* val, Mat4 def, Read_Or_Write action)
{
    if(entry == NULL)
        return false;

    if(action == SERIALIZE_READ)
    {
        Lpf_Entry readable = *entry;
        
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
        serialize_entry_set_identity(entry, format_ephemeral(
            "%f %f %f %f\n"
            "%f %f %f %f\n"
            "%f %f %f %f\n"
            "%f %f %f %f", 
            val->m11, val->m12, val->m13, val->m14,
            val->m21, val->m22, val->m23, val->m24,
            val->m31, val->m32, val->m33, val->m34,
            val->m41, val->m42, val->m43, val->m44 
            ), LPF_ENTRY);

        return true;
    }
}

#endif