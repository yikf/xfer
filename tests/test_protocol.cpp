// Wire-protocol round-trip tests.
//
// We serialize each message type into an in-memory buffer and then read it
// back, comparing the decoded fields against what was written.  This keeps
// the protocol implementation honest across platforms with different
// integer byte-orders (the protocol is defined to be little-endian).

#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <sys/socket.h>
#  include <unistd.h>
#else
#  include <io.h>
#endif

#include "gtest/gtest.h"
#include "net/socket.h"
#include "transfer/protocol.h"

namespace xfer {
namespace transfer {
namespace {

// A Socket subclass backed by an in-memory byte buffer.  It exposes the
// same SendAll/RecvAll interface as a real socket but with no syscalls,
// so tests run deterministically and fast.
class LoopbackSocket {
public:
    net::Socket& AsSendSink() { return send_sock_; }
    net::Socket& AsRecvSource() { return recv_sock_; }

    // Push bytes into the receive buffer (used by the "sender" side).
    void Push(const void* data, std::size_t n) {
        const char* p = static_cast<const char*>(data);
        recv_buf_.insert(recv_buf_.end(), p, p + n);
    }

    // Pull bytes from the receive buffer (used by the "receiver" side).
    std::size_t Pull(void* out, std::size_t n) {
        const std::size_t got = std::min(n, recv_buf_.size());
        std::memcpy(out, recv_buf_.data(), got);
        recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + got);
        return got;
    }

    std::size_t Available() const { return recv_buf_.size(); }

private:
    // NOTE: we only use net::Socket for type compatibility with the wire
    // helpers; actual I/O is driven by Push/Pull above.  The real socket
    // fds inside are never bound or connected.
    net::Socket send_sock_;
    net::Socket recv_sock_;
    std::vector<char> recv_buf_;
};

// Minimal adapter that mimics the SendAll/RecvAll interface of net::Socket
// but routes bytes through a LoopbackSocket.  This lets us reuse wire::
// helpers verbatim in tests.
class TestWire {
public:
    explicit TestWire(LoopbackSocket& lb) : lb_(lb) {}

    bool SendAll(const void* data, std::size_t len, std::error_code&) {
        lb_.Push(data, len);
        return true;
    }

    bool RecvAll(void* data, std::size_t len, std::error_code& ec) {
        if (lb_.Pull(data, len) != len) {
            ec = std::make_error_code(std::errc::connection_reset);
            return false;
        }
        return true;
    }

    // Passthrough overloads so wire::WriteXxx / wire::ReadXxx can pick up
    // SendAll / RecvAll via ADL on the wrapped type.  (Not strictly needed
    // because the helpers already take the Socket by reference; kept for
    // documentation.)

private:
    LoopbackSocket& lb_;
};

// Manually-drive helpers: wire:: helpers accept a Socket&, so we build a
// Socket-like wrapper that records bytes into a LoopbackSocket.  The
// cleanest approach is to build a small class derived from net::Socket
// that overrides nothing and instead feeds bytes through its FD.  We take
// the simpler approach below: build a dedicated helper for each
// (write-then-read) pattern using direct byte buffers.

// ----- Actual wire-level tests using byte-buffer I/O. -----

// We re-implement a minimal "Socket" in-process by linking against the real
// net::Socket class but overriding its file descriptor with an anonymous
// pipe.  This is more realistic than a pure buffer and exercises the exact
// same code paths as production.

// Socketpair-based test fixture.
//
// We use socketpair(2) rather than pipe(2) because the wire-level helpers
// call send(2)/recv(2), which return ENOTSOCK on macOS for pipe fds.
class ProtocolSocketFixture : public ::testing::Test {
protected:
    void SetUp() override {
        int fds[2];
#if defined(__unix__) || defined(__APPLE__)
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
#else
        ASSERT_EQ(::pipe(fds), 0);
#endif
        sender_fd_ = fds[1];
        receiver_fd_ = fds[0];
    }

    void TearDown() override {
        if (sender_fd_ >= 0) ::close(sender_fd_);
        if (receiver_fd_ >= 0) ::close(receiver_fd_);
    }

    net::Socket Sender() { return net::Socket(sender_fd_); }
    net::Socket Receiver() { return net::Socket(receiver_fd_); }

private:
    int sender_fd_ = -1;
    int receiver_fd_ = -1;
};

TEST_F(ProtocolSocketFixture, SessionHeaderRoundTrip) {
    SessionHeader out;
    out.version = 1;
    out.flags = 0x1;
    out.total_files = 42;
    out.total_bytes = 1024ULL * 1024 * 256;

    auto sender = Sender();
    std::error_code ec;
    ASSERT_TRUE(wire::WriteSessionHeader(sender, out, ec));

    auto receiver = Receiver();
    SessionHeader in;
    ASSERT_TRUE(wire::ReadSessionHeader(receiver, in, ec));

    EXPECT_EQ(in.version, out.version);
    EXPECT_EQ(in.flags, out.flags);
    EXPECT_EQ(in.total_files, out.total_files);
    EXPECT_EQ(in.total_bytes, out.total_bytes);
}

TEST_F(ProtocolSocketFixture, SessionAckRoundTrip) {
    const std::uint32_t flags_out = 0xDEADBEEF;

    auto sender = Sender();
    std::error_code ec;
    ASSERT_TRUE(wire::WriteAck(sender, flags_out, ec));

    auto receiver = Receiver();
    std::uint32_t flags_in = 0;
    ASSERT_TRUE(wire::ReadAck(receiver, flags_in, ec));
    EXPECT_EQ(flags_in, flags_out);
}

TEST_F(ProtocolSocketFixture, FileEntryRoundTrip) {
    FileEntry out;
    out.rel_path = "subdir/some-file.bin";
    out.size = 4096ULL * 1024 * 1024;
    out.crc32 = 0xCAFEBABE;

    auto sender = Sender();
    std::error_code ec;
    ASSERT_TRUE(wire::WriteFileHeader(sender, out, ec));

    auto receiver = Receiver();
    FileEntry in;
    ASSERT_TRUE(wire::ReadFileHeader(receiver, in, ec));

    EXPECT_EQ(in.rel_path, out.rel_path);
    EXPECT_EQ(in.size, out.size);
    EXPECT_EQ(in.crc32, out.crc32);
}

TEST_F(ProtocolSocketFixture, EmptyFileEntryIsOk) {
    FileEntry out;
    out.rel_path = "";
    out.size = 0;
    out.crc32 = 0;

    auto sender = Sender();
    std::error_code ec;
    ASSERT_TRUE(wire::WriteFileHeader(sender, out, ec));

    auto receiver = Receiver();
    FileEntry in;
    ASSERT_TRUE(wire::ReadFileHeader(receiver, in, ec));
    EXPECT_EQ(in.rel_path, "");
    EXPECT_EQ(in.size, 0u);
    EXPECT_EQ(in.crc32, 0u);
}

TEST_F(ProtocolSocketFixture, DoneMarkerRoundTrip) {
    auto sender = Sender();
    std::error_code ec;
    ASSERT_TRUE(wire::WriteDone(sender, ec));

    auto receiver = Receiver();
    ASSERT_TRUE(wire::ReadDone(receiver, ec));
}

}  // namespace
}  // namespace transfer
}  // namespace xfer
