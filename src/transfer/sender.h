// xfer - transfer / sender 模块 - 发送端高层 API
//
// 发送端负责：
//   1) 把命令行传入的文件/目录递归扫描成一份"文件清单 (manifest)；
//   2) 对清单中每个文件预先计算 CRC-32；
//   3) 通过 Socket 依次写出：SessionHeader -> 若干 FileRecord -> 原始 payload -> DONE。
//
// 关键设计点：
//   * 零拷贝优化：在 Linux / macOS 上优先使用 sendfile(2) 直接把文件内容推到 TCP，避免用户态缓冲区；
//     在不支持的平台（或通过 --no-zero-copy）回退到 pread + SendAll 的用户态路径。
//   * CRC-32 在发送端预先计算并写入 FileRecord，接收端可校验；
//   * 支持 --recursive 目录遍历；--strip-prefix 控制远端相对路径；
//   * 所有错误通过 std::error_code 向上报告，调用方决定失败即失败语义。

#ifndef XFER_TRANSFER_SENDER_H
#define XFER_TRANSFER_SENDER_H

#include <string>
#include <system_error>
#include <vector>

#include "net/socket.h"
#include "transfer/protocol.h"

namespace xfer {
namespace transfer {

// 控制文件收集与发送过程的选项。
// Options controlling how we collect and send files.
struct SenderOptions {
    bool recursive = false;       // 是否递归进入子目录 (--recursive / -r
    bool follow_symlinks = false;  // 遍历时是否跟随符号链接 (-L)
    bool zero_copy = true;         // 是否启用 sendfile() 零拷贝路径
    bool no_progress = false;      // 关闭进度条
    std::string strip_prefix;     // 发送时从每个文件路径前去掉的前缀（控制远端相对路径）
};

// 扫描给定的 paths（文件或目录），构建 FileEntry 清单。
// 当 options.recursive 为 false 时会跳过目录（并打印一条 WARN 日志）。
// 返回已排序且去重后的文件列表。
//
// Scan the given `paths` (files or directories) and build a file manifest.
// Directories are recursed into only when `options.recursive` is true.
std::vector<FileEntry> CollectFiles(const std::vector<std::string>& paths,
                                    const SenderOptions& options,
                                    std::error_code& ec);

// 通过 sock 发送 manifest 描述的文件。
// 写出 session header、每个文件的 record、以及最后的 DONE 标记。
//
// Send the files described by the manifest through `sock`.
// Writes session header, file records, and the final DONE marker.
bool SendFiles(net::Socket& sock,
               const std::vector<std::string>& source_paths,  // 用于磁盘上打开文件
               const std::vector<FileEntry>& manifest,
               const SenderOptions& options,
               std::error_code& ec);

// 高层便捷函数：一次调用 CollectFiles + SendFiles，
// 在单个调用中完成扫描与传输。
//
// Higher-level helper: scan + send in one call.
bool SendPaths(net::Socket& sock,
               const std::vector<std::string>& paths,
               const SenderOptions& options,
               std::error_code& ec);

}  // namespace transfer
}  // namespace xfer

#endif  // XFER_TRANSFER_SENDER_H
