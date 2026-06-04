// xfer - transfer/sender: implementation.
//
// Sender flow:
//   1. CollectPaths(paths, options) -> manifest
//   2. For each entry: CRC-32 of file contents
//   3. wire::WriteSessionHeader -> wire::ReadAck (handshake)
//   4. For each entry:
//        wire::WriteFileHeader(entry) + SendFileBytes(fd, size, ...)
//   5. wire::WriteDone to mark end of stream.
//
// Zero-copy paths:
//   * Linux: sendfile64(2) via net::Socket::SendFromFd.
//   * macOS: sendfile(2) via net::Socket::SendFromFd (different semantics).
//   * Other platforms / --no-zero-copy: 1 MiB thread-local buffer plus
//     pread + SendAll in userspace.

#include "transfer/sender.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <fcntl.h>
#  include <unistd.h>
#endif

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

// ---- helpers ----

// Strip `prefix` from `path` and normalize separators to '/'.
// Also removes leading "./" sequences so the relative path looks clean.
std::string MakeRelPath(const fs::path& path, const fs::path& prefix) {
  fs::path rel;
  std::error_code ec;
  if (!prefix.empty() && fs::exists(prefix, ec)) {
    rel = fs::relative(path, prefix, ec);
  }
  if (rel.empty()) rel = path.filename();

  std::string s = rel.generic_string();
  while (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
  return s;
}

// Open `path` for sequential read; returns the native file descriptor.
int OpenForRead(const std::string& path, std::error_code& ec) {
#if defined(_WIN32)
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    ec = std::make_error_code(std::errc::no_such_file_or_directory);
    return -1;
  }
  return _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_RDONLY);
#else
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    ec = std::make_error_code(std::errc::no_such_file_or_directory);
    return -1;
  }
  return fd;
#endif
}

void CloseFd(int fd) {
#if defined(_WIN32)
  ::_close(fd);
#else
  ::close(fd);
#endif
}

// Stream-compute the CRC-32 of `fd` using a thread-local 1 MiB buffer.
// `fd` is NOT closed; callers retain ownership.
std::uint32_t CrcOfFd(int fd, std::uint64_t size, std::error_code& ec) {
  static constexpr std::size_t kBufSize = 1 << 20;
  thread_local std::vector<char> g_buf(kBufSize);
  common::Crc32 crc;
  std::uint64_t remaining = size;
  std::uint64_t offset = 0;
  while (remaining > 0) {
    std::size_t want = remaining < kBufSize ? static_cast<std::size_t>(remaining) : kBufSize;
#if defined(_WIN32)
    auto n = _read(fd, g_buf.data(), static_cast<unsigned>(want));
#else
    auto n = ::pread(fd, g_buf.data(), want, static_cast<off_t>(offset));
#endif
    if (n < 0) { ec = std::make_error_code(std::errc::io_error); return 0; }
    if (n == 0) break;  // file is shorter than its declared size.
    crc.Update(g_buf.data(), static_cast<std::size_t>(n));
    offset += static_cast<std::uint64_t>(n);
    remaining -= static_cast<std::uint64_t>(n);
  }
  return crc.Final();
}

// Send `size` bytes of `fd` to `sock` at offset 0. Uses the zero-copy kernel
// path when `zero_copy` is true and the platform supports it; otherwise falls
// back to a userspace 1 MiB read+send loop.
bool SendFileBytes(net::Socket& sock, int fd, std::uint64_t size, bool zero_copy,
                   std::error_code& ec, common::Progress* progress) {
  if (size == 0) return true;

  if (zero_copy) {
    std::uint64_t offset = 0;
    std::uint64_t remaining = size;
    while (remaining > 0) {
      std::uint64_t off_before = offset;
      if (!sock.SendFromFd(fd, offset, remaining, ec)) return false;
      std::uint64_t transmitted = offset - off_before;
      if (transmitted == 0) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
      }
      remaining -= transmitted;
      if (progress) progress->Add(transmitted);
    }
    return true;
  }

  static constexpr std::size_t kBufSize = 1 << 20;
  thread_local std::vector<char> g_buf(kBufSize);
  std::uint64_t offset = 0;
  std::uint64_t remaining = size;
  while (remaining > 0) {
    std::size_t want = remaining < kBufSize ? static_cast<std::size_t>(remaining) : kBufSize;
#if defined(_WIN32)
    auto n = _read(fd, g_buf.data(), static_cast<unsigned>(want));
#else
    auto n = ::pread(fd, g_buf.data(), want, static_cast<off_t>(offset));
#endif
    if (n < 0) { ec = std::make_error_code(std::errc::io_error); return false; }
    if (n == 0) break;
    if (!sock.SendAll(g_buf.data(), static_cast<std::size_t>(n), ec)) return false;
    offset += static_cast<std::uint64_t>(n);
    remaining -= static_cast<std::uint64_t>(n);
    if (progress) progress->Add(static_cast<std::uint64_t>(n));
  }
  return remaining == 0;
}

