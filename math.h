#ifndef JOT_MATH
#define JOT_MATH

//A file defining basic linear algebra functions working with vectors, matrices and quaternions (TODO)
// This file is self contained.

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

#if !defined(ASSERT) && !defined(JOT_ASSERT)
    #define ASSERT(x) assert(x)
#endif // !ASSERT

#ifndef PI
    #define PI 3.14159265358979323846f
    #define PId 3.14159265358979323846
#endif

#ifndef TAU
    #define TAU (PI*2.0f)
#endif

#ifndef EPSILON
    #define EPSILON 2.0e-5f
#endif

#ifndef JMAPI
    #define JMAPI static inline
#endif

#if defined(_MSC_VER)
    #define ATTRIBUTE_ALIGNED(bytes)            __declspec(align(bytes))
#elif defined(__GNUC__) || defined(__clang__)
    #define ATTRIBUTE_ALIGNED(bytes)            __attribute__((aligned(bytes)))
#else
    #define ATTRIBUTE_ALIGNED(align)            _Alignas(align)
#endif

#ifndef BINIT
    #ifdef __cplusplus
        #define BINIT(Struct) Struct
    #else
        #define BINIT(Struct) (Struct)
    #endif
#endif

typedef union Vec2 {
    struct { float x, y; };
    float floats[2];
} Vec2;

typedef union Vec3 {
    struct { Vec2 xy; float _pad1; };
    struct { float _pad2; Vec2 yz; };
    struct { float x, y, z; };
    float floats[3];
} Vec3;

ATTRIBUTE_ALIGNED(16) typedef union Vec4 {
    struct { Vec3 xyz; float _pad1; };
    struct { float _pad2; Vec3 yzw; };
    struct { Vec2 xy; Vec2 zw; };
    struct { float _pad3; Vec2 yz; float _pad4; };
    struct { float x, y, z, w; };
    float floats[4];
} Vec4;

typedef union iVec2 {
    struct { int x, y; };
    int ints[2];
} iVec2;

typedef union iVec3 {
    struct { iVec2 xy; int _pad1; };
    struct { int _pad2; iVec2 yz; };
    struct { int x, y, z; };
    int ints[3];
} iVec3;

ATTRIBUTE_ALIGNED(16) typedef union iVec4 {
    struct { iVec3 xyz; int _pad1; };
    struct { int _pad2; iVec3 yzw; };
    struct { iVec2 xy; iVec2 zw; };
    struct { int _pad3; iVec2 yz; int _pad4; };
    struct { int x, y, z, w; };
    float ints[4];
} iVec4;

ATTRIBUTE_ALIGNED(16) typedef struct Quat {
    float x;
    float y;
    float z;
    float w;
} Quat;

typedef union Mat2 {
    Vec2 col[2];
    float m[2][2];
    struct {
        float m11, m21;
        float m12, m22;
    };
    float floats[4];
} Mat2;

typedef union Mat3 {
    Vec3 col[3];
    float m[3][3];
    struct {
        float m11, m21, m31;
        float m12, m22, m32;
        float m13, m23, m33;
    };
    float floats[9];
} Mat3;

typedef union Mat4 {
    Vec4 col[4];
    float m[4][4];
    struct {
        float m11, m21, m31, m41;
        float m12, m22, m32, m42;
        float m13, m23, m33, m43;
        float m14, m24, m34, m44;
    };
    float floats[16];
} Mat4;

    
#define vec2(...) BINIT(Vec2){__VA_ARGS__}
#define vec3(...) BINIT(Vec3){__VA_ARGS__}
#define vec4(...) BINIT(Vec4){__VA_ARGS__}
#define ivec2(...) BINIT(iVec2){__VA_ARGS__}
#define ivec3(...) BINIT(iVec3){__VA_ARGS__}
#define ivec4(...) BINIT(iVec4){__VA_ARGS__}

#define m(a, b) a < b ? a : b
#define M(a, b) a > b ? a : b

JMAPI Vec2 vec2_of(float s) { return vec2(s, s); }
JMAPI Vec3 vec3_of(float s) { return vec3(s, s, s); }
JMAPI Vec4 vec4_of(float s) { return vec4(s, s, s, s); }

