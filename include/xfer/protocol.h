// xfer — public wire-protocol magic numbers and flag bits.
//
// All multi-byte integers on the wire are little-endian.
//
// Session layout (sender → receiver):
//   [SessionHeader (28 B)] + [header_crc32 (4 B)]  → 32 B total
//   [FileRecordHeader (18 B) + path (N B)] × total_files
//   [FilePayload (size bytes)]                        × total_files
//   [Done magic (4 B)]                                  ← optional trailer
//
// Ack layout (receiver → sender):
//   [magic (4 B)] + [receiver_flags (4 B)]             → 8 B total
//
// Do not change existing magic numbers — doing so breaks wire
// compatibility with older binaries. Add new flags only.
#ifndef XFER_PROTOCOL_H
#define XFER_PROTOCOL_H

#include <cstdint>

namespace xfer {

// --- Magic numbers ----------------------------------------------------------

// SessionHeader starts the stream — it is also used as a quick
// handshaking check; receivers close the connection if the first 4
// bytes do not match kProtoMagic.
constexpr std::uint32_t kProtoMagic = 0x58464552;  // "XFER" little-endian
constexpr std::uint16_t kProtoVersion = 0x0001;
constexpr std::uint32_t kProtoAck   = 0x4f4b4159;  // "OKAY"
constexpr std::uint32_t kFileMagic  = 0x46494c45;  // "FILE"
constexpr std::uint32_t kDoneMagic  = 0x444f4e45;  // "DONE"

// --- Session-header flag bits -------------------------------------------------
constexpr std::uint32_t kFlagCrc32  = 1u << 0;  // Per-file CRC-32C is present.
constexpr std::uint32_t kFlagZstd   = 1u << 1;  // Reserved: zstd-compressed payloads.
constexpr std::uint32_t kFlagDryRun = 1u << 2;  // Reserved: stats only, no writes.

}  // namespace xfer

#endif  // XFER_PROTOCOL_H
