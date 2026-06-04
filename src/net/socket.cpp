// ============================================================================
//  Socket 实现
// ============================================================================
#include "net/socket.h"

#include <cerrno>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <netdb.h>
#endif

#if defined(__linux__)
#  include <sys/sendfile.h>
#endif

namespace xfer {
namespace net {

namespace {

#if defined(_WIN32)
// WinSock 的一次性初始化；放在带静态存储期的对象里，
// 进程退出时由析构函数自动 WSACleanup。
struct WSAInit {
  WSAInit() {
    WSADATA wsa{};
    (void)WSAStartup(MAKEWORD(2, 2), &wsa);
  }
  ~WSAInit() { WSACleanup(); }
};
static WSAInit g_wsa_init;
#endif

// 把 errno / WSAGetLastError 包装成 std::error_code 的小工具。
std::error_code LastError() {
#if defined(_WIN32)
  return {WSAGetLastError(), std::system_category()};
#else
  return {errno, std::generic_category()};
#endif
}

}  // namespace

// ============================================================================
//  生命周期
// ============================================================================
Socket::Socket() noexcept : fd_(-1) {}
Socket::Socket(socket_t fd) noexcept : fd_(fd) {}

Socket::~Socket() { Close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;  // 夺取所有权后把源对象置为无效状态
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    Close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

void Socket::Close() noexcept {
  if (fd_ >= 0) {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(fd_));
#else
    ::close(fd_);
#endif
    fd_ = -1;
  }
}

// ============================================================================
//  Connect：先调用 getaddrinfo 做 DNS/AI 解析，再遍历所有结果
//  尝试连接。第一个成功的结果即被采用。
// ============================================================================
bool Socket::Connect(const std::string& host, int port, std::error_code& ec) {
  Close();

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;      // 同时接受 IPv4 和 IPv6
  hints.ai_socktype = SOCK_STREAM;   // TCP

  struct addrinfo* info = nullptr;
  int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                         &info);
  if (rc != 0 || !info) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  // 遍历所有地址；一旦成功立即跳出。
  for (auto p = info; p != nullptr; p = p->ai_next) {
    fd_ = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd_ < 0) continue;
    if (::connect(fd_, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0) {
      break;
    }
    // 当前地址失败，尝试下一个。
    Close();
  }
  freeaddrinfo(info);

  if (!Valid()) {
    ec = LastError();
    return false;
  }
  return true;
}

// ============================================================================
//  Bind / Listen / Accept
// ============================================================================
bool Socket::Bind(int port, std::error_code& ec) {
  Close();

  struct sockaddr_in6 addr {};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(static_cast<std::uint16_t>(port));
  addr.sin6_addr = in6addr_any;  // 监听所有接口

  fd_ = ::socket(AF_INET6, SOCK_STREAM, 0);
  if (fd_ < 0) {
    ec = LastError();
    return false;
  }

  // 允许在 TIME_WAIT 状态下立刻重启监听；便于调试。
  int on = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&on), sizeof(on));

  // 关闭 IPv6-only 监听，使同一 socket 接受 v4 映射地址；
  // 不是所有平台都支持，但失败了也没关系（只是无法接受 v4 连接）。
#if defined(IPV6_V6ONLY)
  int v6only = 0;
  ::setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY,
               reinterpret_cast<const char*>(&v6only), sizeof(v6only));
#endif

  if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ec = LastError();
    Close();
    return false;
  }
  return true;
}

bool Socket::Listen(int backlog, std::error_code& ec) {
  if (!Valid()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return false;
  }
  if (::listen(fd_, backlog) < 0) {
    ec = LastError();
    return false;
  }
  return true;
}

Socket Socket::Accept(std::error_code& ec) {
  struct sockaddr_storage addr {};
  socklen_t len = sizeof(addr);
  socket_t peer = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
  if (peer < 0) {
    ec = LastError();
    return Socket();
  }
  return Socket(peer);
}

bool Socket::SetNoDelay(bool on, std::error_code& ec) {
  int flag = on ? 1 : 0;
  if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag)) < 0) {
    ec = LastError();
    return false;
  }
  return true;
}

// ============================================================================
//  Send / Recv：单次调用。
// ============================================================================
int Socket::Send(const void* data, std::size_t len, std::error_code& ec) {
  auto n = ::send(fd_, static_cast<const char*>(data), len, 0);
  if (n < 0) {
    ec = LastError();
    return -1;
  }
  return static_cast<int>(n);
}

