// CRC-32C (Castagnoli) — byte-at-a-time table implementation.
//
// The table is built once at static-init time. Each invocation of
// Update() folds bytes into the state using the standard slicing-by-1
// recurrence:
//
//   state = table[(state ^ byte) & 0xFF] ^ (state >> 8)
//
// Final() XORs in 0xFFFFFFFF as required by the CRC-32/Castagnoli
// specification.
#include "common/crc32.h"

#include <cstring>

namespace xfer {
namespace common {

namespace {

alignas(64) std::uint32_t g_table[256];
bool g_table_ready = false;

void BuildTable() {
  if (g_table_ready) return;
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int j = 0; j < 8; ++j)
      c = (c & 1u) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
    g_table[i] = c;
  }
  g_table_ready = true;
}

// Meyers' singleton — guarantees the table is built before any
// user-constructed Crc32 is used.
struct TableInitializer { TableInitializer() { BuildTable(); } };
static TableInitializer g_init;

}  // namespace

Crc32::Crc32() : state_(0xFFFFFFFFu) {}
void Crc32::Reset() { state_ = 0xFFFFFFFFu; }

void Crc32::Update(const void* data, std::size_t length) {
  auto p = static_cast<const unsigned char*>(data);
  std::uint32_t s = state_;
  while (length--) s = g_table[(s ^ *p++) & 0xFF] ^ (s >> 8);
  state_ = s;
}

std::uint32_t Crc32::Final() const { return state_ ^ 0xFFFFFFFFu; }

std::uint32_t Crc32::Compute(const void* data, std::size_t length) {
  Crc32 c;
  c.Update(data, length);
  return c.Final();
}

}  // namespace common
}  // namespace xfer
