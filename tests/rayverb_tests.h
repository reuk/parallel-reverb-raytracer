#include "rayverb.h"
#include "helpers.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include "gtest/gtest.h"

#include <vector>
#include <random>

namespace {
    using namespace std;

    class ClTest: public ::testing::Test
    {
    protected:
        ClTest()
        {
            cl::Platform::get (&platform);
        }

        virtual ~ClTest()
        {

        }

        vector <cl::Platform> platform;
    };

    TEST_F(ClTest, HasPlatforms)
    {
        ASSERT_FALSE(platform.empty());
    }

    TEST_F(ClTest, GetContextNoThrow)
    {
        cl_context_properties cps [3] = {
            CL_CONTEXT_PLATFORM,
            (cl_context_properties) (platform [0]) (),
            0
        };

        ASSERT_NO_THROW({
            cl::Context context (CL_DEVICE_TYPE_GPU, cps);
        });
    }
}

namespace {
    using namespace std;

    class KernelTest: public KernelLoader, public ::testing::Test
    {
    protected:
        KernelTest()
        :   KernelLoader (cl_context)
        ,   cl_in  (cl_context, CL_MEM_READ_WRITE, nreflections * SIZE * sizeof (Impulse))
        ,   cl_out (cl_context, CL_MEM_READ_WRITE, nreflections * SIZE * sizeof (Impulse))
        ,   attenuate
            (   cl::make_kernel
                <   cl_float3
                ,   cl::Buffer
                ,   cl::Buffer
                ,   cl_ulong
                ,   Speaker
                > (cl_program, "attenuate")
            )
        ,   out (SIZE)
        ,   dist (0, 100)
        {
            in.push_back (constructImpulse (-10, 0, 0));
            in.push_back (constructImpulse (10, 0, 0));

            in.push_back (constructImpulse (0, -10, 0));
            in.push_back (constructImpulse (0, 10, 0));

            in.push_back (constructImpulse (0, 0, -10));
            in.push_back (constructImpulse (0, 0, 10));

            in.resize (SIZE, constructImpulse (0, 0, 0));
        }

        virtual ~KernelTest()
        {

        }

        void run (const Speaker & speaker)
        {
            cl::copy
            (   queue
            ,   in.begin()
            ,   in.end()
            ,   cl_in
            );
            attenuate
            (   cl::EnqueueArgs (queue, cl::NDRange (SIZE))
            ,   mic_pos
            ,   cl_in
            ,   cl_out
            ,   nreflections
            ,   speaker
            );
            cl::copy
            (   queue
            ,   cl_out
            ,   out.begin()
            ,   out.end()
            );
        }

        cl::Buffer cl_in;
        cl::Buffer cl_out;

        static const auto SIZE = 1024;
        decltype
        (   cl::make_kernel
            <   cl_float3
            ,   cl::Buffer
            ,   cl::Buffer
            ,   cl_ulong
            ,   Speaker
            > (cl_program, "attenuate")
        ) attenuate;
        static constexpr cl_float3 mic_pos = {{0, 0, 0}};
        static constexpr Speaker speaker0 = {(cl_float3) {{0, 0, 1}}, 0};
        static constexpr Speaker speaker1 = {(cl_float3) {{0, 0, 1}}, 0.5};
        static constexpr Speaker speaker2 = {(cl_float3) {{0, 0, 1}}, 1};
        static const auto nreflections = 2;

        Impulse constructImpulse (float x, float y, float z)
        {
            return (Impulse) {(VolumeType) {{1, 1, 1, 1, 1, 1, 1, 1}}, (cl_float3) {{x, y, z}}, dist (generator)};
        }

        vector <Impulse> in;
        vector <Impulse> out;

        default_random_engine generator;
        uniform_real_distribution <float> dist;
    };

    const cl_float3 KernelTest::mic_pos;
    const Speaker KernelTest::speaker0;
    const Speaker KernelTest::speaker1;
    const Speaker KernelTest::speaker2;

    TEST_F(KernelTest, AttenuateSpeaker0)
    {
        run (speaker0);
        for (auto j = 0; j != SIZE * nreflections; ++j)
            for (auto i = 1; i != sizeof (VolumeType) / sizeof (float); ++i)
                ASSERT_EQ(out [j].volume.s [0], out [j].volume.s [i]);
        ASSERT_EQ(out [0].volume.s [0], 1);
        ASSERT_EQ(out [1].volume.s [0], 1);
        ASSERT_EQ(out [2].volume.s [0], 1);
        ASSERT_EQ(out [3].volume.s [0], 1);
        ASSERT_EQ(out [4].volume.s [0], 1);
        ASSERT_EQ(out [5].volume.s [0], 1);
    }
    TEST_F(KernelTest, AttenuateSpeaker1)
    {
        run (speaker1);
        for (auto j = 0; j != SIZE * nreflections; ++j)
            for (auto i = 1; i != sizeof (VolumeType) / sizeof (float); ++i)
                ASSERT_EQ(out [j].volume.s [0], out [j].volume.s [i]);
        ASSERT_EQ(out [0].volume.s [0], 0.5);
        ASSERT_EQ(out [1].volume.s [0], 0.5);
        ASSERT_EQ(out [2].volume.s [0], 0.5);
        ASSERT_EQ(out [3].volume.s [0], 0.5);
        ASSERT_EQ(out [4].volume.s [0], 0);
        ASSERT_EQ(out [5].volume.s [0], 1);
    }
    TEST_F(KernelTest, AttenuateSpeaker2)
    {
        run (speaker2);
        for (auto j = 0; j != SIZE * nreflections; ++j)
            for (auto i = 1; i != sizeof (VolumeType) / sizeof (float); ++i)
                ASSERT_EQ(out [j].volume.s [0], out [j].volume.s [i]);
        ASSERT_EQ(out [0].volume.s [0], 0);
        ASSERT_EQ(out [1].volume.s [0], 0);
        ASSERT_EQ(out [2].volume.s [0], 0);
        ASSERT_EQ(out [3].volume.s [0], 0);
        ASSERT_EQ(out [4].volume.s [0], -1);
        ASSERT_EQ(out [5].volume.s [0], 1);
    }

    TEST_F(KernelTest, Timing)
    {
        run (speaker0);
        auto i = in.begin();
        auto j = out.begin();
        for (; i != in.end() && j != out.end(); ++i, ++j)
            ASSERT_EQ(i->time, j->time);
    }
}
