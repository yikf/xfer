// Progress bar — throttled rendering with ETA estimation.
#include "common/progress.h"

#include <cstdio>
#include <ctime>

#include "common/format.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace xfer {
namespace common {

namespace {

// Platform abstraction for monotonic time. We avoid std::chrono here
// because we want the cheapest possible read inside the hot loop.
double NowSec() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

bool IsTty() {
#if defined(__unix__) || defined(__APPLE__)
  return isatty(fileno(stderr)) != 0;
#else
  return true;
#endif
}

}  // namespace

Progress::Progress(std::string label, std::uint64_t total_bytes)
    : label_(std::move(label)),
      total_(total_bytes),
      done_(0),
      start_time_(NowSec()),
      last_render_(start_time_),
      enabled_(IsTty()),
      finished_(false) {}

Progress::~Progress() = default;

void Progress::SetLabel(std::string label) { label_ = std::move(label); }
void Progress::SetTotal(std::uint64_t total_bytes) { total_ = total_bytes; }

void Progress::Add(std::uint64_t delta) {
  done_ += delta;
  if (!enabled_ || finished_) return;

  const double now = NowSec();
  const double elapsed_since = now - last_render_;

  // Throttle: at most one refresh every 100 ms. This keeps
  // terminal-dominated workloads cheap.
  if (elapsed_since < 0.1) return;

  last_render_ = now;
  Render(false);
}

void Progress::Finish() {
  if (finished_) return;
  finished_ = true;
  if (enabled_) Render(true);
}

void Progress::Render(bool final_line) {
  const double elapsed = NowSec() - start_time_;
  const double safe_elapsed = elapsed < 1e-6 ? 1e-6 : elapsed;
  const double speed = static_cast<double>(done_) / safe_elapsed;
  const double eta = (total_ > done_)
                         ? static_cast<double>(total_ - done_) / speed
                         : 0.0;

  const double pct = (total_ > 0)
                         ? (100.0 * static_cast<double>(done_) /
                            static_cast<double>(total_))
                         : 100.0;

  char line[512];
  const int n = std::snprintf(
      line, sizeof(line),
      "\r\033[K%s  %5.1f%%  [%s/%s, %s/s, eta %s]",
      label_.c_str(), pct, FormatBytes(done_).c_str(),
      FormatBytes(total_).c_str(), FormatBytes(static_cast<std::uint64_t>(speed)).c_str(),
      FormatDuration(eta).c_str());

  if (n < 0) return;
  std::fwrite(line, 1, static_cast<std::size_t>(n), stderr);
  if (final_line) std::fputc('\n', stderr);
}

}  // namespace common
}  // namespace xfer
