// Human-readable byte-count and duration formatters.
//
// FormatBytes uses base-1024 (KiB/MiB/GiB/...) to match `du`/`ls -lh`.
// FormatDuration picks a natural unit (ms/s/min/h) based on magnitude.
#ifndef XFER_COMMON_FORMAT_H
#define XFER_COMMON_FORMAT_H

#include <cstdint>
#include <string>

namespace xfer {
namespace common {

std::string FormatBytes(std::uint64_t bytes);
std::string FormatDuration(double seconds);

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_FORMAT_H
