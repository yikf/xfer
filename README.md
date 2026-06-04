# xfer

> 一个比 `scp` 更快、更方便的内网文件传输工具。
>
> A faster, easier alternative to `scp` for intranet file transfers.

`xfer` 通过自定义 TCP 协议 + `sendfile(2)` 零拷贝 + 大分块流式 I/O，在内网
（1 Gbps / 10 Gbps / 25 Gbps）环境下快速分发大文件。当前版本 **0.1.0**。

`xfer` ships large files inside a trusted LAN efficiently: small custom TCP
protocol, `sendfile(2)` zero-copy on Linux/macOS, userspace 1 MiB fallback
on Windows. Current release: **0.1.0**.

已实现的功能 / Features implemented：

- 跨平台（Linux / macOS / Windows）的 RAII Socket 封装
  — Cross-platform RAII socket wrapper
- 小端 wire protocol：`[magic | flags | size | name | payload]`
  — Little-endian wire protocol
- 发送端：`sendfile(2)` 零拷贝路径 + 1 MiB userspace 回退
  — `sendfile(2)` zero-copy sender + 1 MiB fallback
- 接收端：目录穿越防御（`..`、绝对路径、`\` 被拒绝）
  — Receiver: path-traversal protection
- 默认每文件 CRC-32 校验（可 `--no-verify` 关闭）
  — CRC-32 per file (on by default; `--no-verify` to disable)
- TTY 下带颜色日志与单行进度条（ETA / 速度 / 字节数）
  — Colored logger + single-line progress bar on TTYs
- CMake（C++17 / ASan / 测试开关）+ clang-format
  — CMake (C++17 / ASan / tests) + clang-format style
- `ctest` 回环端到端测试（1 KiB + 4 MiB 文件，含 CRC 验证）
  — `ctest` round-trip tests on `localhost` (1 KiB + 4 MiB, CRC verified)

---

## 目录结构 / Project layout

```
xfer/
├── CMakeLists.txt              # 顶层构建 / top-level build
├── src/
│   ├── CMakeLists.txt          # xfer_core 静态库 + xfer 可执行程序
│   ├── main.cpp                # dispatch send/recv/help/version
│   ├── cli.h / cli.cpp         # 参数解析与子命令 runner / arg parser & runner
│   ├── common/
│   │   ├── logging.h/.cpp      # 彩色分级日志 / colored leveled logger
│   │   ├── format.h/.cpp       # 字节/时长人类可读格式化 / byte & duration formatters
│   │   ├── stopwatch.h/.cpp    # steady-clock 计时器
│   │   ├── crc32.h/.cpp        # CRC-32C (Castagnoli)
│   │   └── progress.h/.cpp     # 单行进度条 / single-line progress bar
│   ├── net/
│   │   └── socket.h/.cpp       # RAII Socket + sendfile 零拷贝
│   └── transfer/
│       ├── protocol.h/.cpp     # session 握手 + 文件记录编解码
│       ├── sender.h/.cpp       # 目录扫描 / CRC / 流式发送
│       └── receiver.h/.cpp     # 记录解析 / CRC 校验 / 防穿越写入
├── include/xfer/
│   ├── version.h               # XFER_VERSION_{MAJOR,MINOR,PATCH,STR}
│   └── protocol.h              # magic/flag 常量（对外可见）/ public constants
├── tests/
│   ├── CMakeLists.txt          # 独立测试可执行程序 / individual test executables
│   ├── test_basic.cpp          # smoke test（版本号校验）/ smoke test (version)
│   └── test_roundtrip.cpp      # localhost 回环测试（含 CRC）/ round-trip test
├── .gitignore
├── .clang-format               # Google 风格，4 空格，列宽 120
└── build/                      # 构建产物 / build output (bin/xfer, lib/libxfer_core.a)
```

---

## 编译 / Building

### 依赖 / Prerequisites

- CMake ≥ 3.16
- 支持 C++17 的编译器 — C++17 toolchain (GCC 8+ / Clang 7+ / AppleClang 12+ / MSVC 2019+)

### 构建步骤 / Build steps

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build -j                            # compile
ctest --test-dir build --output-on-failure        # run tests
```

### 常用 CMake 选项 / Useful CMake options

| Option | Default | 说明 / Description |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` |
| `XFER_BUILD_TESTS` | `ON` | 是否构建 `tests/` / Build the tests |
| `XFER_ENABLE_ASAN` | `ON` | Debug 下开启 ASan + UBSan / Enable ASan + UBSan in Debug |
| `XFER_WARNINGS_AS_ERRORS` | `ON` | 视警告为错误 / Treat warnings as errors |

示例 / Example — Debug + sanitizers：

```bash
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DXFER_ENABLE_ASAN=ON
cmake --build build-debug -j
```

---

## 使用 / Usage

```text
$ ./build/bin/xfer --help
xfer 0.1.0 - fast intranet file transfer

USAGE:
    xfer send  [OPTIONS] <file or directory>...
    xfer recv  [OPTIONS]

GLOBAL OPTIONS:
    -h, --help            Show this help message and exit.
    -V, --version         Show the version and exit.
    -v, --verbose         Enable debug logging.
    --no-progress         Disable the inline progress bar.
    --color=auto|always|never
                          Force color output (default: auto).

