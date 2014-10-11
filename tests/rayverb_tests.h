#include "rayverb.h"

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

        }

        virtual ~ClTest()
        {

        }

        virtual void SetUp()
        {
            cl::Platform::get (&platform);
        }

        virtual void TearDown()
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

    class SceneTest: public ::testing::Test
    {
    protected:
        SceneTest()
        :   directions (1024)
        ,   triangles (1024)
        ,   vertices (128)
        ,   surfaces (128)
        {
            
        }

        virtual ~SceneTest()
        {

        }

        virtual void SetUp()
        {
            context = getContext();
        }

        virtual void TearDown()
        {

        }

        cl::Context context;
        vector <cl_float3> directions;
        vector <Triangle> triangles;
        vector <cl_float3> vertices;
        vector <Surface> surfaces;
    };

    TEST_F(SceneTest, LongConstructor)
    {
        EXPECT_NO_THROW({
            const Scene scene
            (   context
            ,   128
            ,   directions
            ,   triangles
            ,   vertices
            ,   surfaces
            );
        });
    }

    TEST_F(SceneTest, FileConstructor)
    {
        EXPECT_NO_THROW({
            const Scene scene
            (   context
            ,   128
            ,   directions
            ,   TEST_FILE
            );
        });
    }
}
