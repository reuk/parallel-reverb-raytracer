#include "rayverb.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include "gtest/gtest.h"

#include <vector>
#include <random>

namespace TestsNamespace {
    using namespace std;

    class RaytracerTest: public Raytracer, public ::testing::Test
    {
    protected:
        RaytracerTest();

        default_random_engine generator;
        uniform_real_distribution <float> dist;

        vector <cl_float3> directions;
        static constexpr cl_float3 mic_pos = {{0, 2, 0}};
        static constexpr cl_float3 src_pos = {{0, 2, 2}};
        static const auto NUM_REFLECTIONS = 128;

        static void test_eq (const cl_float3 & a, const cl_float3 & b);
    };

    TEST_F(RaytracerTest, ImpulseDirections)
    {
        raytrace (mic_pos, src_pos, directions);
        auto diffuse = getRawDiffuse().impulses;

        test_eq (diffuse [0 * NUM_REFLECTIONS + 0].position, {{0, 2, -27}});
        test_eq (diffuse [1 * NUM_REFLECTIONS + 0].position, {{0, 2, 27}});
        test_eq (diffuse [2 * NUM_REFLECTIONS + 0].position, {{0, 0, 2}});
        test_eq (diffuse [3 * NUM_REFLECTIONS + 0].position, {{0, 27, 2}});
        test_eq (diffuse [4 * NUM_REFLECTIONS + 0].position, {{-25, 2, 2}});
        test_eq (diffuse [5 * NUM_REFLECTIONS + 0].position, {{25, 2, 2}});

        test_eq (diffuse [0 * NUM_REFLECTIONS + 1].position, {{0, 0, 0}});
        test_eq (diffuse [1 * NUM_REFLECTIONS + 1].position, {{0, 0, 0}});
        test_eq (diffuse [2 * NUM_REFLECTIONS + 1].position, {{0, 27, 2}});
        test_eq (diffuse [3 * NUM_REFLECTIONS + 1].position, {{0, 0, 2}});
        test_eq (diffuse [4 * NUM_REFLECTIONS + 1].position, {{-25, 2, -2}});
        test_eq (diffuse [5 * NUM_REFLECTIONS + 1].position, {{25, 2, -2}});
    }
}