int Socket::Recv(void* data, std::size_t len, std::error_code& ec) {
  auto n = ::recv(fd_, static_cast<char*>(data), len, 0);
  if (n < 0) {
    ec = LastError();
    return -1;
  }
  return static_cast<int>(n);
}

bool Socket::SendAll(const void* data, std::size_t len, std::error_code& ec) {
  auto p = static_cast<const char*>(data);
  while (len > 0) {
    int n = Send(p, len, ec);
    if (n <= 0) return false;
    p += n;
    len -= static_cast<std::size_t>(n);
  }
  return true;
}

bool Socket::RecvAll(void* data, std::size_t len, std::error_code& ec) {
  auto p = static_cast<char*>(data);
  while (len > 0) {
    int n = Recv(p, len, ec);
    // n == 0 代表对端关闭，意味着我们要的字节不能被完整读到。
    if (n <= 0) {
      if (n == 0) ec = std::make_error_code(std::errc::connection_reset);
      return false;
    }
    p += n;
    len -= static_cast<std::size_t>(n);
  }
  return true;
}

// ============================================================================
//  SendFromFd：优先使用操作系统提供的零拷贝机制；不可用时回退到
//  4 MiB 循环的 pread + send 路径。
// ============================================================================
bool Socket::SendFromFd(int source_fd, std::uint64_t& offset_in_out,
                        std::uint64_t length, std::error_code& ec) {
  if (!Valid()) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    return false;
  }
  if (length == 0) return true;

  std::uint64_t remaining = length;

#if defined(__linux__)
  // Linux sendfile(2)：in_fd 必须是支持 mmap 的文件（普通文件 OK），
  // out_fd 必须是一个 socket。`offset` 是 in/out 参数，会被内核更新。
  off64_t off = static_cast<off64_t>(offset_in_out);
  while (remaining > 0) {
    std::size_t want = remaining > static_cast<std::uint64_t>(1ULL << 30)
                           ? static_cast<std::size_t>(1ULL << 30)
                           : static_cast<std::size_t>(remaining);
    ssize_t n = ::sendfile64(fd_, source_fd, &off, want);
    if (n < 0) {
      if (errno == EINTR) continue;
      ec = LastError();
      offset_in_out = static_cast<std::uint64_t>(off);
      return false;
    }
    if (n == 0) {
      ec = std::make_error_code(std::errc::io_error);
      offset_in_out = static_cast<std::uint64_t>(off);
      return false;
    }
    remaining -= static_cast<std::uint64_t>(n);
  }
  offset_in_out = static_cast<std::uint64_t>(off);
  return true;

#elif defined(__APPLE__)
  // macOS sendfile(2)：int sendfile(int fd, int s, off_t offset, off_t *len, ...)
  // 注意参数顺序与 Linux 不同 —— 文件描述符在前，socket 在后。
  off_t off = static_cast<off_t>(offset_in_out);
  while (remaining > 0) {
    off_t len = remaining > static_cast<std::uint64_t>(1LL << 30)
                    ? static_cast<off_t>(1LL << 30)
                    : static_cast<off_t>(remaining);
    int rc = ::sendfile(source_fd, fd_, off, &len, nullptr, 0);
    if (rc < 0) {
      if (errno == EINTR) {
        off += len;
        remaining -= static_cast<std::uint64_t>(len);
        continue;
      }
      ec = LastError();
      offset_in_out = static_cast<std::uint64_t>(off);
      return false;
    }
    off += len;
    remaining -= static_cast<std::uint64_t>(len);
  }
  offset_in_out = static_cast<std::uint64_t>(off);
  return true;

#else
  // 通用回退：循环地从 source_fd pread 到用户缓冲区，再 send 出去。
  static constexpr std::size_t kBufSize = 4 * 1024 * 1024;
  thread_local std::vector<char> g_buffer(kBufSize);
  while (remaining > 0) {
    std::size_t want = remaining < kBufSize ? static_cast<std::size_t>(remaining)
                                            : kBufSize;
    ssize_t got = ::pread(source_fd, g_buffer.data(), want,
                          static_cast<off_t>(offset_in_out));
    if (got < 0) {
      if (errno == EINTR) continue;
      ec = LastError();
      return false;
    }
    if (got == 0) {
      ec = std::make_error_code(std::errc::io_error);
      return false;
    }
    if (!SendAll(g_buffer.data(), static_cast<std::size_t>(got), ec)) return false;
    offset_in_out += static_cast<std::uint64_t>(got);
    remaining -= static_cast<std::uint64_t>(got);
  }
  return true;
#endif
}

}  // namespace net
}  // namespace xfer
