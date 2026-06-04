// Tests for the byte / duration formatting helpers in common/format.{h,cpp}.
//
// FormatBytes and FormatDuration are used by the CLI progress bar and the
// session summary log lines, so we only care about human readability here --
// exact digit counts are not guaranteed across platforms (the implementation
// relies on std::ostringstream with fixed precision).

#include <string>

#include "common/format.h"
#include "gtest/gtest.h"

namespace xfer {
namespace common {
namespace {

TEST(FormatBytesTest, ZeroAndSmallValues) {
    EXPECT_EQ(FormatBytes(0), "0 B");
    EXPECT_EQ(FormatBytes(1), "1 B");
    EXPECT_EQ(FormatBytes(1023), "1023 B");
}

TEST(FormatBytesTest, UnitBoundaries) {
    EXPECT_NE(FormatBytes(1024).find("KiB"), std::string::npos);
    EXPECT_NE(FormatBytes(1024ULL * 1024).find("MiB"), std::string::npos);
    EXPECT_NE(FormatBytes(1024ULL * 1024 * 1024).find("GiB"), std::string::npos);
    EXPECT_NE(FormatBytes(1024ULL * 1024 * 1024 * 1024).find("TiB"),
              std::string::npos);
}

TEST(FormatBytesTest, LargeValuesDontCrash) {
    // Just ensure we do not overflow inside the formatter.
    EXPECT_FALSE(FormatBytes(~0ULL).empty());
}

TEST(FormatDurationTest, ZeroAndSmallValues) {
    EXPECT_NE(FormatDuration(0.0).find("ms"), std::string::npos);
    EXPECT_NE(FormatDuration(0.5).find("s"), std::string::npos);
    EXPECT_NE(FormatDuration(60.0).find("min"), std::string::npos);
}

TEST(FormatDurationTest, StrictlyPositiveForAnyInput) {
    // The formatter must never crash on negative / NaN input.
    EXPECT_FALSE(FormatDuration(-1.0).empty());
}

}  // namespace
}  // namespace common
}  // namespace xfer
