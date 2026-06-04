// xfer - 接收端实现 (receiver.cpp)
//
// 传输流程 (receiver 视角)：
//   ┌────────────────────────────────────────────────────────────┐
//   │ wire::ReadSessionHeader ← 读取 magic/version/totals        │
//   │ wire::WriteAck    → 回复 flags (当前为 0)            │
//   │                                                            │
//   │ 循环 header.total_files 次：                                 │
//   │   wire::ReadFileHeader → entry                        │
//   │   ValidateAndJoin(output_dir, rel_path) → safe_path         │
//   │   ReceivePayloadToFile → 边接收边写盘 + CRC                 │
//   │                                                            │
//   │ wire::ReadDone ← 读取可选 DONE 标记                           │
//   │                                                            │
//   │ ReceiverSummary → 回传给调用方                                │
//   └────────────────────────────────────────────────────────────┘
//
// 路径安全 (Path Traversal Protection)：
//   * ValidateAndJoin 拒绝 rel_path 中出现 ".."、"/"、"\" 等目录分隔符，
//     防止恶意 sender 将文件写出到 output_dir 之外的路径。
//   * 同时拒绝绝对路径。
//   * 最终路径由 output_dir / rel_path 构成，C++17 的 fs::path::operator/ 会
//     在需要时插入 '/'，对 Windows/Linux/macOS 均正确。
//
// 数据完整性：
//   * 当 verify_crc=true 时，接收边做 CRC-32，与 record 中提供的 crc32 对比；
//     不匹配则报 bad_message，返回 false。

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

// 规范化并校验 rel_path：拒绝 ".."、绝对路径、反斜杠。
// 通过后将 output_dir 与 rel_path 拼接到 safe。
// Normalize and validate `rel_path`: reject absolute paths, "..", and "\".
// Returns true on success and populates `safe` with a cleaned path relative
// to output_dir.
bool ValidateAndJoin(const fs::path& output_dir, const std::string& rel_path,
                     fs::path& safe, std::error_code& ec) {
    fs::path rel(rel_path);
    // 逐段检查：不允许任何一段为 ".."。同时也拒绝裸的 '/'、'\\'。
    for (const auto& part : rel) {
        if (part == ".." || part == "/" || part == "\\") {
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
    }
    // 额外再检查一次绝对路径（例如 "/etc/passwd"）。
    if (rel.is_absolute()) { ec = std::make_error_code(std::errc::invalid_argument); return false; }
    safe = output_dir / rel;
    return true;
}

// 从 sock 读取 size 字节写入 target。边读边更新 CRC。
// expected_crc != 0 时，收完后进行比对；
// expected_crc == 0 时，跳过比对（例如 verify_crc=false 时传入 0）。
bool ReceivePayloadToFile(net::Socket& sock, const fs::path& target, std::uint64_t size,
                          std::uint32_t expected_crc, std::uint32_t& actual_crc_out,
                          std::error_code& ec, common::Progress* progress) {
    std::error_code fs_ec;
    // 如果 target 所在父目录不存在，则创建。
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), fs_ec);
    }
    // ios::trunc 覆盖已有文件，保证幂等。
    std::ofstream os(target, std::ios::binary | std::ios::trunc);
    if (!os) { ec = std::make_error_code(std::errc::permission_denied); return false; }

    static constexpr std::size_t kBufSize = 1 << 20;  // 1 MiB 缓冲区
    thread_local std::vector<char> g_buf(kBufSize);

    common::Crc32 crc;
    std::uint64_t remaining = size;
    while (remaining > 0) {
        // 剩余不足 1 MiB 时，只接收剩余部分。
        std::size_t want = remaining < kBufSize ? static_cast<std::size_t>(remaining) : kBufSize;
        // RecvAll 内部循环 recv(2) 直到收满 want 字节。
        if (!sock.RecvAll(g_buf.data(), want, ec)) return false;
        crc.Update(g_buf.data(), want);
        os.write(g_buf.data(), static_cast<std::streamsize>(want));
        if (!os) { ec = std::make_error_code(std::errc::io_error); return false; }
        remaining -= want;
        if (progress) progress->Add(want);
    }
    actual_crc_out = crc.Final();
    // 只有 expected_crc != 0 才做校验；允许调用方关闭校验。
    if (expected_crc != 0 && actual_crc_out != expected_crc) {
        ec = std::make_error_code(std::errc::bad_message);
        return false;
    }
    return true;
}

}  // namespace

// 接收端主函数：阻塞读取直至 DONE。
bool ReceiveStream(net::Socket& sock,
                   const ReceiverOptions& options,
                   ReceiverSummary& summary,
                   std::error_code& ec) {
    Stopwatch sw;

    // 1) 握手：读取 session header，校验 magic；回写 ack。
    // ---- session handshake ----
    SessionHeader header;
    if (!wire::ReadSessionHeader(sock, header, ec)) {
        XFER_ELOG << "bad session header: " << ec.message();
        return false;
    }
    // ack：回复一个 4 字节的 flags=0。未来可用于协商能力。
    if (!wire::WriteAck(sock, 0, ec)) return false;

    XFER_ILOG << "incoming session: " << header.total_files << " file(s), "
              << common::FormatBytes(header.total_bytes);

    // 2) 确定输出目录
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

    // 3) 逐个文件 record + payload
    // ---- per-file records until DONE ----
    // 严格按 header.total_files 次数读取，而非读到 DONE 才停止，
    // 以防止恶意 sender 注入无限 record 流。
    // We expect exactly header.total_files records, then a DONE.
    for (std::uint64_t i = 0; i < header.total_files; ++i) {
        FileEntry entry;
        if (!wire::ReadFileHeader(sock, entry, ec)) {
            XFER_ELOG << "failed reading file record " << i << ": " << ec.message();
            return false;
        }
        if (entry.rel_path.empty()) {
            // 空路径视为无效：跳过，不报错，保证鲁棒性。
            XFER_WLOG << "skipping file record with empty path";
            continue;
        }
        progress.SetLabel(entry.rel_path);

        // 路径校验：必须在 output_dir 内部。
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

    // 4) 可选的 DONE 标记：即便收不到也不视为致命（允许旧版 sender）。
    // ---- DONE ----
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
