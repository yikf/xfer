// ============================================================================
//  xfer — 内网快速文件传输工具
//  ---------------------------------------------------------------------------
//  文件版本号定义。CMake 构建系统使用 PROJECT_VERSION / PROJECT_VERSION_*
//  来驱动版本号，这里在头文件中同步一份，方便在 C++ 源码中通过宏访问。
// ============================================================================
#ifndef XFER_VERSION_H
#define XFER_VERSION_H

// 遵循语义化版本号（Semantic Versioning 2.0.0）：
//   MAJOR  不兼容的 API 改动时递增
//   MINOR  向下兼容的功能性新增时递增
//   PATCH  向下兼容的问题修复时递增
#define XFER_VERSION_MAJOR 0
#define XFER_VERSION_MINOR 1
#define XFER_VERSION_PATCH 0

// 字符串形式，方便日志/--version 使用。
// 注意：此处是宏，在代码里会被预处理器就地展开为字符串字面量。
#define XFER_VERSION_STR "0.1.0"

#endif  // XFER_VERSION_H
