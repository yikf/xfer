// xfer - Smoke test (tests/test_basic.cpp)
//
// 目的：验证编译系统正常工作、核心常量可访问、版本号符合预期。
// 这是一个最小化的"集成冒烟测试"，不依赖任何第三方测试框架。
//
// 运行：
//   cmake -B build && cmake --build build -j
//   ./build/bin/xfer_basic_test  # 返回值 0 表示成功，非零失败
//
// Purpose: Verify the build system works, core constants are reachable,
// and the version string is what we expect. This is a minimal "smoke test"
// that doesn't depend on any third-party test framework.

#include <cstdio>
#include <cstring>

#include "xfer/version.h"

int main() {
    // 打印版本号。
    std::printf("xfer version: %s\n", XFER_VERSION_STR);
    // 版本号应为 "0.1.0"（与 include/xfer/version.h 中定义一致）。
    if (std::strcmp(XFER_VERSION_STR, "0.1.0") != 0) return 1;
    return 0;
}