JMAPI Vec2 vec2_add(Vec2 a, Vec2 b) { return vec2(a.x + b.x, a.y + b.y); }
JMAPI Vec3 vec3_add(Vec3 a, Vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
JMAPI Vec4 vec4_add(Vec4 a, Vec4 b) { return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }

JMAPI Vec2 vec2_sub(Vec2 a, Vec2 b) { return vec2(a.x - b.x, a.y - b.y); }
JMAPI Vec3 vec3_sub(Vec3 a, Vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
JMAPI Vec4 vec4_sub(Vec4 a, Vec4 b) { return vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }

JMAPI Vec2 vec2_scale(Vec2 a, float s) { return vec2(s * a.x, s * a.y); }
JMAPI Vec3 vec3_scale(Vec3 a, float s) { return vec3(s * a.x, s * a.y, s * a.z); }
JMAPI Vec4 vec4_scale(Vec4 a, float s) { return vec4(s * a.x, s * a.y, s * a.z, s * a.w); }

JMAPI float vec2_dot(Vec2 a, Vec2 b) { return a.x*b.x + a.y*b.y; }
JMAPI float vec3_dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
JMAPI float vec4_dot(Vec4 a, Vec4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

JMAPI float vec2_len(Vec2 a) { return sqrtf(vec2_dot(a, a)); }
JMAPI float vec3_len(Vec3 a) { return sqrtf(vec3_dot(a, a)); }
JMAPI float vec4_len(Vec4 a) { return sqrtf(vec4_dot(a, a)); }

JMAPI float vec2_dist(Vec2 a, Vec2 b) { return vec2_len(vec2_sub(a, b)); }
JMAPI float vec3_dist(Vec3 a, Vec3 b) { return vec3_len(vec3_sub(a, b)); }
JMAPI float vec4_dist(Vec4 a, Vec4 b) { return vec4_len(vec4_sub(a, b)); }

JMAPI Vec2 vec2_norm(Vec2 a) { float len = vec2_len(a); return len > 0 ? vec2_scale(a, 1/len) : vec2_of(0); }
JMAPI Vec3 vec3_norm(Vec3 a) { float len = vec3_len(a); return len > 0 ? vec3_scale(a, 1/len) : vec3_of(0); }
JMAPI Vec4 vec4_norm(Vec4 a) { float len = vec4_len(a); return len > 0 ? vec4_scale(a, 1/len) : vec4_of(0); }

JMAPI bool vec2_is_equal(Vec2 a, Vec2 b) { return memcmp(&a, &b, sizeof a) == 0; }
JMAPI bool vec3_is_equal(Vec3 a, Vec3 b) { return memcmp(&a, &b, sizeof a) == 0; }
JMAPI bool vec4_is_equal(Vec4 a, Vec4 b) { return memcmp(&a, &b, sizeof a) == 0; }

JMAPI Vec2 vec2_mul(Vec2 a, Vec2 b) { return vec2(a.x * b.x, a.y * b.y);                                }
JMAPI Vec3 vec3_mul(Vec3 a, Vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z);                     }
JMAPI Vec4 vec4_mul(Vec4 a, Vec4 b) { return vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);          }

JMAPI Vec2 vec2_div(Vec2 a, Vec2 b) { return vec2(a.x / b.x, a.y / b.y);                                }
JMAPI Vec3 vec3_div(Vec3 a, Vec3 b) { return vec3(a.x / b.x, a.y / b.y, a.z / b.z);                     }
JMAPI Vec4 vec4_div(Vec4 a, Vec4 b) { return vec4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w);          }

JMAPI Vec2 vec2_min(Vec2 a, Vec2 b) { return vec2(m(a.x, b.x), m(a.y, b.y));                            }
JMAPI Vec3 vec3_min(Vec3 a, Vec3 b) { return vec3(m(a.x, b.x), m(a.y, b.y), m(a.z, b.z));               }
JMAPI Vec4 vec4_min(Vec4 a, Vec4 b) { return vec4(m(a.x, b.x), m(a.y, b.y), m(a.z, b.z), m(a.w, b.w));  }

JMAPI Vec2 vec2_max(Vec2 a, Vec2 b) { return vec2(M(a.x, b.x), M(a.y, b.y));                            }
JMAPI Vec3 vec3_max(Vec3 a, Vec3 b) { return vec3(M(a.x, b.x), M(a.y, b.y), M(a.z, b.z));               }
JMAPI Vec4 vec4_max(Vec4 a, Vec4 b) { return vec4(M(a.x, b.x), M(a.y, b.y), M(a.z, b.z), M(a.w, b.w));  }

JMAPI Vec2 vec2_clamp(Vec2 clamped, Vec2 low, Vec2 high) { return vec2_max(low, vec2_min(clamped, high)); }
JMAPI Vec3 vec3_clamp(Vec3 clamped, Vec3 low, Vec3 high) { return vec3_max(low, vec3_min(clamped, high)); }
JMAPI Vec4 vec4_clamp(Vec4 clamped, Vec4 low, Vec4 high) { return vec4_max(low, vec4_min(clamped, high)); }

JMAPI Vec2 vec2_lerp(Vec2 a, Vec2 b, float t) { return vec2_add(vec2_scale(a, 1 - t), vec2_scale(b, t)); }
JMAPI Vec3 vec3_lerp(Vec3 a, Vec3 b, float t) { return vec3_add(vec3_scale(a, 1 - t), vec3_scale(b, t)); }
JMAPI Vec4 vec4_lerp(Vec4 a, Vec4 b, float t) { return vec4_add(vec4_scale(a, 1 - t), vec4_scale(b, t)); }

//integer
JMAPI iVec2 ivec2_of(int s) { return ivec2(s, s); }
JMAPI iVec3 ivec3_of(int s) { return ivec3(s, s, s); }
JMAPI iVec4 ivec4_of(int s) { return ivec4(s, s, s, s); }

JMAPI iVec2 ivec2_add(iVec2 a, iVec2 b) { return ivec2(a.x + b.x, a.y + b.y); }
JMAPI iVec3 ivec3_add(iVec3 a, iVec3 b) { return ivec3(a.x + b.x, a.y + b.y, a.z + b.z); }
JMAPI iVec4 ivec4_add(iVec4 a, iVec4 b) { return ivec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }

JMAPI iVec2 ivec2_sub(iVec2 a, iVec2 b) { return ivec2(a.x - b.x, a.y - b.y); }
JMAPI iVec3 ivec3_sub(iVec3 a, iVec3 b) { return ivec3(a.x - b.x, a.y - b.y, a.z - b.z); }
JMAPI iVec4 ivec4_sub(iVec4 a, iVec4 b) { return ivec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }

