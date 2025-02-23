#pragma once

#include "../math.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef TEST
    #define TEST(x, ...) (!(x) ? (fprintf(stderr, "TEST(" #x ") failed. " __VA_ARGS__), abort()) : (void) 0)
#endif // !TEST

#define TEST_MATH_EPSILON 4.0e-5f

#define TEST_NEAR_FLOAT(a, b, msg) TEST(is_near_scaledf(a, b, TEST_MATH_EPSILON), msg)
#define TEST_NEAR_VEC3(a, b, msg) TEST(vec3_is_near_scaled(a, b, TEST_MATH_EPSILON), msg)

static int compare_near_scaledf(float a, float b)
{
    if(is_near_scaledf(a, b, TEST_MATH_EPSILON))
        return 0;
    else if(a <= b)
        return -1;
    else
        return 1;
}

static void test_vec3_identities(Vec3 a, Vec3 b)
{
    //No zero vectors please!
    ASSERT(vec3_len(a) != 0.0f);
    ASSERT(vec3_len(b) != 0.0f);

    TEST_NEAR_VEC3(vec3_add(a, b), vec3_add(b, a), "Addition is symetric");

    TEST_NEAR_FLOAT(vec3_dot(a, b), vec3_dot(b, a), "Dot product is symetric");
    
    TEST_NEAR_FLOAT(vec3_dot(a, a), vec3_len(a)*vec3_len(a), "Length should be correct");

    TEST_NEAR_FLOAT(vec3_len(vec3_norm(a)), 1, "size of normalized vector must be 1");
    TEST_NEAR_FLOAT(vec3_len(vec3(0, 0, 0)), 0, "size of zero vector must be 0");
    TEST_NEAR_FLOAT(vec3_len(vec3_sub(a, a)), 0, "Cancelation should produce correct lentgh");

    TEST_NEAR_VEC3(vec3_cross(a, b), vec3_scale(vec3_cross(b, a), -1), "Cross product is antisymtric");

    const Vec3 n = vec3_norm(a);
    const Vec3 u = vec3_norm(vec3_cross(a, b));
    const Vec3 v = vec3_cross(n, u);

    float x = vec3_dot(n, u);
    if(is_near_scaledf(x, 0.0f, TEST_MATH_EPSILON) == false)
    {
        int z = 0;
        (void) z;
    }
    //@NOTE: this check requires larger epsilon even if scaled
    float large_epsilon = TEST_MATH_EPSILON*5;
    TEST(is_near_scaledf(vec3_dot(n, u), 0.0f, large_epsilon), "Ortogonalization shoould produce ortogonal vectors");
    TEST(is_near_scaledf(vec3_dot(n, v), 0.0f, large_epsilon), "Ortogonalization shoould produce ortogonal vectors");
    TEST(is_near_scaledf(vec3_dot(v, u), 0.0f, large_epsilon), "Ortogonalization shoould produce ortogonal vectors");

    TEST_NEAR_FLOAT(vec3_angle_between(a, a), 0, "Angle between the same vector should be 0");
    TEST_NEAR_FLOAT(vec3_angle_between(n, u), PI/2, "Angle between should measure ortogonal correctly");
    TEST_NEAR_FLOAT(vec3_angle_between(n, v), PI/2, "Angle between should measure ortogonal correctly");
    TEST_NEAR_FLOAT(vec3_angle_between(v, u), PI/2, "Angle between should measure ortogonal correctly");

    const float a_len = vec3_len(a);
    const float b_len = vec3_len(b);
    {
        const float angle_l = vec3_angle_between(a, b);
        const float angle_r = vec3_angle_between(vec3_scale(a, b_len*b_len), b);
        (void) angle_l;
        (void) angle_r;

        TEST_NEAR_FLOAT(vec3_angle_between(a, b), vec3_angle_between(vec3_scale(a, b_len*b_len), b), "Angle should be size independent");
    }

    const Vec3 scaled_n = vec3_scale(n, a_len);
    const Vec3 scaled_u = vec3_scale(u, b_len*2);

    const float n_len = vec3_len(scaled_n);
    const float u_len = vec3_len(scaled_u);
    const float add_len = vec3_len(vec3_sub(scaled_n, scaled_u));
    TEST(is_near_scaledf(add_len*add_len, n_len*n_len + u_len*u_len, large_epsilon), "Adding ortogonal vectors should obey pythagoras theorem");

    {
        float schwarz_l = vec3_dot(a, b)*vec3_dot(a, b);
        float schwarz_r = vec3_dot(a, a)*vec3_dot(b, b);

        TEST(compare_near_scaledf(schwarz_l, schwarz_r) <= 0, "Schwarz inequality must hold");
    }
    
    {
        const Vec3 e1 = {1, 0, 0};
        const Vec3 e2 = {0, 1, 0};
        const Vec3 e3 = {0, 0, 1};
        #define SQR(a) ((a)*(a))

        float bessel_l1 = SQR(vec3_dot(a, e1)) + SQR(vec3_dot(a, e2)) + SQR(vec3_dot(a, e3));
        float bessel_r1 = vec3_len(a)*vec3_len(a);
        TEST(compare_near_scaledf(bessel_l1, bessel_r1) <= 0, "Bessel's inequality must hold");

        float bessel_l2 = SQR(vec3_dot(a, n)) + SQR(vec3_dot(a, u)) + SQR(vec3_dot(a, v));
        float bessel_r2 = vec3_len(a)*vec3_len(a);
        TEST(compare_near_scaledf(bessel_l2, bessel_r2) <= 0, "Bessel's inequality must hold with any OG sequence of basis vectors");

        #undef SQR
    }
}

static float _test_math_random_f()
{
    //we stich multiple randoms together to get more bits of randomness
    typedef long long ll;
    ll r1 = rand();
    ll r2 = rand();
    ll r3 = rand();
    ll RMAX = RAND_MAX;

    ll random_value = r1 + r2*RMAX + r3*RMAX*RMAX;

    float out = (float) random_value / (float) (RMAX*RMAX*RMAX);
    return out;
}

static float _test_math_random_big_f()
{
    float fraction = _test_math_random_f();
    return fraction * 1000.0f;
}

static void test_math(double max_seconds)
{
    srand((unsigned) clock());
    double start = (double) clock() / (double) CLOCKS_PER_SEC;
    while(true)
    {
        double now = (double) clock() / (double) CLOCKS_PER_SEC;
        if(now - start > max_seconds)
            break;

        Vec3 a = {0};
        Vec3 b = {0};

        a.x = _test_math_random_big_f();
        a.y = _test_math_random_big_f();
        a.z = _test_math_random_big_f();
        
        b.x = _test_math_random_big_f();
        b.y = _test_math_random_big_f();
        b.z = _test_math_random_big_f();

        test_vec3_identities(a, b);
    }
}