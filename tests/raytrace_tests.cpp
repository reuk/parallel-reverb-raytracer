#include "raytrace_tests.h"

const cl_float3 TestsNamespace::RaytracerTest::mic_pos;
const cl_float3 TestsNamespace::RaytracerTest::src_pos;

TestsNamespace::RaytracerTest::RaytracerTest()
:   Raytracer (NUM_REFLECTIONS, "../../assets/test_models/large_square.obj", "../../assets/mat.json")
{
    directions.push_back ({{ 0,  0, -1}});
    directions.push_back ({{ 0,  0,  1}});
    directions.push_back ({{ 0, -1,  0}});
    directions.push_back ({{ 0,  1,  0}});
    directions.push_back ({{-1,  0,  0}});
    directions.push_back ({{ 1,  0,  0}});

    directions.resize (64 * 1000, {{0, 0, -1}});
}

void TestsNamespace::RaytracerTest::test_eq (const cl_float3 & a, const cl_float3 & b)
{
    for (auto i = 0; i != 3; ++i)
    {
        ASSERT_FLOAT_EQ(a.s [i], b.s [i]);
    }
}
