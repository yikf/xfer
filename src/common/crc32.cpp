// ============================================================================
//  CRC-32C 软件查表实现
// ============================================================================
#include "common/crc32.h"

#include <cstring>

namespace xfer {
namespace common {

namespace {

// 256 项的静态查表，每项 4 字节。
// 构建方式与标准 CRC-32 一致，只是把多项式换成 0x1EDC6F41（Castagnoli）。
// 小端查表算法：对每个字节 d（0..255），把最低位移出，若该位为 1 则
// 将 32-bit 多项式 XOR 进来；循环 8 次即得到表项 table[d]。
alignas(64) std::uint32_t g_table[256];
bool g_table_ready = false;

void BuildTable() {
  if (g_table_ready) return;
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int j = 0; j < 8; ++j) {
      // 「& 1u」提取最低位；为 1 表示要把多项式 XOR 进来。
      c = (c & 1u) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
    }
    g_table[i] = c;
  }
  g_table_ready = true;
}

// C++11 之后，具有静态存储期的对象会在首次进入所在作用域时
// **线程安全地** 初始化（Meyers' Singleton 模式）。
// 这里通过在匿名命名空间里放一个带副作用的静态对象，
// 保证在第一次使用 CRC 前表已构建。
struct TableInitializer {
  TableInitializer() { BuildTable(); }
};
// 注意：此处的 `static` 指「内部链接」—— 每个 .cpp 单独包含
// 本头文件（如果有的话）都会有一份自己的 g_table / g_table_ready，
// 但我们把表放在 .cpp 里，所以只有一份实例。
static TableInitializer g_init;

}  // namespace

Crc32::Crc32() : state_(0xFFFFFFFFu) {}

// `Update` 核心循环：对每个字节做一次「查表 + 异或」。
//   state = table[(state ^ byte) & 0xFF] ^ (state >> 8)
// 这是经典的「小端字节序 + 右移」版本，它输出的 CRC-32 值
// 与 zlib / Intel CRC32 指令 / Linux crc32c.ko 的结果完全一致。
void Crc32::Update(const void* data, std::size_t length) {
  auto p = static_cast<const unsigned char*>(data);
  std::uint32_t s = state_;
  // 注意：这里使用 `while (length--)` 而非 for 循环，是为了避免额外
  // 的计数器变量。编译器会把它优化为与 for 等价的形式。
  while (length--) s = g_table[(s ^ *p++) & 0xFF] ^ (s >> 8);
  state_ = s;
}

std::uint32_t Crc32::Final() const { return state_ ^ 0xFFFFFFFFu; }

std::uint32_t Crc32::Compute(const void* data, std::size_t length) {
  Crc32 c;
  c.Update(data, length);
  return c.Final();
}

}  // namespace common
}  // namespace xfer
