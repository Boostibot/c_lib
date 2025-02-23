
#include "../serialize.h"
#include "../math.h"
#include "../allocator_debug.h"

//START of test
bool deser_f32v3(const Ser_Value* object, float out[3])
{
    if(object->type == SER_ARRAY)
    {
        int count = 0;
        for(Ser_Value val = {0}; deser_iterate_array(object, &val); ) {
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
    data[i++] = SER_ARRAY_BEGIN;
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
        data[i++] = SER_F32; memcpy(data + i, vals + c++, sizeof(float)); i += sizeof(float);
    data[i++] = SER_ARRAY_END;
    ser_writer_write(w, data, i);
}

typedef enum Map_Scale_Filter {
    MAP_SCALE_FILTER_BILINEAR = 1,
    MAP_SCALE_FILTER_TRILINEAR,
    MAP_SCALE_FILTER_NEAREST,
} Map_Scale_Filter;

typedef enum Map_Repeat {
    MAP_REPEAT_REPEAT = 1,
    MAP_REPEAT_MIRRORED_REPEAT,
    MAP_REPEAT_CLAMP_TO_EDGE,
    MAP_REPEAT_CLAMP_TO_BORDER
} Map_Repeat;

#define MAX_CHANNELS 4
typedef struct Tex_Info {
    String name;
    Vec3 resolution;

    int32_t channels_count; 
    int32_t indices[MAX_CHANNELS]; 

    Map_Scale_Filter filter;
    Map_Repeat repeat;
} Tex_Info;

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

bool deser_map_info(Ser_Value object, Tex_Info* out_map_info)
{
    Tex_Info out = {0};
    bool ok = true;
    for(Ser_Value key, val; deser_iterate_object(&object, &key, &val); )
    {
        if(0) {}
        else if(ser_cstring_eq(key, "name"))            ok &= deser_string(val, &out.name);
        else if(ser_cstring_eq(key, "resolution"))      ok &= deser_f32v3(&val, out.resolution.floats);
        else if(ser_cstring_eq(key, "filter"))          ok &= deser_map_scale_filter(&val, &out.filter);
        else if(ser_cstring_eq(key, "repeat"))          ok &= deser_map_repeat(&val, &out.repeat); 
        else if(ser_cstring_eq(key, "channels_count"))  ok &= deser_i32(val, &out.channels_count);
        else if(ser_cstring_eq(key, "indices"))
        {
            int i = 0;
            for(Ser_Value item; deser_iterate_array(&val, &item) && i < 4; )
                i += deser_i32(item, &out.indices[i]); //log here
        }
    }

    *out_map_info = out;
    return ok;
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

void ser_map_info(Ser_Writer* w, Tex_Info info)
{
    ser_recovery_object_begin(w, "Tex_Info");
    ser_cstring(w, "name");            ser_string(w, info.name);
    ser_cstring(w, "resolution");      ser_f32v3(w, info.resolution.floats);
    ser_cstring(w, "filter");          ser_map_scale_filter(w, info.filter);
    ser_cstring(w, "repeat");          ser_map_repeat(w, info.repeat);
    ser_cstring(w, "channels_count");  ser_i32(w, info.channels_count);
    ser_cstring(w, "indices");
    ser_array_begin(w);
    for(int i = 0; i < MAX_CHANNELS; i++)
        ser_i32(w, info.indices[i]);
    ser_array_end(w);
    ser_recovery_object_end(w, "Tex_Info");
}


void test_ser_single(Tex_Info input, bool success)
{
    Tex_Info output = {0};
    Ser_Writer writer = {0};
    ser_writer_init(&writer, NULL, 0, NULL);

    ser_map_info(&writer, input);

    Ser_Reader reader = ser_reader_make(writer.data, writer.offset);
    Ser_Value map_info_val = {0};
    TEST(deser_value(&reader, &map_info_val));
    TEST(deser_map_info(map_info_val, &output) == success);

    bool is_equal = true
        && string_is_equal(input.name, output.name)
        && vec3_is_equal(input.resolution, output.resolution)
        && input.channels_count == output.channels_count
        && input.indices[0] == output.indices[0]
        && input.indices[1] == output.indices[1]
        && input.indices[2] == output.indices[2]
        && input.indices[3] == output.indices[3]
        && input.filter == output.filter
        && input.repeat == output.repeat;

    if(is_equal == false) {
        Ser_Writer json_w = {0};
        Ser_Reader json_r = ser_reader_make(writer.data, writer.offset);
        ser_writer_init(&json_w, NULL, 0, NULL);
        ser_write_json_read(&json_w, &json_r, 2, 256);
        LOG_INFO("test", "%s", json_w.data);
        ser_writer_deinit(&json_w);
        TEST(false);
    }

    ser_writer_deinit(&writer);
}

typedef struct {
    Ser_Type type;
    union {
        uint64_t mu64;
        uint32_t mu32;
        uint16_t mu16;
        uint8_t mu8;
    
        int64_t mi64;
        int32_t mi32;
        int16_t mi16;
        int8_t mi8;
    
        double mf64;
        float mf32;
    };
} Test_Ser_Gen;

Test_Ser_Gen _test_ser_i64(int64_t v) { Test_Ser_Gen out = {SER_I64}; out.mi64 = v; return out; }
Test_Ser_Gen _test_ser_i32(int32_t v) { Test_Ser_Gen out = {SER_I32}; out.mi32 = v; return out; }
Test_Ser_Gen _test_ser_i16(int16_t v) { Test_Ser_Gen out = {SER_I16}; out.mi16 = v; return out; }
Test_Ser_Gen _test_ser_i8(int8_t v)   { Test_Ser_Gen out = {SER_I8}; out.mi8 = v; return out; }

Test_Ser_Gen _test_ser_u64(uint64_t v) { Test_Ser_Gen out = {SER_U64}; out.mu64 = v; return out; }
Test_Ser_Gen _test_ser_u32(uint32_t v) { Test_Ser_Gen out = {SER_U32}; out.mu32 = v; return out; }
Test_Ser_Gen _test_ser_u16(uint16_t v) { Test_Ser_Gen out = {SER_U16}; out.mu16 = v; return out; }
Test_Ser_Gen _test_ser_u8(uint8_t v)   { Test_Ser_Gen out = {SER_U8}; out.mu8 = v; return out; }

Test_Ser_Gen _test_ser_f64(double v) { Test_Ser_Gen out = {SER_F64}; out.mf64 = v; return out; }
Test_Ser_Gen _test_ser_f32(float v) { Test_Ser_Gen out = {SER_F32}; out.mf32 = v; return out; }

void test_ser_conversion(Test_Ser_Gen from, Test_Ser_Gen expected, bool success)
{
    //This test assumes little endian - it could be written in a way to not assume it 
    // (switch statement and passing pointer to the field instead of &res.mu64) but I am lazy...
    Test_Ser_Gen res = {expected.type};
    bool ok = ser_convert_generic_num(from.type, from.mu64, res.type, &res.mu64);
    TEST(ok == success);
    if(ok)
        TEST(res.mu64 == expected.mu64);
}

//TODO: test recovery, forwards/backwards comaptibility through skipping fields of objects etc.
void test_serialize()
{
    test_ser_single(SINIT(Tex_Info){STRING(""),                     vec3(320, 980),             4, {1, 2, 3, 4},   MAP_SCALE_FILTER_BILINEAR,   MAP_REPEAT_REPEAT}, true);
    test_ser_single(SINIT(Tex_Info){STRING("first \n\t\0 some"),    vec3(1e9f, -3, 0),          4, {-32, 0, 3, 4}, MAP_SCALE_FILTER_TRILINEAR,  MAP_REPEAT_MIRRORED_REPEAT}, true);
    test_ser_single(SINIT(Tex_Info){STRING("first some"),           vec3(320, 980, 1),          2, {1, 2, 0, 0},   MAP_SCALE_FILTER_NEAREST,    MAP_REPEAT_CLAMP_TO_EDGE}, true);
    test_ser_single(SINIT(Tex_Info){STRING("abcdefgh"),             vec3(INFINITY, INFINITY),   0, {0, 0, 0, 0},   (Map_Scale_Filter) 0,        MAP_REPEAT_CLAMP_TO_BORDER}, false);
    
    //common conversions should work when they are in their approrpiate range
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_u8(0), false);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_u16(UINT16_MAX), true);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_u32(UINT16_MAX), true);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_u64(UINT16_MAX), true);
    
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_i8(0), false);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_i16(UINT16_MAX), false);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_i32(UINT16_MAX), true);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_i64(UINT16_MAX), true);
    
    //negatives cannot be cast to psoitives
    test_ser_conversion(_test_ser_i64(INT16_MIN), _test_ser_u8(0), false);
    test_ser_conversion(_test_ser_i64(INT16_MIN), _test_ser_u16(0), false);
    test_ser_conversion(_test_ser_i64(INT16_MIN), _test_ser_u32(0), false);
    test_ser_conversion(_test_ser_i64(INT16_MIN), _test_ser_u64(0), false);
    
    test_ser_conversion(_test_ser_i64(INT32_MIN), _test_ser_i8(0), false);
    test_ser_conversion(_test_ser_i64(INT32_MIN), _test_ser_i16(0), false);
    test_ser_conversion(_test_ser_i64(INT32_MIN), _test_ser_i32(INT32_MIN), true);
    test_ser_conversion(_test_ser_i64(INT32_MIN), _test_ser_i64(INT32_MIN), true);

    //int to float only when int is representable exactly
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_f32(UINT16_MAX), true);
    test_ser_conversion(_test_ser_i64(UINT16_MAX), _test_ser_f64(UINT16_MAX), true);
    
    test_ser_conversion(_test_ser_i64(1 << 23), _test_ser_f32(1 << 23), true);
    test_ser_conversion(_test_ser_i64(1 << 23), _test_ser_f64(1 << 23), true);
    test_ser_conversion(_test_ser_i64(1ll << 53), _test_ser_f64(1ll << 53), true);
    test_ser_conversion(_test_ser_i64(INT32_MAX), _test_ser_f32(0), false);
    test_ser_conversion(_test_ser_i64(INT32_MAX), _test_ser_f64(INT32_MAX), true);
    test_ser_conversion(_test_ser_u64(INT64_MAX), _test_ser_f64((double) INT64_MAX), false);
    test_ser_conversion(_test_ser_u64((uint64_t) INT64_MAX + 1), _test_ser_f64((uint64_t) INT64_MAX + 1), true);
    
    test_ser_conversion(_test_ser_i64(-(1 << 23)), _test_ser_f32(-(1 << 23)), true);
    test_ser_conversion(_test_ser_i64(-(1 << 23)), _test_ser_f64(-(1 << 23)), true);
    test_ser_conversion(_test_ser_i64(-(1ll << 53)), _test_ser_f64(-(1ll << 53)), true);
    test_ser_conversion(_test_ser_i64(INT32_MIN), _test_ser_f32(INT32_MIN), true);
    test_ser_conversion(_test_ser_i64(INT32_MIN + 1), _test_ser_f32((float) (INT32_MIN + 1)), false);
    test_ser_conversion(_test_ser_i64(INT32_MIN), _test_ser_f64(INT32_MIN), true);
    test_ser_conversion(_test_ser_i64(INT64_MIN), _test_ser_f64(INT64_MIN), true);
    test_ser_conversion(_test_ser_i64(INT64_MIN + 1), _test_ser_f64((double) (INT64_MIN + 1)), false);

    //f32 should always cast to f64
    test_ser_conversion(_test_ser_f32(0), _test_ser_f64(0), true);
    test_ser_conversion(_test_ser_f32(-0), _test_ser_f64(-0), true);
    test_ser_conversion(_test_ser_f32(-1e-32f), _test_ser_f64(-1e-32f), true);
    test_ser_conversion(_test_ser_f32(-1e-32f), _test_ser_f64(-1e-32f), true);
    test_ser_conversion(_test_ser_f32(1e32f), _test_ser_f64(1e32f), true);
    test_ser_conversion(_test_ser_f32(1e32f), _test_ser_f64(1e32f), true);
    test_ser_conversion(_test_ser_f32(INFINITY), _test_ser_f64(INFINITY), true);
    test_ser_conversion(_test_ser_f32(-INFINITY), _test_ser_f64(-INFINITY), true);
    test_ser_conversion(_test_ser_f32(nanf("")), _test_ser_f64(nan("")), true);

    //f64 should cast to f32 only when the original value was float
    test_ser_conversion(_test_ser_f64(1e32f), _test_ser_f32(1e32f), true);
    test_ser_conversion(_test_ser_f64(1e-32f), _test_ser_f32(1e-32f), true);
    test_ser_conversion(_test_ser_f64(-1e32f), _test_ser_f32(-1e32f), true);
    test_ser_conversion(_test_ser_f64(-1e-32f), _test_ser_f32(-1e-32f), true);
    test_ser_conversion(_test_ser_f64(INFINITY), _test_ser_f32(INFINITY), true);
    test_ser_conversion(_test_ser_f64(-INFINITY), _test_ser_f32(-INFINITY), true);
    test_ser_conversion(_test_ser_f64(nanf("")), _test_ser_f32(nanf("")), true);

    //these values are impossible to represent exactly so float/double will have different reps
    test_ser_conversion(_test_ser_f64(0.2), _test_ser_f32(0.2f), false);
    test_ser_conversion(_test_ser_f64(0.1), _test_ser_f32(0.1f), false);
    test_ser_conversion(_test_ser_f64(-0.1), _test_ser_f32(-0.1f), false);
    
    test_ser_conversion(_test_ser_f32(0), _test_ser_i16(0), true);
    test_ser_conversion(_test_ser_f32(0.5), _test_ser_i16(0), false);
    test_ser_conversion(_test_ser_f32(INT16_MIN), _test_ser_i16(INT16_MIN), true);
    test_ser_conversion(_test_ser_f32(INT32_MIN), _test_ser_i16(0), false);
}