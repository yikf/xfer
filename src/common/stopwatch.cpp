// Stopwatch implementation.
#include "common/stopwatch.h"

namespace xfer {
namespace common {

Stopwatch::Stopwatch() : start_(std::chrono::steady_clock::now()) {}
void Stopwatch::Reset() { start_ = std::chrono::steady_clock::now(); }

double Stopwatch::Elapsed() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(now - start_).count();
}

}  // namespace common
}  // namespace xfer
