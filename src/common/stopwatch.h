// Lightweight steady-clock timer.
//
// Uses std::chrono::steady_clock because it is monotonic and unaffected
// by wall-clock adjustments (NTP, timezones, user changes).
#ifndef XFER_COMMON_STOPWATCH_H
#define XFER_COMMON_STOPWATCH_H

#include <chrono>

namespace xfer {
namespace common {

class Stopwatch {
 public:
  Stopwatch();
  void   Reset();
  double Elapsed() const;  // seconds since construction / last Reset.

 private:
  std::chrono::steady_clock::time_point start_;
};

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_STOPWATCH_H
