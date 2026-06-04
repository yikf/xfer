// Single-line, carriage-return progress bar.
//
// Designed to be driven from the transfer hot loop. Call Add(n) with
// the number of bytes just transferred; the bar throttles terminal
// updates internally (at most one refresh per ~100ms).
//
// Finish() emits a newline so the next console output begins cleanly
// on a new line. The destructor does NOT emit a newline so that the
// bar is invisible if it is never drawn.
//
// Progress output is gated on stderr being a TTY; non-interactive
// invocations (pipes, CI logs) produce no escape sequences.
#ifndef XFER_COMMON_PROGRESS_H
#define XFER_COMMON_PROGRESS_H

#include <cstdint>
#include <string>

namespace xfer {
namespace common {

class Progress {
 public:
  Progress(std::string label, std::uint64_t total_bytes);
  ~Progress();

  Progress(const Progress&) = delete;
  Progress& operator=(const Progress&) = delete;

  void SetLabel(std::string label);
  void SetTotal(std::uint64_t total_bytes);
  void Add(std::uint64_t delta);
  void Finish();
  void SetEnabled(bool enabled) { enabled_ = enabled; }

 private:
  void Render(bool final_line);

  std::string   label_;
  std::uint64_t total_;
  std::uint64_t done_;
  double        start_time_;
  double        last_render_;
  bool          enabled_;
  bool          finished_;
};

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_PROGRESS_H