// Map `entry.rel_path` to a real on-disk path using `paths` as candidates.
// Works for: single directory, single file, or multi-source inputs with
// heuristic size-based matching.
std::string ResolveOnDisk(const std::vector<std::string>& paths, const FileEntry& entry,
                          std::error_code& ec) {
  if (paths.size() == 1) {
    fs::path base(paths.front());
    std::error_code fs_ec;
    if (fs::is_directory(base, fs_ec)) return (base / entry.rel_path).string();
    if (fs::is_regular_file(base, fs_ec)) return base.string();
  }
  for (const auto& p : paths) {
    fs::path candidate = fs::path(p).parent_path() / entry.rel_path;
    std::error_code fs_ec;
    if (fs::is_regular_file(candidate, fs_ec) &&
        static_cast<std::uint64_t>(fs::file_size(candidate, fs_ec)) == entry.size) {
      return candidate.string();
    }
  }
  ec = std::make_error_code(std::errc::no_such_file_or_directory);
  return {};
}

}  // namespace

// ---- public API ----

// Scan files/directories into a manifest. Sorted and de-duplicated by rel_path.
std::vector<FileEntry> CollectFiles(const std::vector<std::string>& paths,
                                    const SenderOptions& options,
                                    std::error_code& ec) {
  std::vector<FileEntry> out;
  for (const auto& p : paths) {
    fs::path pp(p);
    std::error_code fs_ec;
    if (!fs::exists(pp, fs_ec) || fs_ec) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return {};
    }
    if (fs::is_regular_file(pp, fs_ec)) {
      FileEntry e;
      auto sz = fs::file_size(pp, fs_ec);
      if (sz == static_cast<std::uintmax_t>(-1)) continue;
      e.size = static_cast<std::uint64_t>(sz);
      fs::path prefix = options.strip_prefix.empty() ? pp.parent_path()
                                                     : fs::path(options.strip_prefix);
      e.rel_path = MakeRelPath(pp, prefix);
      if (e.rel_path.empty()) continue;
      out.push_back(std::move(e));
    } else if (fs::is_directory(pp, fs_ec)) {
      if (!options.recursive) {
        XFER_WLOG << "skipping directory (no --recursive): " << p;
        continue;
      }
      fs::path prefix = options.strip_prefix.empty() ? pp : fs::path(options.strip_prefix);
      auto iter_opts = options.follow_symlinks
                           ? fs::directory_options::follow_directory_symlink
                           : fs::directory_options::none;
      for (auto it = fs::recursive_directory_iterator(pp, iter_opts, fs_ec);
           it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(fs_ec)) {
          FileEntry e;
          auto sz = it->file_size(fs_ec);
          if (sz == static_cast<std::uintmax_t>(-1)) continue;
          e.size = static_cast<std::uint64_t>(sz);
          e.rel_path = MakeRelPath(it->path(), prefix);
          if (e.rel_path.empty()) continue;
          out.push_back(std::move(e));
        }
      }
    }
  }

  // Sort + dedupe so receiver sees a stable order and duplicate inputs
  // do not produce duplicate records.
  std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
    return a.rel_path < b.rel_path;
  });
  out.erase(std::unique(out.begin(), out.end(),
                        [](const FileEntry& a, const FileEntry& b) {
                          return a.rel_path == b.rel_path;
                        }),
            out.end());
  return out;
}

