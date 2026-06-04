// xfer - transfer/receiver: high-level receive API.
//
// Receiver flow:
//   1. Read session header, validate magic + CRC; send Ack.
//   2. Read exactly `total_files` records (bounds the loop against
//      malicious senders).
//   3. For each record: validate rel_path (reject ".." and absolute paths),
//      then stream payload bytes into the target file while computing CRC-32.
//   4. Read the optional DONE marker.
//   5. Populate ReceiverSummary with stats (total files/bytes/paths/elapsed).
//
// Security notes:
//   * ValidateAndJoin rejects paths containing ".." or absolute paths;
//     files are always written under `output_dir`.
//   * The number of records consumed is capped by `session_header.total_files`
//     so an endless stream cannot be forced.

#ifndef XFER_TRANSFER_RECEIVER_H
#define XFER_TRANSFER_RECEIVER_H

#include <string>
#include <system_error>
#include <vector>

#include "net/socket.h"
#include "transfer/protocol.h"

namespace xfer {
namespace transfer {

// Receiver runtime options.
struct ReceiverOptions {
  std::string output_dir;  // Root directory for written files.
  bool create_dir = true;  // Create `output_dir` if it does not exist.
  bool no_progress = false;  // Suppress inline progress bar.
  bool verify_crc = true;    // Verify per-file CRC-32 against the record value.
};

// Receiver statistics returned after a successful session.
struct ReceiverSummary {
  std::uint64_t total_files = 0;
  std::uint64_t total_bytes = 0;
  std::vector<std::string> written_paths;
  double elapsed_seconds = 0;
};

// Blocking receive loop. Reads session header, per-file records + payloads,
// and the DONE trailer. Writes files to `options.output_dir`. Returns false on
// any error; `ec` contains the detail.
bool ReceiveStream(net::Socket& sock,
                   const ReceiverOptions& options,
                   ReceiverSummary& summary,
                   std::error_code& ec);

}  // namespace transfer
}  // namespace xfer

#endif  // XFER_TRANSFER_RECEIVER_H
