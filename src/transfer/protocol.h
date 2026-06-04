// xfer - transfer/protocol: wire format definitions + serialize/deserialize.
//
// All integers are little-endian. On-wire layout:
//
//   1. Session Header (32 bytes, sent by the sender first):
//       [ 4] magic         = 0x58464552 ("XFER")
//       [ 2] version       = protocol version (currently 1)
//       [ 2] reserved      = 0
//       [ 8] total_files   = number of file records that follow
//       [ 8] total_bytes   = sum of all file payload sizes
//       [ 4] flags         = bit 0: CRC-32 checking enabled
//       [ 4] header_crc32  = CRC-32 of the preceding 28 bytes
//
//   2. Ack (8 bytes, receiver → sender after validating the session header):
//       [ 4] magic         = 0x4f4b4159 ("OKAY")
//       [ 4] receiver_flags = reserved, currently 0
//
//   3. File Record (18 bytes fixed header + variable path + payload):
//       [ 4] magic         = 0x46494c45 ("FILE")
//       [ 2] path_len      = length of the UTF-8 relative path that follows
//       [ 8] file_size     = payload size in bytes
//       [ 4] payload_crc32 = CRC-32 of the payload (0 = not computed)
//       [path_len] path    = relative path using '/' separators only;
//                            must not contain ".." components or be absolute
//       [file_size] data   = raw file bytes (sent separately to enable
//                            zero-copy paths)
//
//   4. End-of-stream marker (4 bytes, optional):
//       [ 4] magic         = 0x444f4e45 ("DONE")
//
// Security constraints enforced by the receiver:
//   * Reject paths with ".." components, '\', or absolute-path semantics.
//   * Reject records whose count does not match session header total_files.
//   * Verify header_crc32 on the session header before sending the ack.
//   * Verify per-file payload_crc32 when non-zero.

#ifndef XFER_TRANSFER_PROTOCOL_H
#define XFER_TRANSFER_PROTOCOL_H

#include <cstdint>
#include <string>

#include "net/socket.h"

namespace xfer {
namespace transfer {

// Session header — total_files/total_bytes/flags.
struct SessionHeader {
  std::uint16_t version = 1;
  std::uint32_t flags = 0;
  std::uint64_t total_files = 0;
  std::uint64_t total_bytes = 0;
};

// Per-file record.
struct FileEntry {
  std::string rel_path;
  std::uint64_t size = 0;
  std::uint32_t crc32 = 0;  // 0 = not computed / disabled
};

namespace wire {

// Session header I/O.
bool WriteSessionHeader(net::Socket& sock, const SessionHeader& header,
                        std::error_code& ec);
bool ReadSessionHeader(net::Socket& sock, SessionHeader& header,
                       std::error_code& ec);

// Ack I/O.
bool WriteAck(net::Socket& sock, std::uint32_t receiver_flags,
              std::error_code& ec);
bool ReadAck(net::Socket& sock, std::uint32_t& receiver_flags,
             std::error_code& ec);

// File record header I/O (fixed 18-byte header + variable path).
// The payload bytes are NOT sent here; callers push them separately to enable
// zero-copy kernel paths.
bool WriteFileHeader(net::Socket& sock, const FileEntry& entry,
                     std::error_code& ec);
bool ReadFileHeader(net::Socket& sock, FileEntry& entry,
                    std::error_code& ec);

// End-of-stream marker.
bool WriteDone(net::Socket& sock, std::error_code& ec);
bool ReadDone(net::Socket& sock, std::error_code& ec);

}  // namespace wire
}  // namespace transfer
}  // namespace xfer

#endif  // XFER_TRANSFER_PROTOCOL_H
