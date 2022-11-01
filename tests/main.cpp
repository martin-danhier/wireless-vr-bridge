#include <test_framework.hpp>

#include <wvb_common/vr_structs.h>

TEST {
    // Test to see if tests work
    wvb::Extent2D extent { 10, 20 };

    EXPECT_EQ(extent.width, 10u);
    EXPECT_EQ(extent.height, 20u);
}