// High-level entry: collect files -> handshake -> push records/payloads -> DONE.
bool SendPaths(net::Socket& sock,
               const std::vector<std::string>& paths,
               const SenderOptions& options,
               std::error_code& ec) {
  auto manifest = CollectFiles(paths, options, ec);
  if (manifest.empty()) {
    XFER_WLOG << "no files to send";
    return true;
  }

  // Pre-compute per-file CRC-32 so records are self-describing.
  for (auto& e : manifest) {
    std::string on_disk = ResolveOnDisk(paths, e, ec);
    if (on_disk.empty()) return false;
    int fd = OpenForRead(on_disk, ec);
    if (fd < 0) return false;
    e.crc32 = CrcOfFd(fd, e.size, ec);
    CloseFd(fd);
    if (ec) return false;
  }

  // ---- session handshake ----
  SessionHeader header;
  header.version = kProtoVersion;
  header.flags = kFlagCrc32;
  header.total_files = manifest.size();
  std::uint64_t total_bytes = 0;
  for (const auto& e : manifest) total_bytes += e.size;
  header.total_bytes = total_bytes;

  if (!wire::WriteSessionHeader(sock, header, ec)) return false;
  std::uint32_t receiver_flags = 0;
  if (!wire::ReadAck(sock, receiver_flags, ec)) return false;

  XFER_ILOG << "session: " << header.total_files << " file(s), "
            << common::FormatBytes(total_bytes);

  Stopwatch sw;
  common::Progress progress("xfer", total_bytes);
  if (options.no_progress) progress.SetEnabled(false);

  // ---- per-file record + payload ----
  for (const auto& entry : manifest) {
    progress.SetLabel(entry.rel_path);
    if (!wire::WriteFileHeader(sock, entry, ec)) return false;

    std::string on_disk = ResolveOnDisk(paths, entry, ec);
    if (on_disk.empty()) return false;
    int fd = OpenForRead(on_disk, ec);
    if (fd < 0) return false;
    bool ok = SendFileBytes(sock, fd, entry.size, options.zero_copy, ec, &progress);
    CloseFd(fd);
    if (!ok) {
      XFER_ELOG << "send failed: " << entry.rel_path << " (" << ec.message() << ")";
      return false;
    }
  }

  if (!wire::WriteDone(sock, ec)) return false;
  progress.Finish();

  double elapsed = sw.Elapsed();
  XFER_ILOG << "done: " << header.total_files << " file(s), "
            << common::FormatBytes(total_bytes) << " in "
            << common::FormatDuration(elapsed)
            << " (" << common::FormatBytes(static_cast<std::uint64_t>(
                              total_bytes / std::max(elapsed, 1e-9)))
            << "/s)";
  return true;
}

// Allow callers to supply a pre-built manifest; useful for unit tests and
// re-sending the same set to multiple receivers.
bool SendFiles(net::Socket& sock,
               const std::vector<std::string>& source_paths,
               const std::vector<FileEntry>& manifest,
               const SenderOptions& options,
               std::error_code& ec) {
  if (manifest.empty()) { XFER_WLOG << "no files to send"; return true; }

  // Validate on-disk paths; fill in missing CRC-32 values.
  std::vector<FileEntry> entries = manifest;
  for (auto& e : entries) {
    if (e.crc32 == 0) {
      std::string on_disk = ResolveOnDisk(source_paths, e, ec);
      if (on_disk.empty()) return false;
      int fd = OpenForRead(on_disk, ec);
      if (fd < 0) return false;
      e.crc32 = CrcOfFd(fd, e.size, ec);
      CloseFd(fd);
      if (ec) return false;
    }
  }

  SessionHeader header;
  header.version = kProtoVersion;
  header.flags = kFlagCrc32;
  header.total_files = entries.size();
  std::uint64_t total_bytes = 0;
  for (const auto& e : entries) total_bytes += e.size;
  header.total_bytes = total_bytes;

  if (!wire::WriteSessionHeader(sock, header, ec)) return false;
  std::uint32_t receiver_flags = 0;
  if (!wire::ReadAck(sock, receiver_flags, ec)) return false;

  XFER_ILOG << "session: " << header.total_files << " file(s), "
            << common::FormatBytes(total_bytes);
  Stopwatch sw;
  common::Progress progress("xfer", total_bytes);
  if (options.no_progress) progress.SetEnabled(false);

  for (const auto& entry : entries) {
    progress.SetLabel(entry.rel_path);
    if (!wire::WriteFileHeader(sock, entry, ec)) return false;
    std::string on_disk = ResolveOnDisk(source_paths, entry, ec);
    if (on_disk.empty()) return false;
    int fd = OpenForRead(on_disk, ec);
    if (fd < 0) return false;
    bool ok = SendFileBytes(sock, fd, entry.size, options.zero_copy, ec, &progress);
    CloseFd(fd);
    if (!ok) return false;
  }

  if (!wire::WriteDone(sock, ec)) return false;
  progress.Finish();
  XFER_ILOG << "done: " << header.total_files << " file(s), "
            << common::FormatBytes(total_bytes) << " in "
            << common::FormatDuration(sw.Elapsed());
  return true;
}

}  // namespace transfer
}  // namespace xfer
