#include <stdint.h>
#include <stdbool.h>
#include "string.h"

typedef struct Serialize_Context Serialize_Context;

typedef enum Ser_Type {
    //primitive types =======
    SER_NULL = 0,
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

    //vector primitive types (more to be added when needed)
    SER_F32_X2,
    SER_F32_X3,
    SER_F32_X4,
    
    SER_I32_X2,
    SER_I32_X3,
    SER_I32_X4,

    //dynamic types =======

    //Declares begining/end of a list. 
    //Behaves just like '[' / ']' in JSON.
    //The items are simply listed one after each other 
    // and are considered part of the list until SER_LIST_END
    // is found.
    SER_LIST_BEGIN = 128,
    SER_LIST_END,

    //Declares begining/end of an object. 
    //Behaves just like '{' / '}' in JSON.
    //Items are parsed in pairs where the first
    // is taken to be the key and the second is
    // taken to be the value.  
    SER_OBJECT_BEGIN,
    SER_OBJECT_END,

    //String type. The number after underscore represents
    // the number of bits used to encode the string length.
    // 0 means the string is zero sized - empty string.
    //SER_STRING_8, SER_STRING_64 is null terminated while SER_STRING_0 is not.
    //The size does not include the null termination
    SER_STRING_0,   //{u8 type}
    SER_STRING_8,   //{u8 type, u8 size}[size bytes]\0
    SER_STRING_64,  //{u8 type, u64 size}[size bytes]\0

    //Just like string except signals to any visualizer
    // that this data should be probably converted to 
    // base64 or base16 instead of string with escapes.
    //Note that binary, just like string, is followed with
    // null termination byte (unless zero sized).
    SER_BINARY_0,
    SER_BINARY_8,
    SER_BINARY_64,

    //An array of primitive type.
    //Writes the type tag only once and then follows with 
    // a densely packed array of the target type. This allows
    // for efficient serialization of simple/SoA data.
    SER_PRIMITIVE_ARRAY_0,  //{u8 type, u8 primitive type}
    SER_PRIMITIVE_ARRAY_8,  //{u8 type, u8 primitive type, u8 count} [array of count items]
    SER_PRIMITIVE_ARRAY_64, //{u8 type, u8 primitive type, u64 count} [array of count items]
    
    SER_TYPE_ENUM_COUNT
} Ser_Type;

isize set_type_size(Ser_Type type);
const char* set_type_name(Ser_Type type);

void ser_write(Serialize_Context* context, const void* ptr, isize size);

void ser_list_begin(Serialize_Context* context);
void ser_list_end(Serialize_Context* context);

void ser_object_begin(Serialize_Context* context);
void ser_object_end(Serialize_Context* context);

void ser_primitive(Serialize_Context* context, Ser_Type type, const void* ptr, isize size);
void ser_binary(Serialize_Context* context, const void* ptr, isize size);
void ser_string(Serialize_Context* context, const void* ptr, isize size);
void ser_array(Serialize_Context* context, Ser_Type type, const void* ptr, isize count);
void ser_null(Serialize_Context* context);

void ser_i8(Serialize_Context* context, i8 val);
void ser_i16(Serialize_Context* context, i16 val);
void ser_i32(Serialize_Context* context, i32 val);
void ser_i64(Serialize_Context* context, i64 val);

void ser_u8(Serialize_Context* context, u8 val);
void ser_u16(Serialize_Context* context, u16 val);
void ser_u32(Serialize_Context* context, u32 val);
void ser_u64(Serialize_Context* context, u64 val);

void ser_f32(Serialize_Context* context, f32 val);
void ser_f64(Serialize_Context* context, f64 val);

void ser_i32_x2(Serialize_Context* context, i32 val1, i32 val2);
void ser_i32_x3(Serialize_Context* context, i32 val1, i32 val2, i32 val3);
void ser_i32_x4(Serialize_Context* context, i32 val1, i32 val2, i32 val3, i32 val4);

void ser_f32_x2(Serialize_Context* context, f32 val1, f32 val2);
void ser_f32_x3(Serialize_Context* context, f32 val1, f32 val2, f32 val3);
void ser_f32_x4(Serialize_Context* context, f32 val1, f32 val2, f32 val3, f32 val4);

bool deser_read(Serialize_Context* context, void* ptr, isize size);

typedef struct Ser_String {
    const char* data;
    isize count;
} Ser_String;

typedef struct Ser_F32_X2 {
    f32 x;
    f32 y;
} Ser_F32_X2;

typedef struct Ser_F32_X3 {
    f32 x;
    f32 y;
    f32 z;
} Ser_F32_X3;

typedef struct Ser_F32_X4 {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
} Ser_F32_X4;

typedef struct Deser_Value {
    isize depth;
    isize offset;
    Ser_Type exact_type;
    Ser_Type type;
    union {
        Ser_String val_binary;
        Ser_String val_string;
        bool val_bool;

        i64 val_i64;
        u64 val_u64;
        f32 val_f32;
        f64 val_f64;

        Ser_F32_X2 val_f32_x2;
        Ser_F32_X3 val_f32_x3;
        Ser_F32_X4 val_f32_x4;
        
        Ser_F32_X2 val_i32_x2;
        Ser_F32_X3 val_i32_x3;
        Ser_F32_X4 val_i32_x4;
    };
} Deser_Value;

Deser_Value deser_value(Serialize_Context* context);
void deser_skip_to_depth(Serialize_Context* context, isize depth);
void deser_skip_to_offset(Serialize_Context* context, isize offset);
bool deser_iterate_array(Serialize_Context* context, Deser_Value* out_val, Deser_Value array);
bool deser_iterate_object(Serialize_Context* context, Deser_Value* out_key, Deser_Value* out_val, Deser_Value array);