SEND OPTIONS:
    --host <HOST>         Receiver host or IPv4/IPv6 address.  If omitted,
                          the sender listens on the given port and waits for
                          the receiver to connect.
    -p, --port <PORT>     TCP port (default: 9876).
    -r, --recursive       Recurse into directories.
    -L, --follow-symlinks Follow symlinks while walking directories.
    --no-zero-copy        Force the userspace read/send path (useful for
                          profiling / platforms without sendfile()).

RECEIVE OPTIONS:
    -p, --port <PORT>     TCP port to listen on (default: 9876).
    -o, --output <DIR>    Output directory (default: current working dir).
    --no-verify           Skip per-file CRC-32 verification.
```

### 典型用法 / Typical workflow

```bash
# 1. 接收端（10.0.0.2）监听，将文件写入 ./dst
#    Receiver waits for files and writes them to ./dst
xfer recv -o ./dst -p 9876

# 2. 发送端把目录递归推送到接收端
#    Sender pushes a directory recursively
xfer send --host 10.0.0.2 -p 9876 -r ./data

# 反过来：发送端监听，接收端主动拉取
# Reverse-role mode — sender listens, receiver pulls:
xfer send --listen -p 9876 ./data     # on sender host
xfer recv  --host 10.0.0.1 -p 9876 -o ./dst   # on receiver host
```

### 作为库使用 / As a library

`xfer_core` 是一个静态库。公共头在 `include/xfer/`，内部头通过 `-Isrc` 可达。

`xfer_core` is a static library. Public headers live under `include/xfer/`;
internal headers are reachable via `-Isrc`.

```cpp
#include <system_error>
#include <string>
#include <vector>

#include "net/socket.h"
#include "transfer/sender.h"
#include "transfer/receiver.h"

// sender / 发送端
xfer::net::Socket sock;
std::error_code ec;
if (sock.Connect("10.0.0.2", 9876, ec)) {
    xfer::transfer::SenderOptions opts;
    opts.recursive = true;
    xfer::transfer::SendPaths(sock, {"./data"}, opts, ec);
}

// receiver / 接收端
xfer::net::Socket listener;
listener.Bind(9876, ec);
listener.Listen(16, ec);
auto client = listener.Accept(ec);

xfer::transfer::ReceiverOptions ropts;
ropts.output_dir = "./dst";
xfer::transfer::ReceiverSummary summary;
xfer::transfer::ReceiveStream(client, ropts, summary, ec);
```

### Wire Protocol

```
Session Header (32 bytes, sent once at handshake):
  [ 4] magic              = 0x58464552 ("XFER", little-endian)
  [ 2] version            = 0x0001
  [ 2] reserved           = 0
  [ 8] total_files        (little-endian)
  [ 8] total_bytes        (little-endian)
  [ 4] flags              (bit 0 = CRC enabled)
  [ 4] CRC-32 of the preceding 28 bytes

Receiver Ack (8 bytes):
  [ 4] magic              = 0x4f4b4159 ("OKAY")
  [ 4] receiver_flags     (reserved)

Per-file record (repeated total_files times):
  [ 4] FILE magic         = 0x46494c45 ("FILE")
  [ 2] name_len           (little-endian)
  [ 8] file_size          (little-endian)
  [ 4] sender_flags       (reserved)
  [ 4] CRC-32 of payload  (if sender_flags bit 0 set)
  [name_len] relative path (UTF-8, '/' as separator)
  [file_size] raw payload

Trailer:
  [ 4] DONE magic         = 0x444f4e45 ("DONE")
```

所有整数均为 **小端**，在 x86/ARM 上可直接 `memcpy`。
接收端在检测到 `..`、绝对路径或 `\` 时会关闭连接。

All integers are **little-endian** and can be `memcpy`-d on x86/ARM
without byte-swapping. Path-traversal attempts (`..`, absolute paths, `\`)
are rejected by the receiver and close the connection.

---

## 测试 / Tests

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- `xfer_test_basic`：smoke test（版本号）/ smoke test (version string)
- `xfer_test_roundtrip`：在 `localhost` 上发送 1 KiB + 4 MiB 随机文件，
  接收端通过 CRC-32 验证。

Example output:

```
Test project /path/to/xfer/build
    Start 1: xfer_test_basic
1/2 Test #1: xfer_test_basic .................   Passed    0.28 sec
    Start 2: xfer_test_roundtrip
2/2 Test #2: xfer_test_roundtrip .............   Passed    0.69 sec

100% tests passed, 0 tests failed out of 2
```

---

## 路线图 / Roadmap

- [ ] 发送端 `--listen`（接收端反向拉取）— `--listen` on sender (receiver-pull)
- [ ] 多文件并行 & 断点续传（协议 flag 位已预留）
  — Parallel transfers & resumable sessions (flag bits already reserved)
- [ ] 可选 ChaCha20-Poly1305 / TLS 加密（需引入第三方依赖）
  — Optional ChaCha20-Poly1305 / TLS encryption (needs a 3rd-party dep)
- [ ] 带宽限制 / TCP 窗口调优 — Bandwidth limiting / TCP window tuning
- [ ] Windows `TransmitFile` 零拷贝（当前回退为 userspace I/O）
  — Windows `TransmitFile` zero-copy path
- [ ] 更多单元测试：协议编解码、目录穿越防御、Socket 错误路径
  — More unit tests: protocol codec, path-traversal guard, socket errors

欢迎在 `src/` 下新增模块并通过 PR 提交。
Contributions are welcome — drop new modules under `src/` and send a PR.
