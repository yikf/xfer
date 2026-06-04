// Copyright 2026 The xfer Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

// xfer - 主程序入口 (main.cpp)
//
// 职责：
//   1) 解析命令行 → Options；
//   2) 根据 opts.mode 分发到帮助/版本/发送/接收；
//   3) 将 RunSend / RunReceive 的返回值作为进程退出码。
//
// 退出码约定：
//   0: 成功
//   2: 参数错误 / 连接失败
//   3: 传输过程中失败（CRC 不匹配、磁盘 I/O 错误等）

#include <cstdio>
#include <cstdlib>

#include "cli.h"
#include "xfer/version.h"

int main(int argc, char** argv) {
    xfer::cli::Options opts;
    // 解析失败：已由 Parse 打印错误信息，进程返回 2。
    if (!xfer::cli::Parse(argc, argv, opts)) return 2;

    // 根据 mode 分发。
    switch (opts.mode) {
        case xfer::cli::Mode::Help:
            xfer::cli::PrintHelp();
            return 0;
        case xfer::cli::Mode::Version:
            std::printf("xfer %s\n", XFER_VERSION_STR);
            return 0;
        case xfer::cli::Mode::Send:
            return xfer::cli::RunSend(opts);
        case xfer::cli::Mode::Receive:
            return xfer::cli::RunReceive(opts);
        default:
            // mode 未设置（例如 "xfer" 无任何参数）：打印帮助。
            xfer::cli::PrintHelp();
            return 2;
    }
}
