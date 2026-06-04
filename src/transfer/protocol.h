// ============================================================================
//  应用层 wire protocol：header + 多 file record + DONE
//  ---------------------------------------------------------------------------
//  所有整数均为小端字节序（x86_64/ARM64 本机直接可用）。
//
//  Session Header（32 字节，发送端先写出）：
//   [ 4] magic        = 0x58464552 ("XFER")
//   [ 2] version      = 1
//   [ 2] reserved     = 0
//   [ 8] total_files  (本次会话传输多少个文件)
//   [ 8] total_bytes  (本次会话传输多少字节数据)
//   [ 4] flags        (bit 0 = CRC enabled)
//   [ 4] header_crc32 (前 28 字节的 CRC-32)
//
//  Ack（8 字节，接收端在 session header 校验成功后回写）：
//   [ 4] magic        = 0x4f4b4159 ("OKAY")
//   [ 4] receiver_flags (= 0, 预留)
//
//  File Record（变长，循环 total_files 次）：
//   [ 4] magic        = 0x46494c45 ("FILE")
//   [ 2] path_len     (UTF-8 相对路径长度)
//   [ 8] file_size    (文件体字节数)
//   [ 4] payload_crc32 (file_size 字节数据的 CRC-32; 0 = 未校验)
//   [path_len] path   (相对路径; 仅包含 '/'; 不以 '/' 或 '..' 开头)
//   [file_size] data  (文件内容; 由 sender 用零拷贝或普通 IO 发送)
//
//  End-Of-Stream（4 字节，可选）：
//   [ 4] magic        = 0x444f4e45 ("DONE")
//
//  安全约束（由 receiver 强制执行）：
//   * path 不得为空、不得包含 '..' 组件、不得是绝对路径、不得包含 '\'。
//   * 若 header_crc32 不正确则直接关闭连接；
//   * 若开启了逐文件 CRC 校验则文件不匹配时停止写入并报错。
// ============================================================================
#ifndef XFER_TRANSFER_PROTOCOL_H
#define XFER_TRANSFER_PROTOCOL_H

#include <cstdint>
#include <string>

#include "net/socket.h"

namespace xfer {
namespace transfer {

struct SessionHeader {
  std::uint16_t version = 1;
  std::uint32_t flags = 0;
  std::uint64_t total_files = 0;
  std::uint64_t total_bytes = 0;
};

struct FileEntry {
  std::string rel_path;
  std::uint64_t size = 0;
  std::uint32_t crc32 = 0;  // 0 = 未计算/未启用
};

namespace wire {

// 写出 / 读取 session header。
bool WriteSessionHeader(net::Socket& sock, const SessionHeader& header,
                        std::error_code& ec);
bool ReadSessionHeader(net::Socket& sock, SessionHeader& header,
                       std::error_code& ec);

// 写出 / 读取 ack。
bool WriteAck(net::Socket& sock, std::uint32_t receiver_flags,
              std::error_code& ec);
bool ReadAck(net::Socket& sock, std::uint32_t& receiver_flags,
             std::error_code& ec);

// 写出单条 file record 的「头部」（不含 payload 数据）；
// payload 由 sender 自行紧跟其后发送，以便做零拷贝。
bool WriteFileHeader(net::Socket& sock, const FileEntry& entry,
                     std::error_code& ec);
// 读取单条 file record 的头部（不含 payload）。
bool ReadFileHeader(net::Socket& sock, FileEntry& entry,
                    std::error_code& ec);

// 写出 / 读取 DONE 标记。
bool WriteDone(net::Socket& sock, std::error_code& ec);
bool ReadDone(net::Socket& sock, std::error_code& ec);

}  // namespace wire
}  // namespace transfer
}  // namespace xfer

#endif  // XFER_TRANSFER_PROTOCOL_H
