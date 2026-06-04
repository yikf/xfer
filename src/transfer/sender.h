// xfer - transfer/sender: high-level send API.
//
// Responsibility
//   1. Collect files/directories into a FileEntry manifest.
//   2. Pre-compute CRC-32 for each file.
//   3. Push the session header, per-file records + payloads, and DONE marker
//      through the connected socket.
//
// Key implementation notes:
//   * On Linux / macOS, file payloads are sent via sendfile(2) for zero-copy;
//     --no-zero-copy falls back to a userspace pread + send loop.
//   * Paths on disk are resolved separately from the wire records so that
//     relative-path metadata (relative to options.strip_prefix) can differ
//     from the on-disk location.

#ifndef XFER_TRANSFER_SENDER_H
#define XFER_TRANSFER_SENDER_H

#include <string>
#include <system_error>
#include <vector>

#include "net/socket.h"
#include "transfer/protocol.h"

namespace xfer {
namespace transfer {

// Options controlling file collection and the transport path.
struct SenderOptions {
  bool recursive = false;          // Descend into directories (--recursive)
  bool follow_symlinks = false;    // Follow symlinks during directory walk
  bool zero_copy = true;           // Use sendfile(2) when available
  bool no_progress = false;        // Suppress the inline progress display
  std::string strip_prefix;        // Prefix stripped from each file's path
                                   // before sending.
};

// Scan `paths` (files or directories) and build the file manifest.
// Directories are recursed into only when `options.recursive` is true.
std::vector<FileEntry> CollectFiles(const std::vector<std::string>& paths,
                                    const SenderOptions& options,
                                    std::error_code& ec);

// Send the files described by the manifest through `sock`.
// Writes the session header, per-file records + payloads, and the DONE marker.
// `source_paths` are used for on-disk lookup (may differ from rel_path).
bool SendFiles(net::Socket& sock,
               const std::vector<std::string>& source_paths,
               const std::vector<FileEntry>& manifest,
               const SenderOptions& options,
               std::error_code& ec);

// High-level helper that combines CollectFiles + SendFiles.
bool SendPaths(net::Socket& sock,
               const std::vector<std::string>& paths,
               const SenderOptions& options,
               std::error_code& ec);

}  // namespace transfer
}  // namespace xfer

#endif  // XFER_TRANSFER_SENDER_H
