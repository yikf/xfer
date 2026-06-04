// ============================================================================
//  Wire-Protocol 常量（对外公开头文件）
//  ---------------------------------------------------------------------------
//  所有网络协议字段都使用 **小端字节序**，这样在 x86_64 / ARM64（均为小端）
//  上可以直接 `memcpy`，无需字节交换。
//
//  Magic 数字本质上是协议自描述字段：
//    - 0x58464552 == ASCII "XFER"
//    - 0x4f4b4159 == ASCII "OKAY"（接收端 ACK）
//    - 0x46494c45 == ASCII "FILE"
//    - 0x444f4e45 == ASCII "DONE"
//
//  这些 Magic 的作用：
//    1) 识别非 xfer 协议连接并提前断开，避免把随机字节写进磁盘
//    2) 协议升级时可以通过 Session Header 中的 version 字段做
//       向下兼容的扩展
// ============================================================================
#ifndef XFER_PROTOCOL_H
#define XFER_PROTOCOL_H

#include <cstdint>

namespace xfer {

// Session Header 中的 magic（32 位小端）
constexpr std::uint32_t kProtoMagic   = 0x58464552;  // 'X','F','E','R'
// Session 协议版本号；未来扩展时从 1 递增。
constexpr std::uint16_t kProtoVersion = 0x0001;
// 接收端在完成握手后回写的 ACK magic。
constexpr std::uint32_t kProtoAck     = 0x4f4b4159;  // 'O','K','A','Y'
// 每个文件记录开始处的 magic。
constexpr std::uint32_t kFileMagic    = 0x46494c45;  // 'F','I','L','E'
// 流结束标记；接收端以此判断所有文件传输完成。
constexpr std::uint32_t kDoneMagic    = 0x444f4e45;  // 'D','O','N','E'

// Session 级 flag 位。bit-0 表示启用每文件 CRC-32 校验（默认开启）。
constexpr std::uint32_t kFlagCrc32   = 1u << 0;
// 预留位：未来可能用于 zstd 压缩
constexpr std::uint32_t kFlagZstd    = 1u << 1;
// 预留位：dry-run（不写磁盘，只统计/校验）
constexpr std::uint32_t kFlagDryRun  = 1u << 2;

}  // namespace xfer

#endif  // XFER_PROTOCOL_H
