// xfer - transfer/receiver: implementation.
//
// Receiver flow (as seen from this file):
//   1. wire::ReadSessionHeader - read magic/version/totals; verify CRC.
//   2. wire::WriteAck        - reply 4-byte flags; reserved for capability
//                              negotiation.
//   3. Loop exactly `header.total_files` times:
//        wire::ReadFileHeader   -> FileEntry
//        ValidateAndJoin        -> safe on-disk path
//        ReceivePayloadToFile   -> stream bytes, compute CRC, verify
//   4. wire::ReadDone        - optional; log warning if missing.
//   5. Populate ReceiverSummary and return.

#include "transfer/receiver.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "common/crc32.h"
#include "common/format.h"
#include "common/logging.h"
#include "common/progress.h"
#include "common/stopwatch.h"
#include "net/socket.h"
#include "transfer/protocol.h"
#include "xfer/protocol.h"

using xfer::common::Stopwatch;
namespace fs = std::filesystem;

namespace xfer {
namespace transfer {

namespace {

// Validate `rel_path` against path-traversal attacks; join with `output_dir`
// to produce a cleaned absolute path. Rejects ".." components, absolute paths,
// and bare separators ('/' or '\').
bool ValidateAndJoin(const fs::path& output_dir, const std::string& rel_path,
                     fs::path& safe, std::error_code& ec) {
  fs::path rel(rel_path);
  for (const auto& part : rel) {
    if (part == ".." || part == "/" || part == "\\") {
      ec = std::make_error_code(std::errc::invalid_argument);
      return false;
    }
  }
  if (rel.is_absolute()) { ec = std::make_error_code(std::errc::invalid_argument); return false; }
  safe = output_dir / rel;
  return true;
}

// Read `size` bytes from `sock`, write them to `target`, and CRC-32 the data
// as it flows through. If `expected_crc` is non-zero the final CRC is
// compared; otherwise verification is skipped (caller sets expected_crc=0
// when verify_crc is disabled globally).
bool ReceivePayloadToFile(net::Socket& sock, const fs::path& target, std::uint64_t size,
                          std::uint32_t expected_crc, std::uint32_t& actual_crc_out,
                          std::error_code& ec, common::Progress* progress) {
  std::error_code fs_ec;
  // Ensure the parent directory exists (e.g. rel_path = "subdir/file").
  if (target.has_parent_path()) {
    fs::create_directories(target.parent_path(), fs_ec);
  }
  // Open with truncation so repeated sessions are idempotent.
  std::ofstream os(target, std::ios::binary | std::ios::trunc);
  if (!os) { ec = std::make_error_code(std::errc::permission_denied); return false; }

  static constexpr std::size_t kBufSize = 1 << 20;  // 1 MiB
  thread_local std::vector<char> g_buf(kBufSize);

  common::Crc32 crc;
  std::uint64_t remaining = size;
  while (remaining > 0) {
    std::size_t want = remaining < kBufSize ? static_cast<std::size_t>(remaining) : kBufSize;
    if (!sock.RecvAll(g_buf.data(), want, ec)) return false;
    crc.Update(g_buf.data(), want);
    os.write(g_buf.data(), static_cast<std::streamsize>(want));
    if (!os) { ec = std::make_error_code(std::errc::io_error); return false; }
    remaining -= want;
    if (progress) progress->Add(want);
  }
  actual_crc_out = crc.Final();
  if (expected_crc != 0 && actual_crc_out != expected_crc) {
    ec = std::make_error_code(std::errc::bad_message);
    return false;
  }
  return true;
}

}  // namespace

// Blocking entry point. `summary` is only populated on success.
bool ReceiveStream(net::Socket& sock,
                   const ReceiverOptions& options,
                   ReceiverSummary& summary,
                   std::error_code& ec) {
  Stopwatch sw;

  // ---- session handshake ----
  SessionHeader header;
  if (!wire::ReadSessionHeader(sock, header, ec)) {
    XFER_ELOG << "bad session header: " << ec.message();
    return false;
  }
  if (!wire::WriteAck(sock, 0, ec)) return false;

  XFER_ILOG << "incoming session: " << header.total_files << " file(s), "
            << common::FormatBytes(header.total_bytes);

  // ---- output directory ----
  fs::path output_dir = options.output_dir.empty() ? fs::current_path() : fs::path(options.output_dir);
  std::error_code fs_ec;
  if (options.create_dir) fs::create_directories(output_dir, fs_ec);
  if (!fs::is_directory(output_dir, fs_ec)) {
    XFER_ELOG << "output directory does not exist: " << output_dir.string();
    ec = std::make_error_code(std::errc::not_a_directory);
    return false;
  }

  common::Progress progress("recv", header.total_bytes);
  if (options.no_progress) progress.SetEnabled(false);

  // ---- per-file records; bounded by total_files to prevent infinite streams ----
  for (std::uint64_t i = 0; i < header.total_files; ++i) {
    FileEntry entry;
    if (!wire::ReadFileHeader(sock, entry, ec)) {
      XFER_ELOG << "failed reading file record " << i << ": " << ec.message();
      return false;
    }
    if (entry.rel_path.empty()) {
      XFER_WLOG << "skipping file record with empty path";
      continue;
    }
    progress.SetLabel(entry.rel_path);

    fs::path safe;
    if (!ValidateAndJoin(output_dir, entry.rel_path, safe, ec)) {
      XFER_ELOG << "rejecting unsafe path: " << entry.rel_path;
      return false;
    }

    std::uint32_t actual_crc = 0;
    if (!ReceivePayloadToFile(sock, safe, entry.size,
                              options.verify_crc ? entry.crc32 : 0,
                              actual_crc, ec, &progress)) {
      XFER_ELOG << "failed writing " << entry.rel_path << ": " << ec.message();
      return false;
    }

    summary.total_files += 1;
    summary.total_bytes += entry.size;
    summary.written_paths.push_back(safe.string());
    XFER_DLOG << "wrote " << entry.rel_path << " (" << common::FormatBytes(entry.size)
              << ", crc=" << std::hex << actual_crc << std::dec << ")";
  }

  // ---- DONE trailer ----
  if (!wire::ReadDone(sock, ec)) {
    XFER_WLOG << "missing DONE trailer from sender";
  }

  progress.Finish();
  summary.elapsed_seconds = sw.Elapsed();
  XFER_ILOG << "session complete: " << summary.total_files << " file(s), "
            << common::FormatBytes(summary.total_bytes) << " in "
            << common::FormatDuration(summary.elapsed_seconds);
  return true;
}

}  // namespace transfer
}  // namespace xfer
