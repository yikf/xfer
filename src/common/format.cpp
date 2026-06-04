// ============================================================================
//  FormatBytes / FormatDuration 实现
//  ---------------------------------------------------------------------------
//  实现技巧：使用 `snprintf(buf, sizeof(buf), ...)` 输出到栈上固定
//  缓冲区，避免 `std::ostringstream` 的临时堆分配。
// ============================================================================
#include "common/format.h"

#include <array>
#include <cstdio>

namespace xfer {
namespace common {

std::string FormatBytes(std::uint64_t bytes) {
  // 二进制单位表：1024^0, 1024^1, ...
  // C 风格字符串数组是这里最高效的选择；`const char* const` 表示
  // 「指向常量 char 的常量指针」，既不能修改指针本身，也不能修改
  // 被指向的内容。
  static const char* const kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  // `sizeof(kUnits) / sizeof(kUnits[0])` 是在编译期取得数组元素
  // 个数的经典手法，避免 magic number。
  constexpr int kNumUnits = static_cast<int>(sizeof(kUnits) / sizeof(kUnits[0]));

  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < kNumUnits - 1) {
    value /= 1024.0;
    ++unit;
  }
  // 32 个字符足以容纳任何合理大小的人类可读数字；
  // 实际上只要 ~12 字节，多出来的是为了安全。
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
