// ============================================================================
//  Socket：POSIX/WinSock 兼容的 TCP RAII 封装
//  ---------------------------------------------------------------------------
//  设计要点：
//   - 只暴露同步 API（Connect/Listen/Accept/Send/Recv），初学者容易理解；
//   - 使用 `sendfile(2)`（Linux）/`sendfile(2)`（macOS）做内核态零拷贝
//     发送长文件；没有 sendfile 时回退到「用户态 pread + writev」。
//   - 每个对象持有一个 socket fd；非可拷贝但可移动（move-only）。
// ============================================================================
#ifndef XFER_NET_SOCKET_H
#define XFER_NET_SOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

namespace xfer {
namespace net {

// 使用 int 作为 fd；在 Windows 上我们把 SOCKET 强制转换为 int 以保持
// 头文件不依赖 <winsock2.h>（具体转换在 .cpp 里通过 reinterpret_cast）。
using socket_t = int;

class Socket {
 public:
  Socket() noexcept;
  explicit Socket(socket_t fd) noexcept;
  ~Socket();

  // 不可拷贝，避免双重 close。
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  // 可移动。移动语义是「夺取对方 fd 的所有权」。
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  // --- 状态 ---
  bool Valid() const noexcept { return fd_ >= 0; }
  socket_t Fd() const noexcept { return fd_; }
  void Close() noexcept;

  // --- 连接 / 监听 / 接受 ---
  // 主机名可以是 IPv4、IPv6 或域名；端口是整数。
  bool Connect(const std::string& host, int port, std::error_code& ec);
  // 监听 `port`，采用 IPv6 dual-stack 模式（在支持的平台上同时接受
  // v4 与 v6 连接）。
  bool Bind(int port, std::error_code& ec);
  bool Listen(int backlog, std::error_code& ec);
  Socket Accept(std::error_code& ec);

  // 打开 TCP_NODELAY；对延迟敏感的小消息流有用（xfer 的数据路径通常
  // 是大块，所以默认情况下不需要开启，这里只是给高级使用提供 API）。
  bool SetNoDelay(bool on, std::error_code& ec);

  // --- 字节 IO ---
  // 返回：成功写入/读取的字节数；-1 表示错误。
  int Send(const void* data, std::size_t len, std::error_code& ec);
  int Recv(void* data, std::size_t len, std::error_code& ec);

  // 循环调用 send/recv 直到 `len` 字节全部处理完毕或出错。
  bool SendAll(const void* data, std::size_t len, std::error_code& ec);
  bool RecvAll(void* data, std::size_t len, std::error_code& ec);

  // --- 零拷贝发送（Linux/macOS） ---
  // 从已打开的 `source_fd` 读取 `length` 字节（从当前文件偏移开始），
  // 写入到本 socket。`offset_in_out` 用于在失败时告知调用者实际
  // 传输了多少字节。
  bool SendFromFd(int source_fd, std::uint64_t& offset_in_out,
                  std::uint64_t length, std::error_code& ec);

 private:
  socket_t fd_;
};

}  // namespace net
}  // namespace xfer

#endif  // XFER_NET_SOCKET_H
