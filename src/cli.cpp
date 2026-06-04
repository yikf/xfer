// xfer - cli: implementation of argument parsing, runner helpers, and main entry points.
//
// The parser is hand-written (rather than using getopt_long) for portability
// across POSIX and Windows. It consumes argv left-to-right: the first token
// not starting with '-' is treated as the subcommand (send|recv|help|version),
// then remaining tokens are parsed as flags or positional arguments.

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

// Return true and advance `rest` when `a` begins with `prefix`.
bool ArgStarts(const char* a, const char* prefix, const char*& rest) {
  std::size_t n = std::strlen(prefix);
  if (std::strncmp(a, prefix, n) != 0) return false;
  rest = a + n;
  return true;
}

// Parse `s` as an int; returns false on empty or non-numeric input.
bool ParseInt(const char* s, int& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0') return false;
  out = static_cast<int>(v);
  return true;
}

}  // namespace

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
    --host <HOST>         Receiver host name or IPv4/IPv6 address. If omitted,
                          the sender listens and the receiver must initiate the
                          connection.
    -p, --port <PORT>     TCP port (default: 9876).
    -r, --recursive       Recurse into directories.
    -L, --follow-symlinks
                          Follow symlinks when walking directories.
    --no-zero-copy        Force userspace read/send path (useful for profiling
                          and on platforms without sendfile()).

RECEIVE OPTIONS:
    -p, --port <PORT>     TCP port to listen on (default: 9876).
    -o, --output <DIR>    Output directory (default: current working directory).
    --no-verify           Skip per-file CRC-32 verification.

EXAMPLES:
    # Host B (receiver) waits for files:
    xfer recv -p 9876 -o ./downloads

    # Host A pushes files to B:
    xfer send --host 10.0.0.2 -p 9876 ./big-file ./logs/

    # Or make the sender listen (useful for "pull" workflows):
    xfer send --listen -p 9876 ./src
    # And on the receiver:
    xfer recv --host sender-host -p 9876 -o ./src-copy
)EOF";
}

// ---- argument parsing ----
// Single-pass left-to-right scanner over argv.
bool Parse(int argc, char** argv, Options& opts) {
  if (argc < 2) {
    PrintHelp();
    return false;
  }

  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

  // Detect subcommand from first non-flag token.
  std::size_t pos = 0;
  if (args[0][0] != '-') {
    const std::string& cmd = args[0];
    if (cmd == "send") opts.mode = Mode::Send;
    else if (cmd == "recv" || cmd == "receive") opts.mode = Mode::Receive;
    else if (cmd == "help" || cmd == "-h" || cmd == "--help") opts.mode = Mode::Help;
    else if (cmd == "version" || cmd == "-V" || cmd == "--version") opts.mode = Mode::Version;
    else {
      std::fprintf(stderr, "xfer: unknown subcommand '%s'\n", cmd.c_str());
      return false;
    }
    pos = 1;
  }

  // Lambdas used for "consume one more token as value".
  auto need_val = [&](const std::string& flag, std::string& out) -> bool {
    if (pos + 1 >= args.size()) {
      std::fprintf(stderr, "xfer: %s requires a value\n", flag.c_str());
      return false;
    }
    out = args[++pos];
    return true;
  };
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

  for (; pos < args.size(); ++pos) {
    const std::string& a = args[pos];
    const char* rest = nullptr;

    // Standalone flags.
    if (a == "-h" || a == "--help") { opts.mode = Mode::Help; continue; }
    if (a == "-V" || a == "--version") { opts.mode = Mode::Version; continue; }
    if (a == "-v" || a == "--verbose") { opts.verbose = true; continue; }
    if (a == "--no-progress") { opts.no_progress = true; continue; }
    if (a == "-r" || a == "--recursive") { opts.recursive = true; continue; }
    if (a == "-L" || a == "--follow-symlinks") { opts.follow_symlinks = true; continue; }
    if (a == "--no-zero-copy") { opts.no_zero_copy = true; continue; }
    if (a == "--no-verify") { opts.no_verify = true; continue; }
    if (a == "--listen") { opts.listen = true; continue; }

    // --color=<mode>
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

    // Value-bearing flags (consume one additional token).
    if (a == "--host") { if (!need_val("--host", opts.host)) return false; continue; }
    if (a == "-o" || a == "--output") { if (!need_val("--output", opts.output_dir)) return false; continue; }
    if (a == "-p" || a == "--port") { if (!need_int("--port", opts.port)) return false; continue; }

    // Positional argument — only meaningful for `send`.
    if (a[0] != '-' && opts.mode == Mode::Send) {
      opts.paths.push_back(a);
      continue;
    }

    std::fprintf(stderr, "xfer: unknown option '%s'\n", a.c_str());
    return false;
  }

  if (opts.mode == Mode::Send && opts.paths.empty()) {
    std::fprintf(stderr, "xfer: 'send' requires at least one file or directory.\n");
    return false;
  }
  if (opts.port <= 0 || opts.port > 65535) {
    std::fprintf(stderr, "xfer: --port must be in 1..65535\n");
    return false;
  }
  return true;
}

// ---- runner helpers ----

namespace {

// Bind to `port`, accept one incoming connection, hand the Socket back via
// `out`. Returns false on bind/listen/accept failure.
bool BindAndAccept(int port, net::Socket& out, std::error_code& ec) {
  net::Socket listener;
  if (!listener.Bind(port, ec)) return false;
  if (!listener.Listen(16, ec)) return false;
  out = listener.Accept(ec);
  return out.Valid();
}

// Dial `host:port` and hand the connected Socket back via `out`.
bool DialOut(const std::string& host, int port, net::Socket& out, std::error_code& ec) {
  net::Socket s;
  if (!s.Connect(host, port, ec)) return false;
  out = std::move(s);
  return true;
}

}  // namespace

// ---- send entry point ----
int RunSend(const Options& opts) {
  log::SetLevel(opts.verbose ? log::Level::DEBUG : log::Level::INFO);
  log::SetColor(true);

  std::error_code ec;
  net::Socket sock;
  if (opts.listen || opts.host.empty()) {
    XFER_ILOG << "listening on 0.0.0.0:" << opts.port << " (connect from receiver)";
    if (!BindAndAccept(opts.port, sock, ec)) {
      XFER_ELOG << "accept failed: " << ec.message();
      return 2;
    }
  } else {
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

// ---- receive entry point ----
int RunReceive(const Options& opts) {
  log::SetLevel(opts.verbose ? log::Level::DEBUG : log::Level::INFO);
  log::SetColor(true);

  std::error_code ec;
  net::Socket sock;

  if (opts.host.empty()) {
    XFER_ILOG << "listening on 0.0.0.0:" << opts.port;
    if (!BindAndAccept(opts.port, sock, ec)) {
      XFER_ELOG << "accept failed: " << ec.message();
      return 2;
    }
  } else {
    // "pull" mode: actively connect to a listening sender.
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