JMAPI iVec2 ivec2_scale(iVec2 a, int s) { return ivec2(s * a.x, s * a.y); }
JMAPI iVec3 ivec3_scale(iVec3 a, int s) { return ivec3(s * a.x, s * a.y, s * a.z); }
JMAPI iVec4 ivec4_scale(iVec4 a, int s) { return ivec4(s * a.x, s * a.y, s * a.z, s * a.w); }

JMAPI int ivec2_dot(iVec2 a, iVec2 b) { return a.x*b.x + a.y*b.y; }
JMAPI int ivec3_dot(iVec3 a, iVec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
JMAPI int ivec4_dot(iVec4 a, iVec4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

JMAPI bool ivec2_is_equal(iVec2 a, iVec2 b) { return memcmp(&a, &b, sizeof a) == 0; }
JMAPI bool ivec3_is_equal(iVec3 a, iVec3 b) { return memcmp(&a, &b, sizeof a) == 0; }
JMAPI bool ivec4_is_equal(iVec4 a, iVec4 b) { return memcmp(&a, &b, sizeof a) == 0; }

JMAPI iVec2 ivec2_mul(iVec2 a, iVec2 b) { return ivec2(a.x * b.x, a.y * b.y);                                }
JMAPI iVec3 ivec3_mul(iVec3 a, iVec3 b) { return ivec3(a.x * b.x, a.y * b.y, a.z * b.z);                     }
JMAPI iVec4 ivec4_mul(iVec4 a, iVec4 b) { return ivec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);          }

JMAPI iVec2 ivec2_div(iVec2 a, iVec2 b) { return ivec2(a.x / b.x, a.y / b.y);                                }
JMAPI iVec3 ivec3_div(iVec3 a, iVec3 b) { return ivec3(a.x / b.x, a.y / b.y, a.z / b.z);                     }
JMAPI iVec4 ivec4_div(iVec4 a, iVec4 b) { return ivec4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w);          }

JMAPI iVec2 ivec2_min(iVec2 a, iVec2 b) { return ivec2(m(a.x, b.x), m(a.y, b.y));                            }
JMAPI iVec3 ivec3_min(iVec3 a, iVec3 b) { return ivec3(m(a.x, b.x), m(a.y, b.y), m(a.z, b.z));               }
JMAPI iVec4 ivec4_min(iVec4 a, iVec4 b) { return ivec4(m(a.x, b.x), m(a.y, b.y), m(a.z, b.z), m(a.w, b.w));  }

JMAPI iVec2 ivec2_max(iVec2 a, iVec2 b) { return ivec2(M(a.x, b.x), M(a.y, b.y));                            }
JMAPI iVec3 ivec3_max(iVec3 a, iVec3 b) { return ivec3(M(a.x, b.x), M(a.y, b.y), M(a.z, b.z));               }
JMAPI iVec4 ivec4_max(iVec4 a, iVec4 b) { return ivec4(M(a.x, b.x), M(a.y, b.y), M(a.z, b.z), M(a.w, b.w));  }

JMAPI iVec2 ivec2_clamp(iVec2 clamped, iVec2 low, iVec2 high) { return ivec2_max(low, ivec2_min(clamped, high)); }
JMAPI iVec3 ivec3_clamp(iVec3 clamped, iVec3 low, iVec3 high) { return ivec3_max(low, ivec3_min(clamped, high)); }
JMAPI iVec4 ivec4_clamp(iVec4 clamped, iVec4 low, iVec4 high) { return ivec4_max(low, ivec4_min(clamped, high)); }

JMAPI iVec2 ivec2_lerp(iVec2 a, iVec2 b, int t) { return ivec2_add(ivec2_scale(a, 1 - t), ivec2_scale(b, t)); }
JMAPI iVec3 ivec3_lerp(iVec3 a, iVec3 b, int t) { return ivec3_add(ivec3_scale(a, 1 - t), ivec3_scale(b, t)); }
JMAPI iVec4 ivec4_lerp(iVec4 a, iVec4 b, int t) { return ivec4_add(ivec4_scale(a, 1 - t), ivec4_scale(b, t)); }

#undef m
#undef M

//Conversions
JMAPI iVec2 ivec2_from_vec(Vec2 a)    { return ivec2((int)a.x, (int)a.y); }
JMAPI iVec3 ivec3_from_vec(Vec3 a)    { return ivec3((int)a.x, (int)a.y, (int)a.z); }
JMAPI iVec4 ivec4_from_vec(Vec4 a)    { return ivec4((int)a.x, (int)a.y, (int)a.z, (int)a.w); }

JMAPI Vec2 vec2_from_ivec(iVec2 a)    { return vec2((float)a.x, (float)a.y); }
JMAPI Vec3 vec3_from_ivec(iVec3 a)    { return vec3((float)a.x, (float)a.y, (float)a.z); }
JMAPI Vec4 vec4_from_ivec(iVec4 a)    { return vec4((float)a.x, (float)a.y, (float)a.z, (float)a.w); }

JMAPI Vec4 vec4_homo_from_vec3(Vec3 a)  { return vec4(a.x, a.y, a.z, 1); }
JMAPI Vec3 vec3_from_vec4_homo(Vec4 a)  { return vec3(a.x/a.w, a.y/a.w, a.z/a.w); }

