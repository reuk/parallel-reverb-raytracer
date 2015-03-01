#include "gtest/gtest.h"
#include "rayverb_tests.h"
#include "attenuation_tests.h"
#include "hrtf_tests.h"

int main (int argc, char * argv[]) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS();
}
