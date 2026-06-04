// xfer - 端到端回环测试 (tests/test_roundtrip.cpp)
//
// 场景：
//   1) 在临时目录下生成两个测试文件：小文件 (1 KiB) 和大文件 (4 MiB)；
//   2) receiver 线程：监听一个随机端口，接受一次连接，接收并写入 dst/；
//   3) 主线程 sender：连接到相同端口，发送 src/ 目录；
//   4) 验证：dst/ 下的文件与 src/ 一致（通过 CRC-32 比对）；
//   5) 成功返回 0，失败返回 1。
//
// 注意：使用 POSIX socket API 创建监听套接字（AF_INET6，IPv4-mapped 支持），
// xfer 的 net::Socket 类用于实际传输。
//
// End-to-end loopback test: spins up a receiver thread and a sender thread,
// transfers two test files, then compares CRC-32 on both sides.

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include "common/crc32.h"
#include "common/format.h"
#include "net/socket.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

namespace fs = std::filesystem;

namespace {

// 生成 n 字节的伪随机数据（固定 seed，可复现）。
// Generate n bytes of deterministic pseudo-random data (reproducible seed).
std::vector<std::uint8_t> RandomBytes(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(dist(rng));
    return v;
}

// 写入二进制文件到 path。
bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    os.write(reinterpret_cast<const char*>(data.data()), data.size());
    return os.good();
}

// 读取整个文件到 vector<uint8_t>。
std::vector<std::uint8_t> ReadFile(const std::string& path) {
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is) return {};
    auto size = static_cast<std::size_t>(is.tellg());
    is.seekg(0);
    std::vector<std::uint8_t> out(size);
    is.read(reinterpret_cast<char*>(out.data()), size);
    return out;
}

// 对一个 vector 做 CRC-32。
std::uint32_t CrcOf(const std::vector<std::uint8_t>& v) {
    xfer::common::Crc32 c;
    c.Update(v.data(), v.size());
    return c.Final();
}

// 在本机选一个可用端口：创建 IPv6 socket bind to port 0，
// 读取系统分配的端口号后立即关闭。
// 该方法在单进程、短时间内是可靠的（用于回环测试）。
int PickFreePort() {
    int s = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = 0;
    addr.sin6_addr = in6addr_any;
    socklen_t len = sizeof(addr);
    if (::bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 &&
        ::getsockname(s, reinterpret_cast<struct sockaddr*>(&addr), &len) == 0) {
        int port = ntohs(addr.sin6_port);
        ::close(s);
        return port;
    }
    ::close(s);
    return 0;
}

}  // namespace

int main() {
    // 准备工作目录：/tmp/xfer-test-<pid> 下 src/ 和 dst/。
    fs::path tmp = fs::temp_directory_path() / ("xfer-test-" + std::to_string(::getpid()));
    fs::path src = tmp / "src";
    fs::path dst = tmp / "dst";
    fs::create_directories(src);
    fs::create_directories(dst);

    // 1 KiB + 4 MiB：覆盖小文件和大文件两种场景。
    auto small = RandomBytes(1024, 0xC0FFEE);
    auto big   = RandomBytes(4 * 1024 * 1024, 0xBEEF);
    if (!WriteFile((src / "small.bin").string(), small) ||
        !WriteFile((src / "big.bin").string(), big)) {
        std::fprintf(stderr, "cannot write source files\n");
        return 1;
    }
    std::uint32_t small_crc = CrcOf(small);
    std::uint32_t big_crc   = CrcOf(big);

    // 选一个可用端口。
    int port = PickFreePort();
    if (port == 0) { std::fprintf(stderr, "cannot pick free port\n"); return 1; }
    std::fprintf(stderr, "[test] port = %d (src=%s dst=%s)\n",
                 port, src.c_str(), dst.c_str());

    // ---- receiver in a thread ----
    // receiver 线程：Bind+Listen+Accept，然后调用 ReceiveStream 接收。
    bool recv_ok = false;
    std::thread receiver([port, &dst, &recv_ok] {
        std::error_code ec;
        xfer::net::Socket listener;
        if (!listener.Bind(port, ec)) {
            std::fprintf(stderr, "recv bind failed: %s\n", ec.message().c_str());
            return;
        }
        if (!listener.Listen(16, ec)) { return; }
        auto sock = listener.Accept(ec);
        if (!sock.Valid()) { return; }
        xfer::transfer::ReceiverOptions opts;
        opts.output_dir = dst.string();
        opts.verify_crc = true;
        opts.no_progress = true;
        xfer::transfer::ReceiverSummary summary;
        if (xfer::transfer::ReceiveStream(sock, opts, summary, ec)) {
            recv_ok = (summary.total_files == 2);
        }
    });

    // 等待 receiver 线程进入 Accept 状态：200ms 经验值。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ---- sender ----
    std::error_code ec;
    xfer::net::Socket sock;
    if (!sock.Connect("127.0.0.1", port, ec)) {
        std::fprintf(stderr, "sender connect failed: %s\n", ec.message().c_str());
        receiver.join();
        return 1;
    }
    xfer::transfer::SenderOptions snd;
    snd.recursive = true;
    snd.zero_copy = true;
    snd.no_progress = true;
    std::vector<std::string> paths{src.string()};
    if (!xfer::transfer::SendPaths(sock, paths, snd, ec)) {
        std::fprintf(stderr, "send failed: %s\n", ec.message().c_str());
        receiver.join();
        return 1;
    }
    receiver.join();
    if (!recv_ok) {
        std::fprintf(stderr, "receiver reported failure\n");
        return 1;
    }

    // ---- 验证：读取 dst 下的文件，对比 CRC ----
    auto got_small = ReadFile((dst / "small.bin").string());
    auto got_big   = ReadFile((dst / "big.bin").string());
    if (got_small.empty() || got_big.empty()) {
        std::fprintf(stderr, "missing files after transfer\n");
        return 1;
    }
    if (CrcOf(got_small) != small_crc || CrcOf(got_big) != big_crc) {
        std::fprintf(stderr, "crc mismatch\n");
        return 1;
    }
    std::fprintf(stderr, "[test] roundtrip OK: small=%s big=%s\n",
                 xfer::common::FormatBytes(small.size()).c_str(),
                 xfer::common::FormatBytes(big.size()).c_str());

    // 清理工作目录。
    fs::remove_all(tmp);
    return 0;
}