JMAPI float to_radiansf(float degrees)
{
    float radians = degrees / 180 * PI;
    return radians;
}

JMAPI float to_degreesf(float radians)
{
    float degrees = radians * 180 / PI;
    return degrees;
}

JMAPI float lerpf(float lo, float hi, float t) 
{
    return lo * (1 - t) + hi * t;
}

JMAPI float remapf(float value, float input_from, float input_to, float output_from, float output_to)
{
    float result = (value - input_from)/(input_to - input_from)*(output_to - output_from) + output_from;
    return result;
}

JMAPI bool is_nearf(float a, float b, float epsilon)
{
    //this form guarantees that is_nearf(NAN, NAN, 1) == true
    return !(fabsf(a - b) > epsilon);
}

//Returns true if x and y are within epsilon distance of each other.
//If |x| and |y| are less than 1 uses epsilon directly
//else scales epsilon to account for growing floating point inaccuracy
JMAPI bool is_near_scaledf(float x, float y, float epsilon)
{
    //This is the form that produces the best assembly
    float calced_factor = fabsf(x) + fabsf(y);
    float factor = 2 > calced_factor ? 2 : calced_factor;
    return is_nearf(x, y, factor * epsilon / 2);
}

JMAPI bool vec2_is_near(Vec2 a, Vec2 b, float epsilon) 
{
    return is_nearf(a.x, b.x, epsilon) 
        && is_nearf(a.y, b.y, epsilon);
}

JMAPI bool vec3_is_near(Vec3 a, Vec3 b, float epsilon) 
{
    return is_nearf(a.x, b.x, epsilon) 
        && is_nearf(a.y, b.y, epsilon) 
        && is_nearf(a.z, b.z, epsilon);
}

JMAPI bool vec4_is_near(Vec4 a, Vec4 b, float epsilon) 
{
    return is_nearf(a.x, b.x, epsilon) 
        && is_nearf(a.y, b.y, epsilon) 
        && is_nearf(a.z, b.z, epsilon) 
        && is_nearf(a.w, b.w, epsilon);
}

JMAPI bool vec2_is_near_scaled(Vec2 a, Vec2 b, float epsilon) 
{
    return is_near_scaledf(a.x, b.x, epsilon) 
        && is_near_scaledf(a.y, b.y, epsilon);
}

JMAPI bool vec3_is_near_scaled(Vec3 a, Vec3 b, float epsilon) 
{
    return is_near_scaledf(a.x, b.x, epsilon) 
        && is_near_scaledf(a.y, b.y, epsilon) 
        && is_near_scaledf(a.z, b.z, epsilon);
}

JMAPI bool vec4_is_near_scaled(Vec4 a, Vec4 b, float epsilon) 
{
    return is_near_scaledf(a.x, b.x, epsilon) 
        && is_near_scaledf(a.y, b.y, epsilon) 
        && is_near_scaledf(a.z, b.z, epsilon) 
        && is_near_scaledf(a.w, b.w, epsilon);
}

JMAPI Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

JMAPI float vec2_angle_between(Vec2 a, Vec2 b)
{
    float len2a = vec2_dot(a, a);
    float len2b = vec2_dot(b, b);
    float den = sqrtf(len2a*len2b);
    float num = vec2_dot(a, b);
    float result = acosf(num/den);

    return result;
}

JMAPI float vec3_angle_between(Vec3 a, Vec3 b)
{
    //@NOTE: this implementation is a lot more accurate than the one above
    //source: raymath.h
    Vec3 crossed = vec3_cross(a, b);
    float cross_len = vec3_len(crossed);
    float dotted = vec3_dot(a, b);
    float result = atan2f(cross_len, dotted);

    return result;
}

JMAPI float slerpf_coeficient(float t, float arc_angle)
{
    float coef = sinf(t*arc_angle)/sinf(arc_angle);
    return coef;
}

//Spherical lerp. Arc angle needs to be the angle between from and to with respect to some position.
JMAPI Vec3 vec3_slerp(Vec3 from, Vec3 to, float arc_angle, float t)
{
    Vec3 from_portion = vec3_scale(from, slerpf_coeficient(1.0f - t, arc_angle));
    Vec3 to_portion = vec3_scale(to, slerpf_coeficient(t, arc_angle));
    Vec3 result = vec3_add(from_portion, to_portion);
    return result;
}

JMAPI Vec3 vec3_slerp_around(Vec3 from, Vec3 to, Vec3 center, float t)
{
    Vec3 from_center = vec3_sub(from, center);
    Vec3 to_center = vec3_sub(to, center);
    float arc_angle = vec3_angle_between(from_center, to_center);
    Vec3 result = vec3_slerp(from, to, arc_angle, t);
    return result;
}

//Returns the maximum compoment of a vector.
//This is also the maximum norm
JMAPI float vec3_max_len(Vec3 vec)
{
    float x = fabsf(vec.x);
    float y = fabsf(vec.y);
    float z = fabsf(vec.z);

    float max1 = x > y ? x : y;
    float max = max1 > z ? max1 : z;

    return max;
}

//Normalize vector using the maximum norm.
JMAPI Vec3 vec3_max_norm(Vec3 vec)
{
    return vec3_scale(vec, 1.0f / vec3_max_len(vec));
}

