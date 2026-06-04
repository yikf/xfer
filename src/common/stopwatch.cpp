// ============================================================================
//  Stopwatch 实现
// ============================================================================
#include "common/stopwatch.h"

namespace xfer {
namespace common {

// `now()` 返回的是一个 time_point；这里直接用 `=` 初始化，
// 避免写成 `start_(std::chrono::steady_clock::now())` —— 两种写法
// 在 C++11 及之后等价，但 `=` 更具「初始化风格」，对初学者更直观。
Stopwatch::Stopwatch() : start_(std::chrono::steady_clock::now()) {}

void Stopwatch::Reset() { start_ = std::chrono::steady_clock::now(); }

double Stopwatch::Elapsed() const {
  auto now = std::chrono::steady_clock::now();
  // duration_cast 会把默认的「纳秒」duration 转为「秒 + double」。
  return std::chrono::duration<double>(now - start_).count();
}

}  // namespace common
}  // namespace xfer
