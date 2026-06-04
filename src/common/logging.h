// Minimal stream-based logger.
//
// Design:
//   * Log levels are DEBUG < INFO < WARN < ERROR.
//   * `XFER_DLOG / XFER_ILOG / XFER_WLOG / XFER_ELOG` instantiate a
//     temporary `LogStream`, append to it with operator<<, and flush
//     to stderr when the statement ends (destructor).
//   * Colored output is auto-detected from stderr being a TTY, but can
//     be overridden at runtime with SetColor().
//
// Thread-safety: individual log statements are atomic because the
// formatted message is built inside the temporary and printed in one
// `std::cerr << str << '\n'` call. Concurrent calls may still
// interleave at the character level depending on the platform's
// stderr implementation; that is accepted behaviour.
#ifndef XFER_COMMON_LOGGING_H
#define XFER_COMMON_LOGGING_H

#include <sstream>
#include <string>

namespace xfer {
namespace log {

enum class Level {
  DEBUG = 0,
  INFO  = 1,
  WARN  = 2,
  ERROR = 3,
};

void        SetLevel(Level level);
Level       GetLevel();
void        SetColor(bool enabled);
const char* LevelToString(Level level);

class LogStream {
 public:
  LogStream(Level level, const char* /*file*/, int /*line*/);
  ~LogStream();

  template <typename T>
  LogStream& operator<<(const T& value) {
    if (enabled_) oss_ << value;
    return *this;
  }

 private:
  bool             enabled_;
  Level            level_;
  std::ostringstream oss_;
};

}  // namespace log
}  // namespace xfer

#define XFER_LOG(level) xfer::log::LogStream(level, __FILE__, __LINE__)
#define XFER_DLOG XFER_LOG(xfer::log::Level::DEBUG)
#define XFER_ILOG XFER_LOG(xfer::log::Level::INFO)
#define XFER_WLOG XFER_LOG(xfer::log::Level::WARN)
#define XFER_ELOG XFER_LOG(xfer::log::Level::ERROR)

#endif  // XFER_COMMON_LOGGING_H