//Constructs a mat4 by entries in writing order
//Calling this function as mat4(1, 2, 3, 4, ...)
//result in the first ROW from the matrix being 1, 2, 3, 4
//while doint Mat4 mat = {1, 2, 3, 4} reuslts in the first 
//COLUMN being 1, 2, 3, 4
JMAPI Mat4 mat4(
        float m11, float m12, float m13, float m14,
        float m21, float m22, float m23, float m24,
        float m31, float m32, float m33, float m34,
        float m41, float m42, float m43, float m44)
{
    Mat4 mat = {{
        {m11, m21, m31, m41},
        {m12, m22, m32, m42},
        {m13, m23, m33, m43},
        {m14, m24, m34, m44},
    }};        

    return mat;
}

JMAPI Mat4 mat4_from_mat3(Mat3 m)
{
    Mat4 r = {0};
    r.m11 = m.m11; r.m12 = m.m12; r.m13 = m.m13; 
    r.m21 = m.m21; r.m22 = m.m22; r.m23 = m.m23; 
    r.m31 = m.m31; r.m32 = m.m32; r.m33 = m.m33; 
    return r;
}

JMAPI Mat3 mat3_from_mat4(Mat4 m)
{
    Mat3 r = {0};
    r.m11 = m.m11; r.m12 = m.m12; r.m13 = m.m13; 
    r.m21 = m.m21; r.m22 = m.m22; r.m23 = m.m23; 
    r.m31 = m.m31; r.m32 = m.m32; r.m33 = m.m33; 
    return r;
}

JMAPI Vec4 mat4_mul_vec4(Mat4 mat, Vec4 vec)
{
    Vec4 result = {0};
    result.x = mat.m11*vec.x + mat.m12*vec.y + mat.m13*vec.z + mat.m14*vec.w;
    result.y = mat.m21*vec.x + mat.m22*vec.y + mat.m23*vec.z + mat.m24*vec.w;
    result.z = mat.m31*vec.x + mat.m32*vec.y + mat.m33*vec.z + mat.m34*vec.w;
    result.w = mat.m41*vec.x + mat.m42*vec.y + mat.m43*vec.z + mat.m44*vec.w;
    return result;
}

JMAPI Vec3 mat4_mul_vec3(Mat4 mat, Vec3 vec)
{
    Vec3 result = {0};
    result.x = mat.m11*vec.x + mat.m12*vec.y + mat.m13*vec.z;
    result.y = mat.m21*vec.x + mat.m22*vec.y + mat.m23*vec.z;
    result.z = mat.m31*vec.x + mat.m32*vec.y + mat.m33*vec.z;
    return result;
}

//interprets the Vec3 as vector of homogenous coordinates Vec4 
//multiplies it with matrix and then returns back the homogenous cordinates normalized result
//as Vec3
JMAPI Vec3 mat4_apply(Mat4 mat, Vec3 vec)
{
    Vec4 homo = vec4_homo_from_vec3(vec);
    Vec4 muled = mat4_mul_vec4(mat, homo);
    Vec3 result = vec3_from_vec4_homo(muled);
    return result;
}

JMAPI Vec4 mat4_col(Mat4 matrix, int64_t column_i) 
{ 
    return matrix.col[column_i]; 
}

JMAPI Vec4 mat4_row(Mat4 matrix, int64_t row_i) 
{ 
    Vec4 result = {matrix.m[0][row_i], matrix.m[1][row_i], matrix.m[2][row_i], matrix.m[3][row_i]};
    return result;
}

JMAPI Mat4 mat4_add(Mat4 a, Mat4 b)
{
    Mat4 result = {0};
    result.col[0] = vec4_add(a.col[0], b.col[0]);
    result.col[1] = vec4_add(a.col[1], b.col[1]);
    result.col[2] = vec4_add(a.col[2], b.col[2]);
    result.col[3] = vec4_add(a.col[3], b.col[3]);

    return result;
}

JMAPI Mat4 mat4_sub(Mat4 a, Mat4 b)
{
    Mat4 result = {0};
    result.col[0] = vec4_sub(a.col[0], b.col[0]);
    result.col[1] = vec4_sub(a.col[1], b.col[1]);
    result.col[2] = vec4_sub(a.col[2], b.col[2]);
    result.col[3] = vec4_sub(a.col[3], b.col[3]);

    return result;
}

JMAPI Mat4 mat4_scale(Mat4 mat, float scalar)
{
    Mat4 result = {0};
    result.col[0] = vec4_scale(mat.col[0], scalar);
    result.col[1] = vec4_scale(mat.col[1], scalar);
    result.col[2] = vec4_scale(mat.col[2], scalar);
    result.col[3] = vec4_scale(mat.col[3], scalar);

    return result;
}

JMAPI Mat4 mat4_mul(Mat4 a, Mat4 b)
{
    Mat4 result = {0};
    result.col[0] = mat4_mul_vec4(a, b.col[0]);
    result.col[1] = mat4_mul_vec4(a, b.col[1]);
    result.col[2] = mat4_mul_vec4(a, b.col[2]);
    result.col[3] = mat4_mul_vec4(a, b.col[3]);

    return result;
}

JMAPI bool mat4_is_equal(Mat4 a, Mat4 b) 
{
    return memcmp(&a, &b, sizeof a) == 0;
}

JMAPI bool mat4_is_near(Mat4 a, Mat4 b, float epsilon) 
{
    for(int i = 0; i < 4*4; i++)
        if(is_nearf(a.floats[i], b.floats[i], epsilon) == false)
            return false;

    return true;
}

