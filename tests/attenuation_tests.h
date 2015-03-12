#include "rayverb.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include "gtest/gtest.h"

#include <vector>
#include <random>

namespace TestsNamespace {
    using namespace std;

    class AttenuationTest: public SpeakerAttenuator, public ::testing::Test
    {
    protected:
        AttenuationTest()
        :   dist (0, 100)
        {
            in.push_back (constructImpulse (-10, 0, 0));
            in.push_back (constructImpulse (10, 0, 0));

            in.push_back (constructImpulse (0, -10, 0));
            in.push_back (constructImpulse (0, 10, 0));

            in.push_back (constructImpulse (0, 0, -10));
            in.push_back (constructImpulse (0, 0, 10));

            in.resize (1024 * 64, constructImpulse (0, 0, 0));
        }

        virtual ~AttenuationTest()
        {

        }

        void run (const Speaker & speaker)
        {
            out = attenuate (RaytracerResults (in, mic_pos), {speaker}).front();
            for (const auto & j : out)
                for (auto i = 1; i != sizeof (VolumeType) / sizeof (float); ++i)
                    ASSERT_FLOAT_EQ(j.volume.s [0], j.volume.s [i]);
        }

        static constexpr cl_float3 mic_pos = {{0, 0, 0}};
        static constexpr Speaker speaker0 = {(cl_float3) {{0, 0, 1}}, 0};
        static constexpr Speaker speaker1 = {(cl_float3) {{0, 0, 1}}, 0.5};
        static constexpr Speaker speaker2 = {(cl_float3) {{0, 0, 1}}, 1};

        Impulse constructImpulse (float x, float y, float z)
        {
            return (Impulse) {(VolumeType) {{1, 1, 1, 1, 1, 1, 1, 1}}, (cl_float3) {{x, y, z}}, dist (generator)};
        }

        vector <Impulse> in;
        vector <AttenuatedImpulse> out;

        default_random_engine generator;
        uniform_real_distribution <float> dist;
    };

    const cl_float3 AttenuationTest::mic_pos;
    const Speaker AttenuationTest::speaker0;
    const Speaker AttenuationTest::speaker1;
    const Speaker AttenuationTest::speaker2;

    TEST_F(AttenuationTest, AttenuateSpeaker0)
    {
        run (speaker0);
        for (const auto & j : out)
            ASSERT_FLOAT_EQ(j.volume.s [0], 1);
    }
    TEST_F(AttenuationTest, AttenuateSpeaker1)
    {
        run (speaker1);
        ASSERT_FLOAT_EQ(out [0].volume.s [0], 0.5);
        ASSERT_FLOAT_EQ(out [1].volume.s [0], 0.5);
        ASSERT_FLOAT_EQ(out [2].volume.s [0], 0.5);
        ASSERT_FLOAT_EQ(out [3].volume.s [0], 0.5);
        ASSERT_FLOAT_EQ(out [4].volume.s [0], 0);
        ASSERT_FLOAT_EQ(out [5].volume.s [0], 1);
    }
    TEST_F(AttenuationTest, AttenuateSpeaker2)
    {
        run (speaker2);
        ASSERT_FLOAT_EQ(out [0].volume.s [0], 0);
        ASSERT_FLOAT_EQ(out [1].volume.s [0], 0);
        ASSERT_FLOAT_EQ(out [2].volume.s [0], 0);
        ASSERT_FLOAT_EQ(out [3].volume.s [0], 0);
        ASSERT_FLOAT_EQ(out [4].volume.s [0], -1);
        ASSERT_FLOAT_EQ(out [5].volume.s [0], 1);
    }

    TEST_F(AttenuationTest, Timing)
    {
        run (speaker0);
        auto i = in.begin();
        auto j = out.begin();
        for (; i != in.end() && j != out.end(); ++i, ++j)
            ASSERT_EQ(i->time, j->time);
    }
}
