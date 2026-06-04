// End-to-end roundtrip test.
//
// Spawns a receiver on a background thread that binds to an ephemeral port
// and waits for one session.  Then the main thread pushes a set of small
// deterministic files through the sender side and compares the received
// copies byte-for-byte against the originals.

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"

#include "common/crc32.h"
#include "net/socket.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

namespace fs = std::filesystem;

namespace xfer {
namespace transfer {
namespace {

// Scratch directory created per test for source + destination files.
class RoundtripFixture : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_ = fs::temp_directory_path() /
               ("xfer-gtest-" + std::to_string(::getpid()) + "-" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count()));
        src_ = tmp_ / "src";
        dst_ = tmp_ / "dst";
        fs::create_directories(src_);
        fs::create_directories(dst_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmp_, ec);
    }

    fs::path tmp_;
    fs::path src_;
    fs::path dst_;
};

// Write `bytes` bytes of deterministic pseudo-random data to `path`.
void WriteRandomFile(const fs::path& path, std::size_t bytes, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::ofstream os(path, std::ios::binary);
    ASSERT_TRUE(os) << "cannot write " << path;
    std::vector<std::uint8_t> buf(4096);
    for (std::size_t written = 0; written < bytes;) {
        const std::size_t n = std::min(buf.size(), bytes - written);
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = static_cast<std::uint8_t>(dist(rng));
        os.write(reinterpret_cast<const char*>(buf.data()),
                 static_cast<std::streamsize>(n));
        written += n;
    }
    ASSERT_TRUE(os);
}

std::uint32_t Crc32Of(const fs::path& path) {
    std::ifstream is(path, std::ios::binary);
    EXPECT_TRUE(is) << "cannot read " << path;
    common::Crc32 crc;
    std::vector<char> buf(4096);
    while (is.read(buf.data(), buf.size()) || is.gcount() > 0) {
        crc.Update(buf.data(), static_cast<std::size_t>(is.gcount()));
    }
    return crc.Final();
}

TEST_F(RoundtripFixture, FilesArriveUnchanged) {
    // Create source files: a tiny file, a medium file, and a file that is
    // large enough to exercise the streaming path.
    const std::vector<std::pair<std::string, std::size_t>> plan = {
        {"small.bin", 1024},
        {"medium.bin", 64 * 1024},
        {"subdir/large.bin", 1024 * 1024},
    };

    std::vector<std::pair<fs::path, std::uint32_t>> source_crcs;
    for (const auto& [name, size] : plan) {
        fs::path p = src_ / name;
        fs::create_directories(p.parent_path());
        WriteRandomFile(p, size, static_cast<unsigned>(size * 1315423911u + 7));
        source_crcs.emplace_back(name, Crc32Of(p));
    }

    // Pick an ephemeral port.
    int probe = ::socket(AF_INET6, SOCK_STREAM, 0);
    ASSERT_GE(probe, 0);
    struct sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = 0;
    addr.sin6_addr = in6addr_any;
    socklen_t len = sizeof(addr);
    ASSERT_EQ(::bind(probe, reinterpret_cast<struct sockaddr*>(&addr),
                     sizeof(addr)),
              0);
    ASSERT_EQ(::getsockname(probe,
                            reinterpret_cast<struct sockaddr*>(&addr), &len),
              0);
    const int port = ntohs(addr.sin6_port);
    ::close(probe);

    // Receiver thread: bind → accept → receive → summary.
    std::atomic<bool> recv_ok{false};
    std::atomic<std::uint64_t> recv_files{0};
    std::thread receiver([&]() {
        std::error_code ec;
        net::Socket listener;
        if (!listener.Bind(port, ec)) return;
        if (!listener.Listen(16, ec)) return;
        net::Socket client = listener.Accept(ec);
        if (!client.Valid()) return;
        ReceiverOptions opts;
        opts.output_dir = dst_.string();
        opts.verify_crc = true;
        opts.no_progress = true;
        ReceiverSummary summary;
        if (ReceiveStream(client, opts, summary, ec)) {
            recv_files = summary.total_files;
            recv_ok = true;
        }
    });

    // Give the receiver a moment to enter accept(2).
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Sender: connect → push files.
    {
        std::error_code ec;
        net::Socket sock;
        ASSERT_TRUE(sock.Connect("127.0.0.1", port, ec))
            << "connect failed: " << ec.message();
        SenderOptions sopts;
        sopts.recursive = true;
        sopts.zero_copy = true;
        sopts.no_progress = true;
        std::vector<std::string> paths{src_.string()};
        ASSERT_TRUE(SendPaths(sock, paths, sopts, ec))
            << "send failed: " << ec.message();
    }

    receiver.join();
    ASSERT_TRUE(recv_ok.load());
    ASSERT_EQ(recv_files.load(), static_cast<std::uint64_t>(plan.size()));

    // Verify: each source file has a matching file in the destination with
    // the same CRC.
    for (const auto& [rel, expected] : source_crcs) {
        const fs::path got = dst_ / rel;
        ASSERT_TRUE(fs::exists(got)) << "missing received file: " << got;
        EXPECT_EQ(Crc32Of(got), expected) << "crc mismatch for " << rel;
    }
}

}  // namespace
}  // namespace transfer
}  // namespace xfer
