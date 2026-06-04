// xfer - transfer/protocol: on-wire serialize / deserialize helpers.
//
// All multi-byte integers are emitted / consumed in little-endian byte order.
// See protocol.h for the full wire layout.

#include "transfer/protocol.h"

#include <cstring>
#include <vector>

#include "common/crc32.h"
#include "xfer/protocol.h"

namespace xfer {
namespace transfer {
namespace wire {

namespace {

// ---- little-endian helpers ----
// Portable byte-order helpers: no compiler builtins required.

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

// ---- Session header: 32 bytes, last 4 bytes = CRC-32 of the first 28. ----
bool WriteSessionHeader(net::Socket& sock, const SessionHeader& h,
                        std::error_code& ec) {
  char buf[32] = {0};
  EncodeU32LE(kProtoMagic, &buf[0]);
  EncodeU16LE(h.version, &buf[4]);
  // buf[6..7]: reserved two bytes.
  EncodeU64LE(h.total_files, &buf[8]);
  EncodeU64LE(h.total_bytes, &buf[16]);
  EncodeU32LE(h.flags, &buf[24]);
  // Trailer: CRC-32 computed over the first 28 bytes.
  std::uint32_t header_crc = common::Crc32::Compute(buf, 28);
  EncodeU32LE(header_crc, &buf[28]);
  return sock.SendAll(buf, sizeof(buf), ec);
}

bool ReadSessionHeader(net::Socket& sock, SessionHeader& h,
                       std::error_code& ec) {
  char buf[32];
  if (!sock.RecvAll(buf, sizeof(buf), ec)) return false;

  if (DecodeU32LE(&buf[0]) != kProtoMagic) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  // Validate the self-CRC before trusting any field.
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

// ---- Ack: 8 bytes (4 magic + 4 flags) ----
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

// ---- File record header: 18 bytes (4 magic + 2 path_len + 8 size + 4 crc) ----
// followed immediately by `path_len` bytes of UTF-8 path. Payload bytes are
// NOT sent here; callers push them separately to enable kernel zero-copy.
//
// On-wire layout (fixed 18 bytes):
//   offset 0..3 : "FILE" magic
//   offset 4..5 : path_len (LE u16)
//   offset 6..13: file_size (LE u64)
//   offset 14..17: payload_crc32 (LE u32)
bool WriteFileHeader(net::Socket& sock, const FileEntry& entry,
                     std::error_code& ec) {
  if (entry.rel_path.size() > 0xFFFF) {
    ec = std::make_error_code(std::errc::filename_too_long);
    return false;
  }
  auto path_len = static_cast<std::uint16_t>(entry.rel_path.size());
  char fixed[18];
  EncodeU32LE(kFileMagic, &fixed[0]);
  EncodeU16LE(path_len, &fixed[4]);
  EncodeU64LE(entry.size, &fixed[6]);
  EncodeU32LE(entry.crc32, &fixed[14]);
  if (!sock.SendAll(fixed, sizeof(fixed), ec)) return false;
  if (path_len > 0 &&
      !sock.SendAll(entry.rel_path.data(), path_len, ec)) return false;
  return true;
}

bool ReadFileHeader(net::Socket& sock, FileEntry& entry,
                    std::error_code& ec) {
  char fixed[18];
  if (!sock.RecvAll(fixed, sizeof(fixed), ec)) return false;
  if (DecodeU32LE(&fixed[0]) != kFileMagic) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  auto path_len = DecodeU16LE(&fixed[4]);
  entry.size = DecodeU64LE(&fixed[6]);
  entry.crc32 = DecodeU32LE(&fixed[14]);

  if (path_len > 0) {
    // Buffer the variable-length path separately to avoid allocating a
    // std::string of untrusted length directly.
    std::vector<char> tmp(path_len);
    if (!sock.RecvAll(tmp.data(), path_len, ec)) return false;
    entry.rel_path.assign(tmp.data(), tmp.size());
  } else {
    entry.rel_path.clear();
  }
  return true;
}

// ---- Done: 4-byte magic sentinel ----
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
