// ============================================================================
//  字节数 / 时长的「人类可读」格式化
// ============================================================================
#ifndef XFER_COMMON_FORMAT_H
#define XFER_COMMON_FORMAT_H

#include <cstdint>
#include <string>

namespace xfer {
namespace common {

// 把「字节数」转成带单位的字符串（KiB/MiB/GiB...），保留 2 位小数。
// 例如 1024 → "1.00 KiB"，1500000 → "1.43 MiB"。
//
// 使用 1024 进制（二进制单位），与 `du`、`ls -lh` 行为一致。
std::string FormatBytes(std::uint64_t bytes);

// 把秒数格式化为带单位的字符串。小于 1s 时用毫秒，大于 60s 用分钟，
// 大于 3600s 用小时。便于在日志/进度条里展示运行时间。
std::string FormatDuration(double seconds);

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_FORMAT_H
