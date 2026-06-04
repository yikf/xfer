// ============================================================================
//  Progress 实现
// ============================================================================
#include "common/progress.h"

#include <cstdio>
#include <ctime>

#include "common/format.h"

#if defined(__unix__) || defined(__APPLE__)
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace xfer {
namespace common {

namespace {

// 返回单调钟当前时刻（秒）。
double NowSec() {
  struct timespec ts {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// 查询终端列数；失败时回退到 80，避免窄终端出现自动换行。
int TerminalWidth() {
#if defined(TIOCGWINSZ)
  struct winsize w {};
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    return static_cast<int>(w.ws_col);
  }
#endif
  return 80;
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
      last_render_done_(0),
      enabled_(IsTty()),
      finished_(false),
      width_(TerminalWidth()) {}

// 析构函数里我们不会强行写 `\n`；只有在真正 `Finish()` 后才换行。
// 这样如果使用者提前销毁，终端上仍然保持干净的当前行。
Progress::~Progress() = default;

void Progress::SetLabel(std::string label) { label_ = std::move(label); }
void Progress::SetTotal(std::uint64_t total_bytes) { total_ = total_bytes; }

void Progress::Add(std::uint64_t delta) {
  done_ += delta;
  if (!enabled_ || finished_) return;

  double now = NowSec();
  // 节流：两次渲染至少间隔 100 ms，且至少传了 1% 的数据。
  // 这两条规则结合起来避免了两种极端：
  //   - 小文件刷新过于频繁（1% 门限）；
  //   - 慢速大文件毫无响应（时间门限）。
  double dt = now - last_render_;
  std::uint64_t dbytes = done_ - last_render_done_;
  if (dt < 0.1 && total_ > 0 && dbytes * 100ULL < total_) return;

  last_render_ = now;
  last_render_done_ = done_;
  Render(false);
}

void Progress::Finish() {
  if (finished_) return;
  finished_ = true;
  if (enabled_) Render(true);
}

void Progress::Render(bool final_line) {
  // 速度 = 已传 / 用时；ETA = 剩余 / 速度。
  double elapsed = NowSec() - start_time_;
  if (elapsed < 1e-6) elapsed = 1e-6;  // 防止除零（极短传输）
  double speed = static_cast<double>(done_) / elapsed;
  double eta = (total_ > done_)
                   ? (static_cast<double>(total_ - done_) / speed)
                   : 0.0;

  double pct = (total_ > 0)
                   ? (100.0 * static_cast<double>(done_) /
                      static_cast<double>(total_))
                   : 100.0;
  if (pct > 100.0) pct = 100.0;

  // 固定缓冲区：512 字节对于进度条来说绰绰有余。
  char line[512];
  int n = std::snprintf(
      line, sizeof(line),
      "\r\033[K%s  %5.1f%%  [%s/%s, %s/s, eta %s]",
      label_.c_str(), pct, FormatBytes(done_).c_str(),
      FormatBytes(total_).c_str(), FormatBytes(static_cast<std::uint64_t>(speed)).c_str(),
      FormatDuration(eta).c_str());

  // 避免超出终端宽度；超出时截断到 width_。
  if (width_ > 0 && n > width_) {
    line[width_] = '\0';
    n = width_;
  }
  std::fwrite(line, 1, static_cast<std::size_t>(n), stderr);
  if (final_line) std::fputc('\n', stderr);
}

}  // namespace common
}  // namespace xfer
