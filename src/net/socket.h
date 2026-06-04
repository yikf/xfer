// xfer - net/socket: RAII TCP socket wrapper with zero-copy send support.
//
// Provides a thin RAII wrapper around a native socket descriptor.
// The interface exposes synchronous connect/listen/accept and byte-oriented
// send/recv helpers that loop until the requested length has been fully
// transferred.
//
// Key features:
//   * Move-only ownership semantics.
//   * Platform abstraction for POSIX and WinSock (cast to int).
//   * `SendFromFd` on Linux/macOS uses sendfile(2) for kernel-level zero-copy;
//     other platforms fall back to a pread + send loop.
//   * All errors are reported via `std::error_code`.

#ifndef XFER_NET_SOCKET_H
#define XFER_NET_SOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

namespace xfer {
namespace net {

// We store socket descriptors as `int` for API simplicity.
// On Windows we reinterpret_cast<SOCKET> when calling WinSock functions.
using socket_t = int;

class Socket {
 public:
  Socket() noexcept;
  explicit Socket(socket_t fd) noexcept;
  ~Socket();

  // Non-copyable: a Socket uniquely owns its descriptor.
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  // Movable: transfer ownership of the underlying descriptor.
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  // State
  bool Valid() const noexcept { return fd_ >= 0; }
  socket_t Fd() const noexcept { return fd_; }
  void Close() noexcept;

  // Connection management
  bool Connect(const std::string& host, int port, std::error_code& ec);
  bool Bind(int port, std::error_code& ec);
  bool Listen(int backlog, std::error_code& ec);
  Socket Accept(std::error_code& ec);

  // TCP_NODELAY control (useful for latency-sensitive short-message workloads).
  bool SetNoDelay(bool on, std::error_code& ec);

  // Raw byte I/O. Returns the number of bytes sent/received, or -1 on error.
  int Send(const void* data, std::size_t len, std::error_code& ec);
  int Recv(void* data, std::size_t len, std::error_code& ec);

  // Loop helpers — block until `len` bytes have been fully transferred.
  bool SendAll(const void* data, std::size_t len, std::error_code& ec);
  bool RecvAll(void* data, std::size_t len, std::error_code& ec);

  // Zero-copy send path on Linux/macOS: push `length` bytes from the current
  // position of `source_fd` to this socket. `offset_in_out` is updated to
  // reflect the cumulative amount transferred. On platforms without a
  // zero-copy primitive we fall back to a userspace pread + send loop.
  bool SendFromFd(int source_fd, std::uint64_t& offset_in_out,
                  std::uint64_t length, std::error_code& ec);

 private:
  socket_t fd_;
};

}  // namespace net
}  // namespace xfer

#endif  // XFER_NET_SOCKET_H