JMAPI bool mat4_is_near_scaled(Mat4 a, Mat4 b, float epsilon) 
{
    for(int i = 0; i < 4*4; i++)
        if(is_near_scaledf(a.floats[i], b.floats[i], epsilon) == false)
            return false;

    return true;
}

JMAPI Mat4 mat4_cols(Vec4 col1, Vec4 col2, Vec4 col3, Vec4 col4)
{
    Mat4 result = {{col1, col2, col3, col4}};
    return result;
}

JMAPI Mat4 mat4_rows(Vec4 row1, Vec4 row2, Vec4 row3, Vec4 row4)
{
    Mat4 result = mat4(
        row1.x, row1.y, row1.z, row1.w, 
        row2.x, row2.y, row2.z, row2.w, 
        row3.x, row3.y, row3.z, row3.w, 
        row4.x, row4.y, row4.z, row4.w
    );

    return result;
}

JMAPI Mat4 mat4_inverse(Mat4 matrix)
{
    float M[4][4] = {0};
    float s[6] = {0};
	float c[6] = {0};
    
    ASSERT(sizeof(M) == sizeof(matrix));
    memcpy(M, &matrix, sizeof(matrix));

	s[0] = M[0][0]*M[1][1] - M[1][0]*M[0][1];
	s[1] = M[0][0]*M[1][2] - M[1][0]*M[0][2];
	s[2] = M[0][0]*M[1][3] - M[1][0]*M[0][3];
	s[3] = M[0][1]*M[1][2] - M[1][1]*M[0][2];
	s[4] = M[0][1]*M[1][3] - M[1][1]*M[0][3];
	s[5] = M[0][2]*M[1][3] - M[1][2]*M[0][3];

	c[0] = M[2][0]*M[3][1] - M[3][0]*M[2][1];
	c[1] = M[2][0]*M[3][2] - M[3][0]*M[2][2];
	c[2] = M[2][0]*M[3][3] - M[3][0]*M[2][3];
	c[3] = M[2][1]*M[3][2] - M[3][1]*M[2][2];
	c[4] = M[2][1]*M[3][3] - M[3][1]*M[2][3];
	c[5] = M[2][2]*M[3][3] - M[3][2]*M[2][3];
	
    float determinant = s[0]*c[5]-s[1]*c[4]+s[2]*c[3]+s[3]*c[2]-s[4]*c[1]+s[5]*c[0];
    if(determinant == 0)
    {
        Mat4 null = {0};
        return null;
    }

    ASSERT(determinant > 0 && "determinant must not be 0!");
	float invdet = 1.0f/determinant;
	
    Mat4 result = {0};
	result.m[0][0] = ( M[1][1] * c[5] - M[1][2] * c[4] + M[1][3] * c[3]) * invdet;
	result.m[0][1] = (-M[0][1] * c[5] + M[0][2] * c[4] - M[0][3] * c[3]) * invdet;
	result.m[0][2] = ( M[3][1] * s[5] - M[3][2] * s[4] + M[3][3] * s[3]) * invdet;
	result.m[0][3] = (-M[2][1] * s[5] + M[2][2] * s[4] - M[2][3] * s[3]) * invdet;

	result.m[1][0] = (-M[1][0] * c[5] + M[1][2] * c[2] - M[1][3] * c[1]) * invdet;
	result.m[1][1] = ( M[0][0] * c[5] - M[0][2] * c[2] + M[0][3] * c[1]) * invdet;
	result.m[1][2] = (-M[3][0] * s[5] + M[3][2] * s[2] - M[3][3] * s[1]) * invdet;
	result.m[1][3] = ( M[2][0] * s[5] - M[2][2] * s[2] + M[2][3] * s[1]) * invdet;

	result.m[2][0] = ( M[1][0] * c[4] - M[1][1] * c[2] + M[1][3] * c[0]) * invdet;
	result.m[2][1] = (-M[0][0] * c[4] + M[0][1] * c[2] - M[0][3] * c[0]) * invdet;
	result.m[2][2] = ( M[3][0] * s[4] - M[3][1] * s[2] + M[3][3] * s[0]) * invdet;
	result.m[2][3] = (-M[2][0] * s[4] + M[2][1] * s[2] - M[2][3] * s[0]) * invdet;

	result.m[3][0] = (-M[1][0] * c[3] + M[1][1] * c[1] - M[1][2] * c[0]) * invdet;
	result.m[3][1] = ( M[0][0] * c[3] - M[0][1] * c[1] + M[0][2] * c[0]) * invdet;
	result.m[3][2] = (-M[3][0] * s[3] + M[3][1] * s[1] - M[3][2] * s[0]) * invdet;
	result.m[3][3] = ( M[2][0] * s[3] - M[2][1] * s[1] + M[2][2] * s[0]) * invdet;

    return result;
}

