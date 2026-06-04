// LogStream implementation.
#include "common/logging.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace xfer {
namespace log {

namespace {

Level g_level = Level::INFO;
bool  g_color_forced = false;
bool  g_color_wanted = false;

bool WantColorByDefault() {
#if defined(__unix__) || defined(__APPLE__)
  return isatty(fileno(stderr)) != 0;
#else
  return true;
#endif
}
bool WantColor() { return g_color_forced ? g_color_wanted : WantColorByDefault(); }

const char* AnsiColorFor(Level level) {
  if (!WantColor()) return "";
  switch (level) {
    case Level::DEBUG: return "\033[36m";
    case Level::INFO:  return "\033[32m";
    case Level::WARN:  return "\033[33m";
    case Level::ERROR: return "\033[31m";
  }
  return "";
}
const char* AnsiReset() { return WantColor() ? "\033[0m" : ""; }

}  // namespace

void  SetLevel(Level level) { g_level = level; }
Level GetLevel() { return g_level; }
void  SetColor(bool enabled) { g_color_forced = true; g_color_wanted = enabled; }

const char* LevelToString(Level level) {
  switch (level) {
    case Level::DEBUG: return "DEBUG";
    case Level::INFO:  return "INFO ";
    case Level::WARN:  return "WARN ";
    case Level::ERROR: return "ERROR";
  }
  return "?????";
}

LogStream::LogStream(Level level, const char* /*file*/, int /*line*/)
    : enabled_(level >= g_level), level_(level) {
  if (!enabled_) return;
  std::time_t t = std::time(nullptr);
  std::tm     tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  oss_ << AnsiColorFor(level_) << "["
       << std::put_time(&tm_buf, "%H:%M:%S")
       << "][" << LevelToString(level_) << "]" << AnsiReset() << " ";
}

LogStream::~LogStream() {
  if (!enabled_) return;
  std::cerr << oss_.str() << '\n';
}

}  // namespace log
}  // namespace xfer
