#include "hrtf_tests.h"

using namespace std;

namespace TestsNamespace {
    HrtfTest::HrtfTest()
    :   dist (0, 100)
    {
        in.push_back (constructImpulse (-10, 0, 0));
        in.push_back (constructImpulse (10, 0, 0));

        in.push_back (constructImpulse (0, -10, 0));
        in.push_back (constructImpulse (0, 10, 0));

        in.push_back (constructImpulse (0, 0, -10));
        in.push_back (constructImpulse (0, 0, 10));

        in.resize (1000, constructImpulse (0, 0, 0));
    }

    const cl_float3 HrtfTest::mic_pos = {{0, 0, 0}};
    const HrtfConfig HrtfTest::config0 = {(cl_float3) {{0, 0, 1}}, (cl_float3) {{0, 1, 0}}};
    const HrtfConfig HrtfTest::config1 = {(cl_float3) {{1, 0, 0}}, (cl_float3) {{0, 1, 0}}};
    const HrtfConfig HrtfTest::config2 = {(cl_float3) {{0, 0, -1}}, (cl_float3) {{0, 1, 0}}};
    const HrtfConfig HrtfTest::config3 = {(cl_float3) {{-1, 0, 0}}, (cl_float3) {{0, 1, 0}}};

    const std::array <std::array <std::array <cl_float8, 180>, 360>, 2> & HrtfTest::getHrtfData() const
    {
        return HRTF_DATA;
    }

    void HrtfTest::run (const HrtfConfig & config)
    {
        out = attenuate (RaytracerResults (in, mic_pos), config).front();
    }

    Impulse HrtfTest::constructImpulse (float x, float y, float z)
    {
        return (Impulse) {(VolumeType) {{1, 1, 1, 1, 1, 1, 1, 1}}, (cl_float3) {{x, y, z}}, dist (generator)};
    }

    TEST_F(HrtfTest, HrtfConfig0)
    {
        run (config0);
        for (auto i = 0; i != sizeof (VolumeType) / sizeof (float); ++i)
        {
            ASSERT_FLOAT_EQ(HRTF_DATA [0] [180] [90].s [i], out [5].volume.s [i]);
        }
    }
    TEST_F(HrtfTest, HrtfConfig1)
    {
        run (config1);
        for (auto i = 0; i != sizeof (VolumeType) / sizeof (float); ++i)
        {
            ASSERT_FLOAT_EQ(HRTF_DATA [0] [180] [90].s [i], out [1].volume.s [i]);
        }
    }
    TEST_F(HrtfTest, HrtfConfig2)
    {
        run (config2);
        for (auto i = 0; i != sizeof (VolumeType) / sizeof (float); ++i)
        {
            ASSERT_FLOAT_EQ(HRTF_DATA [0] [180] [90].s [i], out [4].volume.s [i]);
        }
    }
    TEST_F(HrtfTest, HrtfConfig3)
    {
        run (config3);
        for (auto i = 0; i != sizeof (VolumeType) / sizeof (float); ++i)
        {
            ASSERT_FLOAT_EQ(HRTF_DATA [0] [180] [90].s [i], out [0].volume.s [i]);
        }
    }
}