JMAPI Mat3 mat3_inverse(Mat3 matrix) 
{
    float M[3][3] = {0};
    ASSERT(sizeof(M) == sizeof(matrix));
    memcpy(M, &matrix, sizeof(matrix));

	float determinant = +M[0][0] * (M[1][1] * M[2][2] -M[2][1] * M[1][2])
                        -M[0][1] * (M[1][0] * M[2][2] -M[1][2] * M[2][0])
                        +M[0][2] * (M[1][0] * M[2][1] -M[1][1] * M[2][0]);

    if(determinant == 0)
    {
        Mat3 null = {0};
        return null;
    }
    float invdet = 1/determinant;
    Mat3 result = {0};
    result.m[0][0] =  (M[1][1] * M[2][2] - M[2][1] * M[1][2]) * invdet;
    result.m[1][0] = -(M[0][1] * M[2][2] - M[0][2] * M[2][1]) * invdet;
    result.m[2][0] =  (M[0][1] * M[1][2] - M[0][2] * M[1][1]) * invdet;
    result.m[0][1] = -(M[1][0] * M[2][2] - M[1][2] * M[2][0]) * invdet;
    result.m[1][1] =  (M[0][0] * M[2][2] - M[0][2] * M[2][0]) * invdet;
    result.m[2][1] = -(M[0][0] * M[1][2] - M[1][0] * M[0][2]) * invdet;
    result.m[0][2] =  (M[1][0] * M[2][1] - M[2][0] * M[1][1]) * invdet;
    result.m[1][2] = -(M[0][0] * M[2][1] - M[2][0] * M[0][1]) * invdet;
    result.m[2][2] =  (M[0][0] * M[1][1] - M[1][0] * M[0][1]) * invdet;
    
    return result;
}

JMAPI Mat4 mat4_identity() 
{
	return mat4(
		 1,  0,  0,  0,
		 0,  1,  0,  0,
		 0,  0,  1,  0,
		 0,  0,  0,  1
	);
}

JMAPI Mat4 mat4_diagonal(Vec4 vec)
{
	float x = vec.x;
    float y = vec.y;
    float z = vec.z;
    float w = vec.w;
	return mat4(
		 x,  0,  0,  0,
		 0,  y,  0,  0,
		 0,  0,  z,  0,
		 0,  0,  0,  w
	);
}

JMAPI Mat4 mat4_scaling(Vec3 scale) 
{
	float x = scale.x;
    float y = scale.y;
    float z = scale.z;
	return mat4(
		 x,  0,  0,  0,
		 0,  y,  0,  0,
		 0,  0,  z,  0,
		 0,  0,  0,  1
	);
}

JMAPI Mat4 mat4_translation(Vec3 offset) 
{
	return mat4(
		 1,  0,  0,  offset.x,
		 0,  1,  0,  offset.y,
		 0,  0,  1,  offset.z,
		 0,  0,  0,  1
	);
}


JMAPI Mat4 mat4_rotation_x(float angle_in_rad) 
{
	float s = sinf(angle_in_rad);
    float c = cosf(angle_in_rad);
	return mat4(
		1,  0,  0,  0,
		0,  c, -s,  0,
		0,  s,  c,  0,
		0,  0,  0,  1
	);
}

JMAPI Mat4 mat4_rotation_y(float angle_in_rad) 
{
	float s = sinf(angle_in_rad);
    float c = cosf(angle_in_rad);
	return mat4(
		 c,  0,  s,  0,
		 0,  1,  0,  0,
		-s,  0,  c,  0,
		 0,  0,  0,  1
	);
}

JMAPI Mat4 mat4_rotation_z(float angle_in_rad) 
{
	float s = sinf(angle_in_rad);
    float c = cosf(angle_in_rad);
	return mat4(
		 c, -s,  0,  0,
		 s,  c,  0,  0,
		 0,  0,  1,  0,
		 0,  0,  0,  1
	);
}

JMAPI Mat4 mat4_transpose(Mat4 matrix) 
{
	return mat4(
        matrix.m11, matrix.m21, matrix.m31, matrix.m41,
        matrix.m12, matrix.m22, matrix.m32, matrix.m42,
        matrix.m13, matrix.m23, matrix.m33, matrix.m43,
        matrix.m14, matrix.m24, matrix.m34, matrix.m44
	);
}


JMAPI Mat4 mat4_rotation(Vec3 axis, float radians)
{
	Vec3 normalized_axis = vec3_norm(axis);
	float x = normalized_axis.x, y = normalized_axis.y, z = normalized_axis.z;
	float c = cosf(radians), s = sinf(radians);
	
	return mat4(
		c + x*x*(1-c),            x*y*(1-c) - z*s,      x*z*(1-c) + y*s,  0,
		    y*x*(1-c) + z*s,  c + y*y*(1-c),            y*z*(1-c) - x*s,  0,
		    z*x*(1-c) - y*s,      z*y*(1-c) + x*s,  c + z*z*(1-c),        0,
		    0,                        0,                    0,            1
	);
}

//@NOTE: the application order is reverse from glm!
//this means rotate(translate(mat, ...), ...)
// glm:  first rotates and then translates
// here: first translates and then rotatets!
JMAPI Mat4 mat4_translate(Mat4 matrix, Vec3 offset)
{
    Mat4 translation = mat4_translation(offset);
    Mat4 result = mat4_mul(translation, matrix);
    return result;
}

JMAPI Mat4 mat4_rotate(Mat4 matrix, Vec3 axis, float radians)
{
    Mat4 rotation = mat4_rotation(axis, radians);
    Mat4 result = mat4_mul(rotation, matrix);
    return result;
}

JMAPI Mat4 mat4_scale_affine(Mat4 mat, Vec3 scale_by)
{
    Mat4 scaling = mat4_scaling(scale_by);
    Mat4 result = mat4_mul(scaling, mat);
    return result;
}

