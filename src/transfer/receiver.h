// xfer - transfer / receiver 模块 - 接收端高层 API
//
// 接收端负责：
//   1) 从 Socket 读取 SessionHeader，校验 magic 与 version；
//   2) 回写一个 SessionAck（4 字节 flags），允许未来协商能力）；
//   3) 循环读取 FileRecordHeader，对每个 record：
//        - 校验 rel_path（拒绝 ".."、绝对路径、反斜杠等穿越字符）；
//        - 打开目标文件，边接收边写盘；
//        - 同步计算 CRC-32，若 receiver 设置 verify_crc 则与 record 中 CRC 对比；
//   4) 最后读取 DONE 标记（可选）；
//   5) 填充 ReceiverSummary，供调用方输出统计。
//
// 安全性：
//   * 所有 rel_path 必须通过 ValidateAndJoin 的白名单检查，防止写出 output_dir 之外。
//   * 文件/目录创建仅限于 output_dir 内部，避免覆盖敏感路径。

#ifndef XFER_TRANSFER_RECEIVER_H
#define XFER_TRANSFER_RECEIVER_H

#include <string>
#include <system_error>
#include <vector>

#include "net/socket.h"
#include "transfer/protocol.h"

namespace xfer {
namespace transfer {

// 接收端运行参数：输出目录及校验选项。
struct ReceiverOptions {
    std::string output_dir;  // 输出根目录；为空则为当前目录
    bool create_dir = true;    // 若 output_dir 不存在时是否创建
    bool no_progress = false; // 不打印进度条
    bool verify_crc = true;    // 是否校验每个文件的 CRC-32
};

// 接收端统计信息。
struct ReceiverSummary {
    std::uint64_t total_files = 0;  // 成功写入的文件数
    std::uint64_t total_bytes = 0;  // 成功写入的字节数
    std::vector<std::string> written_paths;   // 实际写入的路径列表
    double elapsed_seconds = 0;    // 从接收到全部文件的耗时
};

// 阻塞式接收主循环：读取 session header、逐个文件 record、读取 DONE 标记；
// 将所有文件写入 options.output_dir。任何一步失败返回 false。
//
// Blocking receive loop.  Reads session header, per-file records, and DONE
// trailer, writing files to `options.output_dir`.  Returns false on any error.
bool ReceiveStream(net::Socket& sock,
                   const ReceiverOptions& options,
                   ReceiverSummary& summary,
                   std::error_code& ec);

}  // namespace transfer
}  // namespace xfer

#endif  // XFER_TRANSFER_RECEIVER_H
