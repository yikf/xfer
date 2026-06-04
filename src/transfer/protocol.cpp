// ============================================================================
//  wire protocol 序列化实现
// ============================================================================
#include "transfer/protocol.h"

#include <cstring>
#include <vector>

#include "common/crc32.h"
#include "xfer/protocol.h"

namespace xfer {
namespace transfer {
namespace wire {

namespace {

// --- 小端字节序辅助 ---
// 为了可移植性（避免编译器内置函数），我们显式按字节写入/读取。
inline void EncodeU16LE(std::uint16_t v, char* out) {
  out[0] = static_cast<char>(v & 0xFF);
  out[1] = static_cast<char>((v >> 8) & 0xFF);
}
inline void EncodeU32LE(std::uint32_t v, char* out) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline void EncodeU64LE(std::uint64_t v, char* out) {
  for (int i = 0; i < 8; ++i) out[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline std::uint16_t DecodeU16LE(const char* p) {
  return static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0])) |
         (static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[1])) << 8);
}
inline std::uint32_t DecodeU32LE(const char* p) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i)
    v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[i])) << (8 * i);
  return v;
}
inline std::uint64_t DecodeU64LE(const char* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[i])) << (8 * i);
  return v;
}

}  // namespace

// -------------------------------------------------------------------------
//  Session header：32 字节。最后 4 字节是前面 28 字节的 CRC-32。
// -------------------------------------------------------------------------
bool WriteSessionHeader(net::Socket& sock, const SessionHeader& h,
                        std::error_code& ec) {
  char buf[32] = {0};
  EncodeU32LE(kProtoMagic, &buf[0]);
  EncodeU16LE(h.version, &buf[4]);
  // buf[6..7] 保留
  EncodeU64LE(h.total_files, &buf[8]);
  EncodeU64LE(h.total_bytes, &buf[16]);
  EncodeU32LE(h.flags, &buf[24]);
  // 尾部 4 字节 = 前 28 字节的 CRC-32。
  std::uint32_t header_crc = common::Crc32::Compute(buf, 28);
  EncodeU32LE(header_crc, &buf[28]);
  return sock.SendAll(buf, sizeof(buf), ec);
}

bool ReadSessionHeader(net::Socket& sock, SessionHeader& h,
                       std::error_code& ec) {
  char buf[32];
  if (!sock.RecvAll(buf, sizeof(buf), ec)) return false;

  // 校验 magic —— 避免误连接到非 xfer 服务。
  if (DecodeU32LE(&buf[0]) != kProtoMagic) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  // 校验前 28 字节的 CRC-32 与尾部匹配。
  std::uint32_t expected = DecodeU32LE(&buf[28]);
  std::uint32_t actual = common::Crc32::Compute(buf, 28);
  if (expected != actual) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  h.version = DecodeU16LE(&buf[4]);
  h.total_files = DecodeU64LE(&buf[8]);
  h.total_bytes = DecodeU64LE(&buf[16]);
  h.flags = DecodeU32LE(&buf[24]);
  return true;
}

// -------------------------------------------------------------------------
//  Ack：8 字节 (4 OKAY + 4 flags)
// -------------------------------------------------------------------------
bool WriteAck(net::Socket& sock, std::uint32_t receiver_flags,
              std::error_code& ec) {
  char buf[8];
  EncodeU32LE(kProtoAck, &buf[0]);
  EncodeU32LE(receiver_flags, &buf[4]);
  return sock.SendAll(buf, sizeof(buf), ec);
}

bool ReadAck(net::Socket& sock, std::uint32_t& receiver_flags,
             std::error_code& ec) {
  char buf[8];
  if (!sock.RecvAll(buf, sizeof(buf), ec)) return false;
  if (DecodeU32LE(&buf[0]) != kProtoAck) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  receiver_flags = DecodeU32LE(&buf[4]);
  return true;
}

// -------------------------------------------------------------------------
//  File Header：[4 FILE][2 path_len][8 size][4 payload_crc][path_len bytes]
// -------------------------------------------------------------------------
bool WriteFileHeader(net::Socket& sock, const FileEntry& entry,
                     std::error_code& ec) {
  if (entry.rel_path.size() > 0xFFFF) {
    ec = std::make_error_code(std::errc::filename_too_long);
    return false;
  }
  auto path_len = static_cast<std::uint16_t>(entry.rel_path.size());
  char fixed[4 + 2 + 8 + 4];
  EncodeU32LE(kFileMagic, &fixed[0]);
  EncodeU16LE(path_len, &fixed[4]);
  EncodeU64LE(entry.size, &fixed[6]);
  // fixed[14..17] 保留（flags）；先写 0。
  EncodeU32LE(0, &fixed[14]);
  EncodeU32LE(entry.crc32, &fixed[18]);
  if (!sock.SendAll(fixed, sizeof(fixed), ec)) return false;
  if (path_len > 0 &&
      !sock.SendAll(entry.rel_path.data(), path_len, ec)) return false;
  return true;
}

bool ReadFileHeader(net::Socket& sock, FileEntry& entry,
                    std::error_code& ec) {
  char fixed[4 + 2 + 8 + 4];
  if (!sock.RecvAll(fixed, sizeof(fixed), ec)) return false;
  if (DecodeU32LE(&fixed[0]) != kFileMagic) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  auto path_len = DecodeU16LE(&fixed[4]);
  entry.size = DecodeU64LE(&fixed[6]);
  // fixed[14..17]：flags 保留
  entry.crc32 = DecodeU32LE(&fixed[18]);

  if (path_len > 0) {
    // path 放在 vector 里再 move 给 string；避免直接 resize 产生的零初始化。
    std::vector<char> tmp(path_len);
    if (!sock.RecvAll(tmp.data(), path_len, ec)) return false;
    entry.rel_path.assign(tmp.data(), tmp.size());
  } else {
    entry.rel_path.clear();
  }
  return true;
}

// -------------------------------------------------------------------------
//  Done：4 字节 magic
// -------------------------------------------------------------------------
bool WriteDone(net::Socket& sock, std::error_code& ec) {
  char buf[4];
  EncodeU32LE(kDoneMagic, buf);
  return sock.SendAll(buf, sizeof(buf), ec);
}

bool ReadDone(net::Socket& sock, std::error_code& ec) {
  char buf[4];
  if (!sock.RecvAll(buf, sizeof(buf), ec)) return false;
  return DecodeU32LE(buf) == kDoneMagic;
}

}  // namespace wire
}  // namespace transfer
}  // namespace xfer
