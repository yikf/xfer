// Tests for the CRC-32C (Castagnoli) implementation in common/crc32.{h,cpp}.
//
// The Castagnoli polynomial is 0x1EDC6F41.  We verify three invariants:
//   1. The CRC of an empty byte range is zero.
//   2. Known test vectors from RFC 3720 / iSCSI produce the canonical
//      reference values.
//   3. Streaming incremental updates yield the same result as a single
//      contiguous Update() call.

#include <cstdint>
#include <string>
#include <vector>

#include "common/crc32.h"
#include "gtest/gtest.h"

namespace xfer {
namespace common {
namespace {

TEST(Crc32Test, EmptyRangeReturnsZero) {
    Crc32 crc;
    EXPECT_EQ(crc.Final(), 0u);

    // Re-Final after zero updates must still be zero.
    crc.Reset();
    EXPECT_EQ(crc.Final(), 0u);
}

TEST(Crc32Test, KnownVectorsMatchReference) {
    // RFC 3720 / iSCSI sample vectors for CRC-32C.
    const struct {
        std::string input;
        std::uint32_t expected;
    } cases[] = {
        // 32 zero bytes → 0x8A9136AA
        {std::string(32, '\x00'), 0x8A9136AAu},
        // 32 0xFF bytes → 0x62A8AB43
        {std::string(32, '\xFF'), 0x62A8AB43u},
        // Incrementing bytes 0..31 → 0x46DD794E
        {[]() {
             std::string s;
             s.reserve(32);
             for (int i = 0; i < 32; ++i) s.push_back(static_cast<char>(i));
             return s;
         }(),
         0x46DD794Eu},
        // Decrementing bytes 31..0 → 0x113FDB5C
        {[]() {
             std::string s;
             s.reserve(32);
             for (int i = 31; i >= 0; --i) s.push_back(static_cast<char>(i));
             return s;
         }(),
         0x113FDB5Cu},
    };

    for (const auto& c : cases) {
        Crc32 crc;
        crc.Update(c.input.data(), c.input.size());
        EXPECT_EQ(crc.Final(), c.expected)
            << "for input of size " << c.input.size();
    }
}

TEST(Crc32Test, StreamingEqualsOneShot) {
    // Build a 1 MiB deterministic payload.
    std::vector<std::uint8_t> payload(1 << 20);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i * 31u + 17u);
    }

    Crc32 one_shot;
    one_shot.Update(payload.data(), payload.size());
    const std::uint32_t expected = one_shot.Final();

    // Stream the same data in varying chunk sizes.
    for (std::size_t step : {1u, 7u, 64u, 512u, 4096u, 65537u}) {
        Crc32 streaming;
        for (std::size_t off = 0; off < payload.size(); off += step) {
            const std::size_t n = std::min(step, payload.size() - off);
            streaming.Update(payload.data() + off, n);
        }
        EXPECT_EQ(streaming.Final(), expected)
            << "when streaming with step=" << step;
    }
}

TEST(Crc32Test, ResetClearsState) {
    Crc32 crc;
    const std::string data = "hello, world";
    crc.Update(data.data(), data.size());
    const std::uint32_t first = crc.Final();
    ASSERT_NE(first, 0u);

    crc.Reset();
    crc.Update(data.data(), data.size());
    EXPECT_EQ(crc.Final(), first);
}

}  // namespace
}  // namespace common
}  // namespace xfer
