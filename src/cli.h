// xfer - CLI 模块：命令行参数解析与子命令分发
//
// 命令行语法：
//   xfer send  [--host HOST] [-p PORT] [-r] [-L] [--no-zero-copy] <file-or-dir>...
//   xfer recv  [-p PORT] [-o DIR] [--no-verify]
//   xfer --help / -h    : 打印帮助
//   xfer --version / -V : 打印版本
//
// 常见选项：
//   -v, --verbose  : 开启 DEBUG 日志
//   --no-progress  : 关闭进度条
//   --color=MODE   : always|auto|never 控制日志颜色
//
// 设计：Parse 将 argc/argv 解析为 Options 结构，
// 然后由调用方（main.cpp）根据 opts.mode 调用 RunSend 或 RunReceive。
// 这种"解析/运行"分离便于单元测试。

#ifndef XFER_CLI_H
#define XFER_CLI_H

#include <string>
#include <system_error>
#include <vector>

namespace xfer {
namespace cli {

// 子命令类型。
// 由 Parse 根据第一个非选项参数设置。
enum class Mode {
    Unset,      // 未指定 / 参数缺失
    Send,       // 发送端：推送文件到远端
    Receive,    // 接收端：从 sender 拉取或监听等待
    Help,       // --help
    Version,    // --version
};

// 完整的命令行选项集合。
struct Options {
    Mode mode = Mode::Unset;

    // Common
    bool verbose = false;       // 是否开启 DEBUG 日志
    bool no_progress = false;   // 关闭进度条
    bool force_color = false;   // --color=always
    int port = 9876;            // TCP 端口

    // Send-side
    std::string host;           // 远端 host；空字符串表示 sender 监听等待
    bool listen = false;        // sender 在端口上监听，等待 receiver 主动连接
    bool recursive = false;     // -r / --recursive 递归进入目录
    bool follow_symlinks = false;  // -L：遍历目录时跟随符号链接
    bool no_zero_copy = false;  // 禁用 sendfile()，强制用户态路径（用于性能对比/调试）
    std::vector<std::string> paths;  // send 模式下：要发送的文件或目录列表

    // Receive-side
    std::string output_dir;     // -o / --output：输出目录
    bool no_verify = false;     // 关闭 CRC-32 校验
};

// 解析 argc/argv。成功返回 true；失败打印错误到 stderr 并返回 false。
// Parse argc/argv into `opts`.  Returns true on success. On failure, prints
// an error message to stderr and returns false.
bool Parse(int argc, char** argv, Options& opts);

// 将帮助文本打印到 stdout。
// Print usage/help text to stdout.
void PrintHelp();

// 执行发送/接收主循环。成功返回 0，失败返回非零（供 main() 用作 exit code）。
// Entry points
int RunSend(const Options& opts);
int RunReceive(const Options& opts);

}  // namespace cli
}  // namespace xfer

#endif  // XFER_CLI_H
