<div align="right">

[简体中文](README.zh-CN.md) | **English**

</div>

<div align="center">

<a href="https://github.com/yikf/xfer">
  <h1><code>xfer</code></h1>
</a>

**High-performance intranet file transfer tool — a faster, easier alternative to `scp` on the LAN.**

<br>

[![GitHub](https://img.shields.io/badge/GitHub-yikf%2Fxfer-181717?logo=github)](https://github.com/yikf/xfer)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C?logo=cmake)](https://cmake.org/)
[![Version](https://img.shields.io/badge/version-0.1.0-green.svg)](include/xfer/version.h)

</div>

---

## Table of Contents

- [About the Project](#about-the-project)
- [Features](#features)
- [Project Layout](#project-layout)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Building](#building)
  - [Build Options](#build-options)
- [Usage](#usage)
  - [Command-Line Reference](#command-line-reference)
  - [Typical Workflows](#typical-workflows)
  - [As a Library](#as-a-library)
- [Wire Protocol](#wire-protocol)
- [Testing](#testing)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## About the Project

`xfer` is a fast intranet file transfer tool, designed to move large files across a trusted LAN (1 Gbps / 10 Gbps / 25 Gbps) with the least possible overhead.

The `scp`/`ssh` stack is general-purpose and great for the open internet, but inside a private network the encryption layer and the per-packet copy overhead become bottlenecks. `xfer` replaces them with:

- a **small custom TCP protocol** that keeps the wire format tiny;
- **`sendfile(2)` zero-copy** on Linux and macOS — no round-trip through userspace buffers;
- a **big-block streaming I/O** fallback (1 MiB) on platforms without `sendfile`.

The current release is **0.1.0**.

---

## Features

| Feature | Details |
| --- | --- |
| Cross-platform | Linux / macOS / Windows via a thin RAII socket wrapper |
| Zero-copy send path | `sendfile(2)` on Linux/macOS, 1 MiB userspace fallback on Windows |
| Wire protocol | Little-endian `[magic | flags | size | name | payload]` records |
| Integrity | Per-file CRC-32 (Castagnoli) — on by default; `--no-verify` to disable |
| Safety | Receiver rejects path-traversal attempts (`..`, absolute paths, `\`) |
| UX | Colored leveled logger, single-line progress bar (ETA / throughput / bytes) on TTYs |
| Build system | CMake (C++17) with AddressSanitizer + UBSan, and Google Test |
| Code style | clang-format — Google style, 4-space indent, 120-char column |
| Tests | `ctest` round-trip tests on `localhost` (1 KiB + 4 MiB files, CRC verified) |

---

## Project Layout

```
xfer/
├── CMakeLists.txt              # top-level build configuration
├── src/
│   ├── CMakeLists.txt          # xfer_core static library + xfer executable
│   ├── main.cpp                # dispatches send / recv / help / version
│   ├── cli.h / cli.cpp         # argument parser + sub-command runner
│   ├── common/
│   │   ├── logging.h/.cpp      # colored leveled logger
│   │   ├── format.h/.cpp       # human-readable byte / duration formatters
│   │   ├── stopwatch.h/.cpp    # steady-clock timer
│   │   ├── crc32.h/.cpp        # CRC-32C (Castagnoli)
│   │   └── progress.h/.cpp     # single-line progress bar (ETA / speed)
│   ├── net/
│   │   └── socket.h/.cpp       # RAII socket + sendfile zero-copy helpers
│   └── transfer/
│       ├── protocol.h/.cpp     # session handshake + file-record codec
│       ├── sender.h/.cpp       # directory walk, CRC, streaming send
│       └── receiver.h/.cpp     # record parser, CRC verify, safe writes
├── include/xfer/
│   ├── version.h               # XFER_VERSION_{MAJOR,MINOR,PATCH,STR}
│   └── protocol.h              # public magic / flag constants
├── tests/
│   ├── CMakeLists.txt          # individual test executables
│   ├── test_basic.cpp          # smoke test — version string
│   ├── test_crc32.cpp          # CRC-32 correctness tests
│   ├── test_format.cpp         # byte / duration formatter tests
│   ├── test_protocol.cpp       # wire protocol codec tests
│   └── test_roundtrip.cpp      # localhost round-trip test (with CRC)
├── scripts/
│   ├── build.sh                # one-command Release build
│   ├── clean.sh                # remove all build directories
│   ├── fmt.sh                  # run clang-format on the source tree
│   └── test.sh                 # configure and run ctest
├── .clang-format               # Google style, 4-space indent, 120 cols
├── .gitignore
└── build/                      # build output — bin/xfer, lib/libxfer_core.a
```

---

## Getting Started

### Prerequisites

- **CMake ≥ 3.16**
- A **C++17 toolchain**: GCC 8+, Clang 7+, AppleClang 12+, or MSVC 2019+
- (Optional) Google Test — bundled via CMake's `FetchContent` when `XFER_BUILD_TESTS=ON`
- (Optional) `clang-format` — to reformat the source tree

### Building

Three commands are enough:

```bash
# 1. Configure (Release build)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 2. Compile in parallel
cmake --build build -j

# 3. Run the test suite
ctest --test-dir build --output-on-failure
```

The binary ends up at `build/bin/xfer`, and the `xfer_core` static library at
`build/lib/libxfer_core.a`.

For a **sanitized Debug build**:

```bash
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DXFER_ENABLE_ASAN=ON
cmake --build build-debug -j
```

### Build Options

The project exposes a handful of CMake cache variables. Pass them with `-D<option>=<value>` at configure time.

| Option | Default | Description |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` |
| `XFER_BUILD_TESTS` | `ON` | Whether to build the `tests/` tree with Google Test |
| `XFER_ENABLE_ASAN` | `ON` | Enables AddressSanitizer + UndefinedBehaviorSanitizer in `Debug` builds |
| `XFER_WARNINGS_AS_ERRORS` | `ON` | Treats compiler warnings as errors |

---

## Usage

### Command-Line Reference

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

Exit codes:

| Value | Meaning |
| --- | --- |
| `0` | Success |
| `2` | Argument parsing error or connection failure |
| `3` | Transfer-side failure (CRC mismatch, I/O error, etc.) |

### Typical Workflows

#### Scenario 1 — receiver listens, sender pushes

```bash
# On the receiver host (e.g. 10.0.0.2):
xfer recv -o ./dst -p 9876

# On the sender host, push a directory recursively:
xfer send --host 10.0.0.2 -p 9876 -r ./data
```

#### Scenario 2 — sender listens, receiver pulls (reverse role)

```bash
# On the sender host (e.g. 10.0.0.1):
xfer send --listen -p 9876 ./data

# On the receiver host:
xfer recv --host 10.0.0.1 -p 9876 -o ./dst
```

### As a Library

`xfer_core` is a standalone static library. Public headers live under
`include/xfer/`; internal headers are reachable via `-Isrc`.

```cpp
#include <system_error>
#include <string>
#include <vector>

#include "net/socket.h"
#include "transfer/sender.h"
#include "transfer/receiver.h"

// -------- Sender --------
xfer::net::Socket sock;
std::error_code ec;
if (sock.Connect("10.0.0.2", 9876, ec)) {
  xfer::transfer::SenderOptions opts;
  opts.recursive = true;
  xfer::transfer::SendPaths(sock, {"./data"}, opts, ec);
}

// -------- Receiver --------
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

## Wire Protocol

All integers are **little-endian** and can be `memcpy`-d on x86 / ARM without byte-swapping.

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

> Path-traversal attempts (`..`, absolute paths, `\`) are rejected by the receiver and close the connection immediately.

---

## Testing

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Individual test binaries are also built to `build/bin/`:

| Target | Purpose |
| --- | --- |
| `xfer_test_basic` | Smoke test — validates the version string |
| `xfer_test_crc32` | CRC-32C correctness against known test vectors |
| `xfer_test_format` | Byte and duration formatter tests |
| `xfer_test_protocol` | Wire-protocol encode / decode round-trips |
| `xfer_test_roundtrip` | `localhost` send + recv for 1 KiB + 4 MiB random files, CRC verified |

Typical `ctest` output:

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

## Roadmap

Planned work — listed roughly in priority order:

- [ ] `--listen` on the sender (reverse-role, receiver-pull flow)
- [ ] Parallel multi-file transfers and resumable sessions (flag bits are already reserved in the wire protocol)
- [ ] Optional ChaCha20-Poly1305 / TLS encryption (requires a third-party dependency)
- [ ] Bandwidth limiting and TCP window tuning
- [ ] Windows `TransmitFile` zero-copy path (currently falls back to userspace I/O)
- [ ] More unit tests — protocol codec, path-traversal guard, socket error paths

---

## Contributing

Contributions are welcome! Drop new modules under `src/`, mirror the test shape in `tests/`, run `./scripts/fmt.sh`, and open a PR.

Good starting points:

- Pick an item from the [Roadmap](#roadmap)
- Add a unit test for an uncovered code path
- Profile the send path on your platform and share the results

---

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

---

<div align="center">

[← back to README](README.md) • [简体中文](README.zh-CN.md)

</div>
