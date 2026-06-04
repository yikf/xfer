// xfer - CLI 实现 (cli.cpp)
//
// 职责：
//   * Parse：将 argc/argv 解析为 Options；
//   * PrintHelp：打印帮助文本到 stdout；
//   * RunSend / RunReceive：绑定 Socket 并调用 transfer 模块执行实际传输。
//
// 参数解析风格：手工编写而非使用 getopt，便于跨平台（Windows 无 POSIX getopt_long）。
// 常见模式：
//   * 长选项：--host --output --port --recursive --follow-symlinks --no-zero-copy ...
//   * 短选项：-h -V -v -r -L -p -o
//   * 赋值式：--color=always

#include "cli.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "common/format.h"
#include "common/logging.h"
#include "net/socket.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"
#include "xfer/version.h"

namespace xfer {
namespace cli {

namespace {

// 小工具：检查 a 是否以 prefix 开头，若是则将剩余部分写入 rest。
// 用于解析 "--host foo" 风格的选项。
bool ArgStarts(const char* a, const char* prefix, const char*& rest) {
    std::size_t n = std::strlen(prefix);
    if (std::strncmp(a, prefix, n) != 0) return false;
    rest = a + n;
    return true;
}

// 将 C 字符串解析为 int，写入 out。空串或非数字字符返回 false。
bool ParseInt(const char* s, int& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    // strtol 允许前导空白，但必须是完整的数字串。
    if (end == s || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

}  // namespace

// 帮助文本：使用 C++ 原始字符串 R"EOF(...)" 避免转义，方便维护。
void PrintHelp() {
    std::cout << "xfer " XFER_VERSION_STR R"EOF( - fast intranet file transfer

USAGE:
    xfer send  [OPTIONS] <file or directory>...
    xfer recv  [OPTIONS]

MODES:
    send    Push files / directories to a remote receiver, OR wait for the
            receiver to connect (see --listen).
    recv    Listen for a sender and write files to --output.

GLOBAL OPTIONS:
    -h, --help            Show this help message and exit.
    -V, --version         Show the version and exit.
    -v, --verbose         Enable debug logging (repeatable).
    --no-progress         Disable the inline progress bar.
    --color=auto|always|never
                          Force color output (default: auto = stderr TTY).

SEND OPTIONS:
    --host <HOST>         Receiver host name or IPv4/IPv6 address.  If
                          omitted, the sender listens and the receiver must
                          initiate the connection.
    -p, --port <PORT>     TCP port (default: 9876).
    -r, --recursive       Recurse into directories.
    -L, --follow-symlinks Follow symlinks when walking directories.
    --no-zero-copy        Force the userspace read/send path (useful for
                          profiling and for platforms without sendfile()).

RECEIVE OPTIONS:
    -p, --port <PORT>     TCP port to listen on (default: 9876).
    -o, --output <DIR>    Output directory (default: current working
                          directory).
    --no-verify           Skip per-file CRC-32 verification.

EXAMPLES:
    # Host B (receiver, 10.0.0.2) waits for files:
    xfer recv -p 9876 -o ./downloads

    # Host A pushes files to B:
    xfer send --host 10.0.0.2 -p 9876 ./big-file ./logs/

    # Or have the sender listen (useful for "pull"):
    xfer send --listen -p 9876 ./src
    # and on the receiver:
    xfer recv --host sender-host -p 9876 -o ./src-copy
)EOF";
}

// 主解析函数：一次遍历 argv[1..argc-1]。
// 设计：第一个不以 '-' 开头的 token 视为子命令 (send|recv|help|version)。
bool Parse(int argc, char** argv, Options& opts) {
    if (argc < 2) {
        PrintHelp();
        return false;
    }

    // 复制到 vector<string>，便于处理。
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    // 第一个非选项 token 是子命令。
    // First non-flag token is the subcommand.
    std::size_t pos = 0;
    if (args[0][0] != '-') {
        const std::string& cmd = args[0];
        if (cmd == "send")       opts.mode = Mode::Send;
        else if (cmd == "recv" || cmd == "receive") opts.mode = Mode::Receive;
        else if (cmd == "help" || cmd == "-h" || cmd == "--help") opts.mode = Mode::Help;
        else if (cmd == "version" || cmd == "-V" || cmd == "--version") opts.mode = Mode::Version;
        else {
            std::fprintf(stderr, "xfer: unknown subcommand '%s'\n", cmd.c_str());
            return false;
        }
        pos = 1;
    }

    // 逐 token 解析。
    for (; pos < args.size(); ++pos) {
        const std::string& a = args[pos];
        const char* rest = nullptr;

        // 开关类选项（无参数）。
        if (a == "-h" || a == "--help") { opts.mode = Mode::Help; continue; }
        if (a == "-V" || a == "--version") { opts.mode = Mode::Version; continue; }
        if (a == "-v" || a == "--verbose") { opts.verbose = true; continue; }
        if (a == "--no-progress") { opts.no_progress = true; continue; }
        if (a == "-r" || a == "--recursive") { opts.recursive = true; continue; }
        if (a == "-L" || a == "--follow-symlinks") { opts.follow_symlinks = true; continue; }
        if (a == "--no-zero-copy") { opts.no_zero_copy = true; continue; }
        if (a == "--no-verify") { opts.no_verify = true; continue; }
        if (a == "--listen") { opts.listen = true; continue; }

        // --color=always|never|auto：赋值式选项。
        if (ArgStarts(a.c_str(), "--color=", rest)) {
            std::string v(rest);
            if (v == "always") opts.force_color = true;
            else if (v == "never") opts.force_color = false;
            else if (v == "auto") { /* default */ }
            else {
                std::fprintf(stderr, "xfer: --color= must be auto|always|never\n");
                return false;
            }
            continue;
        }

        // 需要下一个参数作为值的选项（消耗 ++pos）。
        // Lambda: 检查是否还有下一个 token；若有则写入 out。
        auto need_val = [&](const std::string& flag, std::string& out) -> bool {
            if (pos + 1 >= args.size()) {
                std::fprintf(stderr, "xfer: %s requires a value\n", flag.c_str());
                return false;
            }
            out = args[++pos];
            return true;
        };
        // 同上，但解析为整数。
        auto need_int = [&](const std::string& flag, int& out) -> bool {
            if (pos + 1 >= args.size()) {
                std::fprintf(stderr, "xfer: %s requires a value\n", flag.c_str());
                return false;
            }
            int v = 0;
            if (!ParseInt(args[++pos].c_str(), v)) {
                std::fprintf(stderr, "xfer: invalid integer for %s\n", flag.c_str());
                return false;
            }
            out = v;
            return true;
        };

        if (a == "--host") { if (!need_val("--host", opts.host)) return false; continue; }
        if (a == "-o" || a == "--output") { if (!need_val("--output", opts.output_dir)) return false; continue; }
        if (a == "-p" || a == "--port") { if (!need_int("--port", opts.port)) return false; continue; }

        // 位置参数（仅限 send 模式）。
        if (a[0] != '-' && opts.mode == Mode::Send) {
            opts.paths.push_back(a);
            continue;
        }

        // 未知选项。
        std::fprintf(stderr, "xfer: unknown option '%s'\n", a.c_str());
        return false;
    }

    // 发送模式至少需要一个文件/目录。
    if (opts.mode == Mode::Send && opts.paths.empty()) {
        std::fprintf(stderr, "xfer: 'send' requires at least one file or directory.\n");
        return false;
    }
    // 端口合法性。
    if (opts.port <= 0 || opts.port > 65535) {
        std::fprintf(stderr, "xfer: --port must be in 1..65535\n");
        return false;
    }
    return true;
}

// ---- runner helpers ----

namespace {

// Bind + Accept 封装：便于 RunSend/RunReceive 复用。
bool BindAndAccept(int port, net::Socket& out, std::error_code& ec) {
    net::Socket listener;
    if (!listener.Bind(port, ec)) return false;
    if (!listener.Listen(16, ec)) return false;
    out = listener.Accept(ec);
    return out.Valid();
}

// Dial 封装：host:port → Socket。
bool DialOut(const std::string& host, int port, net::Socket& out, std::error_code& ec) {
    net::Socket s;
    if (!s.Connect(host, port, ec)) return false;
    out = std::move(s);  // Socket 支持移动语义，移交所有权。
    return true;
}

}  // namespace

// 发送端：解析参数 → 建立 Socket → 调用 transfer::SendPaths。
int RunSend(const Options& opts) {
    log::SetLevel(opts.verbose ? log::Level::DEBUG : log::Level::INFO);
    log::SetColor(true);

    std::error_code ec;
    net::Socket sock;
    if (opts.listen || opts.host.empty()) {
        // 推送模式：sender 监听，等待 receiver 主动连接。
        XFER_ILOG << "listening on 0.0.0.0:" << opts.port << " (connect from receiver)";
        if (!BindAndAccept(opts.port, sock, ec)) {
            XFER_ELOG << "accept failed: " << ec.message();
            return 2;
        }
    } else {
        // 推送模式：sender 主动连接到远端 host:port。
        XFER_ILOG << "connecting to " << opts.host << ":" << opts.port;
        if (!DialOut(opts.host, opts.port, sock, ec)) {
            XFER_ELOG << "connect failed: " << ec.message();
            return 2;
        }
    }

    transfer::SenderOptions snd;
    snd.recursive = opts.recursive;
    snd.follow_symlinks = opts.follow_symlinks;
    snd.zero_copy = !opts.no_zero_copy;
    snd.no_progress = opts.no_progress;
    if (!transfer::SendPaths(sock, opts.paths, snd, ec)) {
        XFER_ELOG << "transfer failed: " << ec.message();
        return 3;
    }
    return 0;
}

// 接收端主入口：Bind + Accept，读取并校验 session。
int RunReceive(const Options& opts) {
    log::SetLevel(opts.verbose ? log::Level::DEBUG : log::Level::INFO);
    log::SetColor(true);

    std::error_code ec;
    net::Socket sock;

    if (opts.host.empty()) {
        // 默认：监听等待 sender 连接。
        XFER_ILOG << "listening on 0.0.0.0:" << opts.port;
        if (!BindAndAccept(opts.port, sock, ec)) {
            XFER_ELOG << "accept failed: " << ec.message();
            return 2;
        }
    } else {
        // "pull" 模式：主动连接到 sender（sender 端使用 --listen）。
        XFER_ILOG << "connecting to sender " << opts.host << ":" << opts.port;
        if (!DialOut(opts.host, opts.port, sock, ec)) {
            XFER_ELOG << "connect failed: " << ec.message();
            return 2;
        }
    }

    transfer::ReceiverOptions rcv;
    rcv.output_dir = opts.output_dir;
    rcv.create_dir = true;
    rcv.no_progress = opts.no_progress;
    rcv.verify_crc = !opts.no_verify;

    transfer::ReceiverSummary summary;
    if (!transfer::ReceiveStream(sock, rcv, summary, ec)) {
        XFER_ELOG << "transfer failed: " << ec.message();
        return 3;
    }
    XFER_ILOG << "wrote " << summary.total_files << " file(s), "
              << common::FormatBytes(summary.total_bytes);
    return 0;
}

}  // namespace cli
}  // namespace xfer
