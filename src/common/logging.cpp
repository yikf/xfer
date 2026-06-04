// ============================================================================
//  彩色分级日志实现
// ============================================================================
#include "common/logging.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#  include <unistd.h>
#endif

namespace xfer {
namespace log {

namespace {

// 当前全局日志阈值（默认 INFO，DEBUG 会被过滤）。
// 使用匿名命名空间 + 普通静态变量 —— 这种方式在单线程下完全正确；
// 在多线程场景下我们依然可以接受（日志级别本身只在启动时设置一次）。
Level g_level = Level::INFO;

// 是否「强制」启用颜色；默认自动检测 TTY。
// 两个变量分别存储「是否被强制覆盖」与「覆盖后的偏好」。
bool g_color_forced = false;
bool g_color_wanted = false;

// 自动检测：stderr 是否指向一个终端？
// 非 TTY（比如被重定向到文件 / 管道）不应输出 ANSI 序列，否则会让
// 文件里出现一堆 `^[[32m` 乱码。
bool WantColorByDefault() {
#if defined(__unix__) || defined(__APPLE__)
  return isatty(fileno(stderr)) != 0;
#else
  return true;
#endif
}

bool WantColor() {
  return g_color_forced ? g_color_wanted : WantColorByDefault();
}

const char* AnsiColorFor(Level level) {
  if (!WantColor()) return "";
  switch (level) {
    case Level::DEBUG: return "\033[36m";  // 青色
    case Level::INFO:  return "\033[32m";  // 绿色
    case Level::WARN:  return "\033[33m";  // 黄色
    case Level::ERROR: return "\033[31m";  // 红色
  }
  return "";
}
const char* AnsiReset() { return WantColor() ? "\033[0m" : ""; }

}  // namespace

void SetLevel(Level level) { g_level = level; }
Level GetLevel() { return g_level; }
void SetColor(bool enabled) {
  g_color_forced = true;
  g_color_wanted = enabled;
}

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
  if (!enabled_) return;  // 若被禁用，oss_ 从此不会被写入
  // 把前缀（时间戳 + 级别）写进 ostringstream，避免每一行
  // 多次调用 operator<< 到 std::cerr 引起的线程交错输出。
  std::time_t t = std::time(nullptr);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  oss_ << AnsiColorFor(level_) << "["
       << std::put_time(&tm_buf, "%H:%M:%S")
       << "][" << LevelToString(level_) << "]" << AnsiReset() << " ";
}

// 析构函数是真正执行写操作的地方：
//  * 把整个 ostringstream 一次性写出到 stderr；
//  * 注意我们没有加 std::endl（它会 flush），而是显式使用
//    '\n' 由 C++ 运行时在缓冲区满时刷新 —— 对于日志来说足够。
LogStream::~LogStream() {
  if (!enabled_) return;
  std::cerr << oss_.str() << '\n';
}

}  // namespace log
}  // namespace xfer
