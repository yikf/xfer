// FormatBytes/FormatDuration implementation.
//
// Both functions write into a small stack buffer and return a
// std::string — this avoids any heap allocation beyond the result.
#include "common/format.h"

#include <array>
#include <cstdio>

namespace xfer {
namespace common {

std::string FormatBytes(std::uint64_t bytes) {
  static const char* const kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  constexpr int kNumUnits = static_cast<int>(sizeof(kUnits) / sizeof(kUnits[0]));

  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < kNumUnits - 1) {
    value /= 1024.0;
    ++unit;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), (unit == 0) ? "%.0f %s" : "%.2f %s",
                value, kUnits[unit]);
  return buf;
}

std::string FormatDuration(double seconds) {
  char buf[32];
  if (seconds < 1.0) {
    std::snprintf(buf, sizeof(buf), "%.0f ms", seconds * 1000.0);
  } else if (seconds < 60.0) {
    std::snprintf(buf, sizeof(buf), "%.2f s", seconds);
  } else if (seconds < 3600.0) {
    std::snprintf(buf, sizeof(buf), "%.1f min", seconds / 60.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f h", seconds / 3600.0);
  }
  return buf;
}

}  // namespace common
}  // namespace xfer
