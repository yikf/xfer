// Sanity checks for the public build-time constants in include/xfer/*.h.

#include <cstring>
#include <string>

#include "gtest/gtest.h"
#include "xfer/version.h"

TEST(VersionTest, StringIsWellFormed) {
    // Expect "X.Y.Z".
    const std::string v = XFER_VERSION_STR;
    size_t first_dot = v.find('.');
    ASSERT_NE(first_dot, std::string::npos);
    size_t second_dot = v.find('.', first_dot + 1);
    ASSERT_NE(second_dot, std::string::npos);
    EXPECT_EQ(v.find('.', second_dot + 1), std::string::npos);
}

TEST(VersionTest, MajorMinorMicroArePositive) {
    EXPECT_GE(XFER_VERSION_MAJOR, 0);
    EXPECT_GE(XFER_VERSION_MINOR, 0);
    EXPECT_GE(XFER_VERSION_MICRO, 0);
}
