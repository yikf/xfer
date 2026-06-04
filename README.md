<div align="right">

[English](README.en.md) | [简体中文](README.zh-CN.md)

</div>

<div align="center">

# xfer

**High-performance intranet file transfer tool**

> 一个比 `scp` 更快、更方便的内网文件传输工具。

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C.svg)](https://cmake.org/)
[![Version](https://img.shields.io/badge/version-0.1.0-green.svg)](include/xfer/version.h)

</div>

---

Please select your language / 请选择您的语言：

- **[English](README.en.md)** — Full documentation in English
- **[简体中文](README.zh-CN.md)** — 完整的中文文档

---

## Quick Overview

`xfer` 是一个为内网环境（1 Gbps / 10 Gbps / 25 Gbps）优化的文件传输工具。它通过自定义 TCP 协议、`sendfile(2)` 零拷贝以及大分块流式 I/O，在内网环境下高效分发大文件。

`xfer` is a fast intranet file transfer tool optimized for trusted LANs (1 Gbps / 10 Gbps / 25 Gbps). It ships large files efficiently using a custom TCP protocol, `sendfile(2)` zero-copy, and large-block streaming I/O.

### Features at a glance

- **Cross-platform**: Linux / macOS / Windows with a RAII socket wrapper
- **Zero-copy**: `sendfile(2)` on Linux/macOS, 1 MiB userspace fallback on Windows
- **Safe**: CRC-32 per file, path-traversal protection in the receiver
- **Developer friendly**: CMake (C++17), ASan/UBSan, Google Test, clang-format
- **UX**: Colored logger, single-line progress bar with ETA and throughput

See [README.en.md](README.en.md) or [README.zh-CN.md](README.zh-CN.md) for the complete guide.

### Getting started

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# Send a directory to a receiver
./build/bin/xfer send --host 10.0.0.2 -p 9876 -r ./data

# On the receiver host
./build/bin/xfer recv -p 9876 -o ./dst
```

---

<div align="center">

[English](README.en.md) • [简体中文](README.zh-CN.md)

</div>
