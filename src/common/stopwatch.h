// ============================================================================
//  简单的 steady-clock 计时器
//  ---------------------------------------------------------------------------
//  为什么用 `std::chrono::steady_clock`？
//    * system_clock 是「墙上钟」，可能受 NTP 对时、闰秒等反向跳变，
//      不适合用来度量区间。
//    * steady_clock 是单调递增的物理时钟，专门用于时间间隔测量。
// ============================================================================
#ifndef XFER_COMMON_STOPWATCH_H
#define XFER_COMMON_STOPWATCH_H

#include <chrono>

namespace xfer {
namespace common {

class Stopwatch {
 public:
  // 构造即启动；显式记录开始时间点。
  Stopwatch();

  // 重置零点为当前时刻。对多次「分段计时」非常有用。
  void Reset();

  // 返回自起点（或上次 Reset）以来经过的秒数，用 double 表示以
  // 方便做浮点数除法（例如 bytes / seconds = 速度）。
  double Elapsed() const;

 private:
  std::chrono::steady_clock::time_point start_;
};

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_STOPWATCH_H
