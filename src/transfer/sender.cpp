// xfer - 发送端实现 (sender.cpp)
//
// 传输流程 (sender 视角)：
//   ┌────────────────────────────────────────────────────────────┐
//   │ CollectFiles(paths, options)                               │
//   │   └─ 递归扫描磁盘，得到 FileEntry 列表 (rel_path / size)      │
//   │                                                            │
//   │ 对每个 entry:                                               │
//   │   └─ CrcOfFd(fd, size) → 预计算 CRC-32                     │
//   │                                                            │
//   │ wire::WriteSessionHeader → 发送 magic/version/flags/totals  │
//   │ wire::ReadAck    ← 等待 receiver 确认                 │
//   │                                                            │
//   │ 对每个 entry:                                               │
//   │   └─ wire::WriteFileHeader(entry)                    │
//   │   └─ SendFileBytes(fd, entry.size, zero_copy)              │
//   │                                                            │
//   │ wire::WriteDone → 标记传输结束                                │
//   └────────────────────────────────────────────────────────────┘
//
// 零拷贝：
//   * Linux: sendfile(2) 直接从文件 fd 推到 socket fd；
//   * macOS: sendfile(2) 语义略有不同（见 net/socket.cpp）；
//   * Windows 或强制 --no-zero-copy：使用 1 MiB 的 thread_local 缓冲区，
//     pread + SendAll 回用户态。
//
// 线程安全：
//   * 每个线程使用自己的 thread_local 缓冲区，避免 malloc/free 热点；
//   * 本模块不提供多线程同时写入同一个 Socket 的保护，由上层保证。

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

