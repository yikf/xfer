<div align="right">

**简体中文** | [English](README.en.md)

</div>

<div align="center">

<a href="https://github.com/yikf/xfer">
  <h1><code>xfer</code></h1>
</a>

**高性能内网文件传输工具 — 在局域网中提供比 `scp` 更快、更便捷的替代方案。**

<br>

[![GitHub](https://img.shields.io/badge/GitHub-yikf%2Fxfer-181717?logo=github)](https://github.com/yikf/xfer)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C?logo=cmake)](https://cmake.org/)
[![Version](https://img.shields.io/badge/version-0.1.0-green.svg)](include/xfer/version.h)

</div>

---

## 目录

- [项目简介](#项目简介)
- [核心特性](#核心特性)
- [目录结构](#目录结构)
- [快速开始](#快速开始)
  - [前置依赖](#前置依赖)
  - [构建步骤](#构建步骤)
  - [构建选项](#构建选项)
- [使用说明](#使用说明)
  - [命令行参考](#命令行参考)
  - [典型工作流](#典型工作流)
  - [作为库使用](#作为库使用)
- [网络协议](#网络协议)
- [测试](#测试)
- [路线图](#路线图)
- [贡献指南](#贡献指南)
- [许可证](#许可证)

---

## 项目简介

`xfer` 是一款面向内网环境（1 Gbps / 10 Gbps / 25 Gbps）的高性能文件传输工具，目标是以尽可能低的开销在可信局域网中分发大文件。

`scp`/`ssh` 协议栈通用而稳健，非常适合公网环境，但在内网中其加密层与逐包复制的开销会成为瓶颈。`xfer` 对此做出了针对性替换：

- 使用一套**精简的自定义 TCP 协议**，让每个字节都服务于传输；
- 在 Linux / macOS 上采用**`sendfile(2)` 零拷贝**路径，数据不绕回用户态缓冲；
- 在不支持 `sendfile` 的平台上回退到**1 MiB 大分块流式 I/O**。

当前版本：**0.1.0**。

---

## 核心特性

| 特性 | 说明 |
| --- | --- |
| 跨平台 | Linux / macOS / Windows，通过 RAII Socket 封装统一抽象 |
| 零拷贝发送 | Linux/macOS 使用 `sendfile(2)`，Windows 回退到 1 MiB 用户态 I/O |
| 精简协议 | 小端 `[magic \| flags \| size \| name \| payload]` 的会话记录 |
| 完整性校验 | 默认启用每个文件的 CRC-32（Castagnoli）；可通过 `--no-verify` 关闭 |
| 路径穿越防护 | 接收端拒绝 `..`、绝对路径、`\` 等恶意路径，发现即关闭连接 |
| 终端体验 | 彩色分级日志，TTY 下单行进度条显示 ETA / 速度 / 字节数 |
| 工程化 | CMake（C++17）+ AddressSanitizer / UBSan + Google Test |
| 代码风格 | clang-format 统一为 Google 风格，4 空格缩进，120 列换行 |
| 测试 | `ctest` 可在 `localhost` 上做回环测试（1 KiB + 4 MiB 文件，含 CRC 验证） |

---

## 目录结构

```
xfer/
├── CMakeLists.txt              # 顶层构建配置
├── src/
│   ├── CMakeLists.txt          # 构建 xfer_core 静态库 + xfer 可执行文件
│   ├── main.cpp                # 根据子命令派发 send/recv/help/version
│   ├── cli.h / cli.cpp         # 参数解析与子命令 runner
│   ├── common/
│   │   ├── logging.h/.cpp      # 彩色分级日志
│   │   ├── format.h/.cpp       # 字节数 / 时长的人类可读格式化
│   │   ├── stopwatch.h/.cpp    # steady-clock 计时器
│   │   ├── crc32.h/.cpp        # CRC-32C (Castagnoli)
│   │   └── progress.h/.cpp     # 单行进度条（ETA / 速度）
│   ├── net/
│   │   └── socket.h/.cpp       # RAII Socket + sendfile 零拷贝辅助
│   └── transfer/
│       ├── protocol.h/.cpp     # 会话握手 + 文件记录编解码
│       ├── sender.h/.cpp       # 目录扫描、CRC 计算、流式发送
│       └── receiver.h/.cpp     # 记录解析、CRC 校验、安全写入
├── include/xfer/
│   ├── version.h               # XFER_VERSION_{MAJOR,MINOR,PATCH,STR}
│   └── protocol.h              # 对外可见的 magic / flag 常量
├── tests/
│   ├── CMakeLists.txt          # 独立的测试可执行文件
│   ├── test_basic.cpp          # 冒烟测试：版本字符串
│   ├── test_crc32.cpp          # CRC-32 正确性测试
│   ├── test_format.cpp         # 字节 / 时长格式化测试
│   ├── test_protocol.cpp       # 协议编解码测试
│   └── test_roundtrip.cpp      # localhost 回环测试（含 CRC）
├── scripts/
│   ├── build.sh                # 一键 Release 构建
│   ├── clean.sh                # 清理所有构建产物目录
│   ├── fmt.sh                  # 对源码树执行 clang-format
│   └── test.sh                 # 配置并执行 ctest
├── .clang-format               # Google 风格，4 空格缩进，120 列换行
├── .gitignore
└── build/                      # 构建产物 — bin/xfer, lib/libxfer_core.a
```

---

## 快速开始

### 前置依赖

- **CMake ≥ 3.16**
- **C++17 工具链**：GCC 8+、Clang 7+、AppleClang 12+ 或 MSVC 2019+
- （可选）Google Test — 当 `XFER_BUILD_TESTS=ON` 时由 CMake 的 `FetchContent` 自动拉取
- （可选）`clang-format` — 用于格式化源码树

### 构建步骤

三步即可完成：

```bash
# 1. 配置（Release 构建）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 2. 并行编译
cmake --build build -j

# 3. 运行测试套件
ctest --test-dir build --output-on-failure
```

可执行文件位于 `build/bin/xfer`，静态库位于 `build/lib/libxfer_core.a`。

若要开启 sanitizer 的 Debug 构建：

```bash
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DXFER_ENABLE_ASAN=ON
cmake --build build-debug -j
```

### 构建选项

项目暴露了若干 CMake 缓存变量，在配置阶段通过 `-D<option>=<value>` 传入。

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` |
| `XFER_BUILD_TESTS` | `ON` | 是否构建 `tests/` 目录的 Google Test 测试 |
| `XFER_ENABLE_ASAN` | `ON` | 在 `Debug` 构建中启用 AddressSanitizer + UndefinedBehaviorSanitizer |
| `XFER_WARNINGS_AS_ERRORS` | `ON` | 将编译器警告视为错误 |

---

## 使用说明

### 命令行参考

```text
$ ./build/bin/xfer --help
xfer 0.1.0 - fast intranet file transfer

USAGE:
    xfer send  [OPTIONS] <file or directory>...
    xfer recv  [OPTIONS]

GLOBAL OPTIONS:
    -h, --help            显示帮助并退出。
    -V, --version         显示版本号并退出。
    -v, --verbose         启用调试日志。
    --no-progress         禁用单行进度条。
    --color=auto|always|never
                          强制彩色输出（默认：auto）。

SEND OPTIONS:
    --host <HOST>         接收端主机名或 IPv4/IPv6 地址。省略时，发送端在
                          给定端口上监听，等待接收端主动连接。
    -p, --port <PORT>     TCP 端口（默认：9876）。
    -r, --recursive       递归进入目录。
    -L, --follow-symlinks 遍历目录时跟随符号链接。
    --no-zero-copy        强制使用用户态读/写路径（在无 sendfile 的平台
                          做性能剖析时比较有用）。

RECEIVE OPTIONS:
    -p, --port <PORT>     监听的 TCP 端口（默认：9876）。
    -o, --output <DIR>    输出目录（默认：当前工作目录）。
    --no-verify           跳过每个文件的 CRC-32 校验。
```

退出码约定：

| 值 | 含义 |
| --- | --- |
| `0` | 成功 |
| `2` | 参数解析错误或连接失败 |
| `3` | 传输侧失败（CRC 不匹配、I/O 错误等） |

### 典型工作流

#### 场景 1 — 接收端监听，发送端推送

```bash
# 在接收端主机（例如 10.0.0.2）：
xfer recv -o ./dst -p 9876

# 在发送端主机，递归推送一个目录：
xfer send --host 10.0.0.2 -p 9876 -r ./data
```

#### 场景 2 — 发送端监听，接收端主动拉取（反向角色）

```bash
# 在发送端主机（例如 10.0.0.1）：
xfer send --listen -p 9876 ./data

# 在接收端主机：
xfer recv --host 10.0.0.1 -p 9876 -o ./dst
```

### 作为库使用

`xfer_core` 是一个独立的静态库。公共头文件位于 `include/xfer/`，内部头文件通过 `-Isrc` 即可访问。

```cpp
#include <system_error>
#include <string>
#include <vector>

#include "net/socket.h"
#include "transfer/sender.h"
#include "transfer/receiver.h"

// -------- 发送端 --------
xfer::net::Socket sock;
std::error_code ec;
if (sock.Connect("10.0.0.2", 9876, ec)) {
  xfer::transfer::SenderOptions opts;
  opts.recursive = true;
  xfer::transfer::SendPaths(sock, {"./data"}, opts, ec);
}

// -------- 接收端 --------
xfer::net::Socket listener;
listener.Bind(9876, ec);
listener.Listen(16, ec);
auto client = listener.Accept(ec);

xfer::transfer::ReceiverOptions ropts;
ropts.output_dir = "./dst";
xfer::transfer::ReceiverSummary summary;
xfer::transfer::ReceiveStream(client, ropts, summary, ec);
```

---

## 网络协议

所有整数均为**小端**，在 x86 / ARM 上可直接 `memcpy`，无需字节序转换。

```
会话头（32 字节，握手阶段发送一次）：
  [ 4] magic              = 0x58464552 ("XFER", 小端)
  [ 2] version            = 0x0001
  [ 2] reserved           = 0
  [ 8] total_files        (小端)
  [ 8] total_bytes        (小端)
  [ 4] flags              (第 0 位 = 启用 CRC)
  [ 4] 前面 28 个字节的 CRC-32

接收端确认（8 字节）：
  [ 4] magic              = 0x4f4b4159 ("OKAY")
  [ 4] receiver_flags     (保留)

每个文件的记录（重复 total_files 次）：
  [ 4] FILE magic         = 0x46494c45 ("FILE")
  [ 2] name_len           (小端)
  [ 8] file_size          (小端)
  [ 4] sender_flags       (保留)
  [ 4] 载荷的 CRC-32      (若 sender_flags 第 0 位置位)
  [name_len] 相对路径     (UTF-8，以 '/' 分隔)
  [file_size] 原始载荷

尾部结束标记：
  [ 4] DONE magic         = 0x444f4e45 ("DONE")
```

> 接收端对路径穿越尝试（`..`、绝对路径、`\`）会立即拒绝并关闭连接。

---

## 测试

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

独立的测试可执行文件同样位于 `build/bin/`：

| 目标 | 用途 |
| --- | --- |
| `xfer_test_basic` | 冒烟测试 — 验证版本字符串 |
| `xfer_test_crc32` | CRC-32C 正确性，对照已知测试向量 |
| `xfer_test_format` | 字节数与时长格式化测试 |
| `xfer_test_protocol` | 协议编解码往返测试 |
| `xfer_test_roundtrip` | 在 `localhost` 上发送 1 KiB + 4 MiB 随机文件并通过 CRC 验证 |

典型 `ctest` 输出：

```
Test project /path/to/xfer/build
    Start 1: xfer_test_basic
1/5 Test #1: xfer_test_basic .................   Passed    0.28 sec
    Start 2: xfer_test_crc32
2/5 Test #2: xfer_test_crc32 .................   Passed    0.11 sec
    Start 3: xfer_test_format
3/5 Test #3: xfer_test_format ................   Passed    0.09 sec
    Start 4: xfer_test_protocol
4/5 Test #4: xfer_test_protocol ..............   Passed    0.15 sec
    Start 5: xfer_test_roundtrip
5/5 Test #5: xfer_test_roundtrip .............   Passed    0.69 sec

100% tests passed, 0 tests failed out of 5
```

---

## 路线图

按优先级大致排序的规划：

- [ ] 发送端 `--listen`（反向角色，由接收端主动拉取）
- [ ] 多文件并行传输与断点续传（协议 flag 位已预留）
- [ ] 可选的 ChaCha20-Poly1305 / TLS 加密（需引入第三方依赖）
- [ ] 带宽限制与 TCP 窗口调优
- [ ] Windows `TransmitFile` 零拷贝路径（当前回退为用户态 I/O）
- [ ] 更多单元测试：协议编解码、路径穿越防护、Socket 错误路径

---

## 贡献指南

欢迎贡献！可在 `src/` 下新增模块，在 `tests/` 下仿照现有测试编写用例，运行 `./scripts/fmt.sh` 格式化后提交 PR。

推荐的入门方向：

- 从 [路线图](#路线图) 中挑选一项实现
- 为尚未覆盖的代码路径补充单元测试
- 在你使用的平台上 profile 发送路径，并分享结果

---

## 许可证

采用 Apache License, Version 2.0 发布。详见 [LICENSE](LICENSE)。

---

<div align="center">

[← 返回主 README](README.md) • [English](README.en.md)

</div>
