// ============================================================================
//  单行进度条（仅在 TTY 上启用）
//  ---------------------------------------------------------------------------
//  设计目标：
//    * 在 1 行上显示「文件名 进度%  [已传/总大小, 速度/s, eta 剩余]」。
//    * 刷新率受节流（100 ms 一次），避免小文件在慢速链接上产生
//      大量终端刷新开销。
//    * 不使用 ncurses / 第三方库，仅依赖 \r（回车不换行）与
//      ANSI 清行转义序列 `\033[K`。
// ============================================================================
#ifndef XFER_COMMON_PROGRESS_H
#define XFER_COMMON_PROGRESS_H

#include <cstdint>
#include <string>

namespace xfer {
namespace common {

class Progress {
 public:
  // `label` 通常是当前文件名；`total_bytes` 为 0 时进度被视为 100%。
  Progress(std::string label, std::uint64_t total_bytes);
  ~Progress();

  // 在传输「下一个文件」时可重写标签；通常在循环中调用。
  void SetLabel(std::string label);
  void SetTotal(std::uint64_t total_bytes);

  // 增加 `delta` 字节到计数器，并在满足刷新频率时重绘一行。
  void Add(std::uint64_t delta);

  // 完成：写入最终一行 + 换行；多次 Finish 无害。
  void Finish();

  // 显式启用/禁用（用于脚本环境中始终关闭）。
  void SetEnabled(bool enabled) { enabled_ = enabled; }

 private:
  // 真正渲染一行。`final_line` 为 true 时以 `\n` 结束，否则只回车。
  void Render(bool final_line);

  std::string label_;
  std::uint64_t total_;
  std::uint64_t done_;
  double start_time_;    // 起始时刻（秒，单调钟）
  double last_render_;   // 上次渲染时刻
  std::uint64_t last_render_done_;  // 上次渲染时的 done_ 值，用于跳过低变化率
  bool enabled_;         // 进度条是否启用（TTY 检测 / 手动覆盖）
  bool finished_;        // 是否已 Finish，防止重复写换行
  int width_;            // 终端宽度（暂时仅存值，未使用）
};

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_PROGRESS_H
