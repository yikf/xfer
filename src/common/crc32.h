// CRC-32C (Castagnoli) incremental checksum.
//
// Uses a 256-entry software lookup table for portability.
//
// Typical use:
//   Crc32 h;
//   h.Update(buf1, len1);
//   h.Update(buf2, len2);
//   uint32_t sum = h.Final();
//
// The same object can be reused by calling Reset() before the next
// sequence of Update() calls.
#ifndef XFER_COMMON_CRC32_H
#define XFER_COMMON_CRC32_H

#include <cstddef>
#include <cstdint>

namespace xfer {
namespace common {

class Crc32 {
 public:
  Crc32();
  void Reset();
  void Update(const void* data, std::size_t length);
  std::uint32_t Final() const;

  // One-shot convenience wrapper — equivalent to:
  //   Crc32 c; c.Update(data, length); return c.Final();
  static std::uint32_t Compute(const void* data, std::size_t length);

 private:
  std::uint32_t state_;
};

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_CRC32_H