// 匿名命名空间：实现细节，对外不可见。
namespace {

// ---- helpers ----

// 从 path 中去掉 prefix，并将分隔符统一为 '/'。
// 另外会清理掉开头的 "./"，防止 receiver 认为路径不合法。
// Strip `prefix` from `path` and normalize separators to '/'.
std::string MakeRelPath(const fs::path& path, const fs::path& prefix) {
    fs::path rel;
    std::error_code ec;
    if (!prefix.empty() && fs::exists(prefix, ec)) {
        // fs::relative(path, base) 可得到相对于 base 的路径，
        // 对跨平台和规范化都有帮助。
        rel = fs::relative(path, prefix, ec);
    }
    // 如果前缀不可用，则退化为仅保留文件名。
    if (rel.empty()) rel = path.filename();

    std::string s = rel.generic_string();
    // 去掉前导的 "./" 重复出现，使路径更干净。
    while (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    return s;
}

// 以 O_RDONLY 打开文件，返回 int fd（POSIX 风格），Windows 上使用
// CreateFile + _open_osfhandle 桥接。错误时返回 -1 并设置 ec。
// Open a file with the OS-native open() so we can feed its descriptor into
// sendfile().  Returns -1 on error.
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

// 关闭由 OpenForRead 打开的 fd。
void CloseFd(int fd) {
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
}

// 对一个已打开的 fd 流式计算 CRC-32，fd **不会**被关闭。
// 使用 thread_local 的 1 MiB 缓冲区，避免每次分配。
// Compute CRC-32 of a file, streaming through `fd`. `fd` is NOT closed.
std::uint32_t CrcOfFd(int fd, std::uint64_t size, std::error_code& ec) {
    static constexpr std::size_t kBufSize = 1 << 20;  // 1 MiB 单次读
    // thread_local：首次访问时构造，线程退出时析构。
    // 对高并发场景可避免每个线程都反复分配缓冲区。
    thread_local std::vector<char> g_buf(kBufSize);
    common::Crc32 crc;
    std::uint64_t remaining = size;
    std::uint64_t offset = 0;
    while (remaining > 0) {
        // 剩余字节小于 kBufSize 时只取剩余部分。
        std::size_t want = remaining < kBufSize ? static_cast<std::size_t>(remaining) : kBufSize;
#if defined(_WIN32)
        auto n = _read(fd, g_buf.data(), static_cast<unsigned>(want));
#else
        // pread 不会改变文件偏移，对并发/重入更友好。
        auto n = ::pread(fd, g_buf.data(), want, static_cast<off_t>(offset));
#endif
        if (n < 0) { ec = std::make_error_code(std::errc::io_error); return 0; }
        if (n == 0) break;  // 文件比声明的 size 小；停止。
        crc.Update(g_buf.data(), static_cast<std::size_t>(n));
        offset += static_cast<std::uint64_t>(n);
        remaining -= static_cast<std::uint64_t>(n);
    }
    return crc.Final();
}

// 将 fd 中 size 字节写入 sock。支持两条路径：
//   * zero_copy=true：调用 Socket::SendFromFd，内核态直接搬运；
//   * zero_copy=false：用户态 1 MiB 缓冲区，pread + SendAll。
// progress 用于更新进度条，可为 nullptr。
//
// Send `size` bytes from `fd` to `sock` starting at offset 0. When `zero_copy`
// is true, uses kernel-level sendfile(); otherwise falls back to userspace
// read/send loops.  When `crc_out` is non-null, computes a CRC over the data
// as it passes through (only on the userspace path; zero-copy precomputes).
bool SendFileBytes(net::Socket& sock, int fd, std::uint64_t size, bool zero_copy,
                   std::error_code& ec, common::Progress* progress) {
    // 空文件：直接返回成功。
    if (size == 0) return true;

    if (zero_copy) {
        // 零拷贝路径：SendFromFd 内部可能需要多次调用才能把 size 字节全部送出。
        // 此处以 offset 为游标，每次推进 offset，直到传输完成。
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

    // 用户态路径：1 MiB 缓冲区复用。
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
        // SendAll 会循环调用 send(2) 直到 n 字节全部发出。
        if (!sock.SendAll(g_buf.data(), static_cast<std::size_t>(n), ec)) return false;
        offset += static_cast<std::uint64_t>(n);
        remaining -= static_cast<std::uint64_t>(n);
        if (progress) progress->Add(static_cast<std::uint64_t>(n));
    }
    return remaining == 0;
}

// 根据 manifest 中的 rel_path，在原始 source_paths 中找到实际磁盘路径。
// 策略：
//   * paths.size() == 1 时，若是目录则拼接；若是文件则直接使用；
//   * 否则遍历 paths，按 size 匹配猜测文件（鲁棒性考虑）。
// Resolve manifest entry back to on-disk path from the originating `paths`.
std::string ResolveOnDisk(const std::vector<std::string>& paths, const FileEntry& entry,
                          std::error_code& ec) {
    if (paths.size() == 1) {
        fs::path base(paths.front());
        std::error_code fs_ec;
        if (fs::is_directory(base, fs_ec)) return (base / entry.rel_path).string();
        if (fs::is_regular_file(base, fs_ec)) return base.string();
    }
    for (const auto& p : paths) {
        // 将 paths 中的每一项视作"候选目录或父目录的父"，按 rel_path 拼出路径。
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

// 扫描文件/目录生成 manifest。
// 关键点：
//   * 对每个 path：是普通文件 → 直接加入；是目录 → 根据 recursive 决定是否递归。
//   * 遍历完后按 rel_path 排序并去重，避免同一个 rel_path 被发送两次。
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
            // 单个文件：取出 size，计算相对路径（相对其所在目录或 strip_prefix）。
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
            // recursive_directory_iterator 以深度优先遍历目录树。
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

    // 按 rel_path 排序并去重，使 receiver 得到稳定的 manifest 顺序。
    // Sort and dedupe by relative path so the receiver sees a stable manifest.
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

// 发送主流程（最常用入口）。
bool SendPaths(net::Socket& sock,
               const std::vector<std::string>& paths,
               const SenderOptions& options,
               std::error_code& ec) {
    // 1) 扫描 manifest
    auto manifest = CollectFiles(paths, options, ec);
    if (manifest.empty()) {
        XFER_WLOG << "no files to send";
        return true;
    }

    // 2) 为每个文件预计算 CRC-32（需要磁盘读）
    // ---- compute per-file CRC-32 (needed for the record header) ----
    for (auto& e : manifest) {
        std::string on_disk = ResolveOnDisk(paths, e, ec);
        if (on_disk.empty()) return false;
        int fd = OpenForRead(on_disk, ec);
        if (fd < 0) return false;
        e.crc32 = CrcOfFd(fd, e.size, ec);
        CloseFd(fd);
        if (ec) return false;
    }

    // 3) session header + ack
    // ---- session header ----
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

    // 4) 逐个文件发送 record + payload
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

    // 5) 结束标记
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

// 保留一个底层入口：允许调用方自己构造 manifest，然后复用同一套传输流水线。
// Keep the lower-level SendFiles() around so callers that already built a
// manifest can reuse the same pipeline.  Paths are used for on-disk lookup.
bool SendFiles(net::Socket& sock,
               const std::vector<std::string>& source_paths,
               const std::vector<FileEntry>& manifest,
               const SenderOptions& options,
               std::error_code& ec) {
    if (manifest.empty()) { XFER_WLOG << "no files to send"; return true; }

    // Validate on-disk paths exist, fill in CRCs if missing.
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