JMAPI Mat4 mat4_inverse_affine(Mat4 matrix) 
{
    //taken from: https://github.com/arkanis/single-header-file-c-libs/blob/master/math_3d.h#L235

	// Create shorthands to access matrix members
	float m00 = matrix.m11,  m10 = matrix.m21,  m20 = matrix.m31,  m30 = matrix.m41;
	float m01 = matrix.m12,  m11 = matrix.m22,  m21 = matrix.m32,  m31 = matrix.m42;
	float m02 = matrix.m13,  m12 = matrix.m23,  m22 = matrix.m33,  m32 = matrix.m43;
	
	// Invert 3x3 part of the 4x4 matrix that contains the rotation, etc.
	// That part is called R from here on.
		
		// Calculate cofactor matrix of R
		float c00 =   m11*m22 - m12*m21,   c10 = -(m01*m22 - m02*m21),  c20 =   m01*m12 - m02*m11;
		float c01 = -(m10*m22 - m12*m20),  c11 =   m00*m22 - m02*m20,   c21 = -(m00*m12 - m02*m10);
		float c02 =   m10*m21 - m11*m20,   c12 = -(m00*m21 - m01*m20),  c22 =   m00*m11 - m01*m10;
		
		// Caclculate the determinant by using the already calculated determinants
		// in the cofactor matrix.
		// Second sign is already minus from the cofactor matrix.
		float det = m00*c00 + m10*c10 + m20 * c20;
		if (det == 0)
			return mat4_identity();
		
		// Calcuate inverse of R by dividing the transposed cofactor matrix by the
		// determinant.
		float i00 = c00 / det,  i10 = c01 / det,  i20 = c02 / det;
		float i01 = c10 / det,  i11 = c11 / det,  i21 = c12 / det;
		float i02 = c20 / det,  i12 = c21 / det,  i22 = c22 / det;
	
	// Combine the inverted R with the inverted translation
	return mat4(
		i00, i10, i20,  -(i00*m30 + i10*m31 + i20*m32),
		i01, i11, i21,  -(i01*m30 + i11*m31 + i21*m32),
		i02, i12, i22,  -(i02*m30 + i12*m31 + i22*m32),
		0,   0,   0,      1
	);
}

JMAPI Mat4 mat4_inverse_nonuniform_scale(Mat4 mat)
{
    Mat4 upper = mat4(
        mat.m11, mat.m12, mat.m13, 0, 
        mat.m21, mat.m22, mat.m23, 0, 
        mat.m31, mat.m32, mat.m33, 0, 
        0,       0,       0,       1
    );

    Mat4 inv_upper = mat4_inverse(upper);
    Mat4 normal_matrix = mat4_transpose(inv_upper);
    return normal_matrix;
}

//Makes a perspective projection matrix so that the output is in ranage [-1, 1] in all dimensions (OpenGL standard)
JMAPI Mat4 mat4_perspective_projection(float fov_radians, float width_over_height, float near, float far) 
{ 
    ASSERT(fov_radians != 0);
    ASSERT(near != far);
    ASSERT(width_over_height != 0);

    //https://ogldev.org/www/tutorial12/tutorial12.html
	float fo = 1.0f / tanf(fov_radians / 2.0f);
	float ar = width_over_height, n = near, f = far;
	Mat4 result = mat4(
		 fo / ar,     0,           0,            0,
		 0,           fo,          0,            0,
		 0,           0,           (-f-n)/(n-f), (2*f*n)/(n-f),
		 0,           0,           1,            0
	);
    return result;
} 

JMAPI Mat4 mat4_ortographic_projection(float bottom, float top, float left, float right, float near, float far) 
{
    ASSERT(bottom != top);
    ASSERT(left != right);
    ASSERT(near != far);

    float l = left, r = right, b = bottom, t = top, n = near, f = far;
	float tx = -(r + l) / (r - l);
	float ty = -(t + b) / (t - b);
	float tz = -(f + n) / (f - n);
	Mat4 result = mat4(
		 2 / (r - l),  0,            0,            tx,
		 0,            2 / (t - b),  0,            ty,
		 0,            0,            2 / (f - n),  tz,
		 0,            0,            0,            1
	);

    return result;
}

JMAPI Mat4 mat4_local_matrix(Vec3 x_dir, Vec3 y_dir, Vec3 position)
{
    Vec3 X = vec3_norm(x_dir);
    Vec3 Z = vec3_norm(vec3_cross(x_dir, y_dir));
    Vec3 Y = vec3_cross(Z, X);

    Mat4 local = mat4(
        X.x, Y.x, Z.x, position.x,
        X.y, Y.y, Z.y, position.y,
        X.z, Y.z, Z.z, position.z,
        0,   0,   0,   1
    );

    return local;
}

JMAPI Mat4 mat4_look_at(Vec3 camera_pos, Vec3 camera_target, Vec3 camera_up_dir)
{
    const Vec3 front_dir = vec3_sub(camera_target, camera_pos);
    const Vec3 n = vec3_norm(front_dir);
    const Vec3 u = vec3_norm(vec3_cross(front_dir, camera_up_dir));
    const Vec3 v = vec3_cross(u, n);

    return mat4(
        u.x, u.y, u.z, -vec3_dot(camera_pos, u), 
        v.x, v.y, v.z, -vec3_dot(camera_pos, v), 
        n.x, n.y, n.z, -vec3_dot(camera_pos, n), 
        0,   0,   0,   1
    );
}

#endif
