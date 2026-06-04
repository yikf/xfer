// ============================================================================
//  彩色分级日志（DEBUG / INFO / WARN / ERROR）
//  ---------------------------------------------------------------------------
//  设计取舍：
//    * 只使用 std::cerr 与 ANSI 转义序列，避免引入 spdlog 等外部依赖，
//      便于初学者阅读核心逻辑。
//    * 所有日志宏都会被展开为一条临时 LogStream 对象，在语句结束处
//      析构并把内容输出到 stderr —— 这种模式在众多日志库里都被使用
//      （典型代表：glog），优点是：
//         - 线程竞争被限制在析构函数这一行；
//         - 如果某条日志被禁用（如 DEBUG 在 Release 里关闭），
//           整个 `<<` 链都不会执行，省掉参数格式化的开销。
// ============================================================================
#ifndef XFER_COMMON_LOGGING_H
#define XFER_COMMON_LOGGING_H

#include <sstream>
#include <string>

namespace xfer {
namespace log {

enum class Level {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
};

// 动态设置日志级别；低于该级别的消息将被完全跳过。
void SetLevel(Level level);
Level GetLevel();

// 强制开/关 ANSI 颜色；默认会自动检测 stderr 是否为 TTY。
void SetColor(bool enabled);

const char* LevelToString(Level level);

// 单个日志语句的流对象。它的生命周期通常只存在于一条语句中：
//   XFER_ILOG << "hello " << value << std::endl;
// 该语句等价于：
//   (临时 LogStream 对象) << "hello " << value << std::endl;
// 语句结束时临时对象析构，把内部字符串输出到 stderr。
class LogStream {
 public:
  LogStream(Level level, const char* /*file*/, int /*line*/);
  ~LogStream();

  // `operator<<` 的返回值必须是 `LogStream&`，否则链式调用会
  // 被截断（只能打印出第一个参数）。
  template <typename T>
  LogStream& operator<<(const T& value) {
    if (enabled_) oss_ << value;
    return *this;
  }

 private:
  bool enabled_;           // 由当前级别决定：当前语句是否应被输出
  Level level_;
  std::ostringstream oss_; // 先把所有 << 内容缓冲到这里，再一次性输出
};

}  // namespace log
}  // namespace xfer

// === 便捷宏 ===
// 为什么用宏而不是函数？
//   - 因为我们希望禁用时整条 `<<` 链都能被优化掉，
//     函数做不到这一点（参数会先被求值）。
// 约定：宏全部大写，带 XFER_ 前缀以避免冲突。
#define XFER_LOG(level) xfer::log::LogStream(level, __FILE__, __LINE__)
#define XFER_DLOG XFER_LOG(xfer::log::Level::DEBUG)
#define XFER_ILOG XFER_LOG(xfer::log::Level::INFO)
#define XFER_WLOG XFER_LOG(xfer::log::Level::WARN)
#define XFER_ELOG XFER_LOG(xfer::log::Level::ERROR)

#endif  // XFER_COMMON_LOGGING_H
