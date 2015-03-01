#include "rayverb.h"
#include "helpers.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include "gtest/gtest.h"

#include <vector>

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

