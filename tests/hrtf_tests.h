#include "rayverb.h"
#include "helpers.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include "gtest/gtest.h"

#include <vector>
#include <random>

namespace TestsNamespace {
    using namespace std;

    class HrtfTest: public HrtfAttenuator, public ::testing::Test
    {
    protected:
        HrtfTest();

        void run (const HrtfConfig & config);

        static const cl_float3 mic_pos;
        static const HrtfConfig config0;
        static const HrtfConfig config1;
        static const HrtfConfig config2;
        static const HrtfConfig config3;

        virtual const std::array <std::array <std::array <cl_float8, 180>, 360>, 2> & getHrtfData() const;

        Impulse constructImpulse (float x, float y, float z);

        vector <Impulse> in;
        vector <AttenuatedImpulse> out;

        default_random_engine generator;
        uniform_real_distribution <float> dist;
        static const array <array <array <cl_float8, 180>, 360>, 2> HRTF_DATA;
    };

}
