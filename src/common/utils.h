#ifndef XFER_COMMON_UTILS_H
#define XFER_COMMON_UTILS_H

#include <cstdint>
#include <string>

namespace xfer {

// Human-friendly byte-count formatter (1024.0 -> "1.0 KiB").
std::string FormatBytes(std::uint64_t bytes);

}  // namespace xfer

#endif  // XFER_COMMON_UTILS_H
