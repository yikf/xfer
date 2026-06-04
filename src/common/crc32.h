// ============================================================================
//  CRC-32C (Castagnoli, 多项式 0x1EDC6F41)
//  ---------------------------------------------------------------------------
//  Castagnoli 多项式在现代 Intel/AMD CPU 上可由 CRC32 指令加速（本实现
//  为可移植性使用软件查表；保留未来替换为硬件加速的接口不变性）。
//
//  参考：https://en.wikipedia.org/wiki/Cyclic_redundancy_check
// ============================================================================
#ifndef XFER_COMMON_CRC32_H
#define XFER_COMMON_CRC32_H

#include <cstddef>
#include <cstdint>

namespace xfer {
namespace common {

// 增量式 CRC 计算器。用法：
//   Crc32 c;
//   c.Update(buf1, len1);
//   c.Update(buf2, len2);
//   uint32_t checksum = c.Final();
//
//  —— 设计说明 ——
//  * 类内只保有一个 32-bit 状态字（192-bit 对齐的表是静态的），
//    对象大小仅为 4 字节，非常适合值传递到 lambda / 任务队列。
//  * `Update` 可安全调用任意次；多次调用等同于把字节拼接后调用一次。
class Crc32 {
 public:
  // 初始状态为全 1；异或 0xFFFFFFFF 是 CRC-32 族的约定，使空字串
  // 也能产生非零的校验值。
  Crc32();

  // 将 [data, data+length) 折叠进 CRC 状态。
  void Update(const void* data, std::size_t length);

  // 返回最终 CRC 值（与 CRC32C 标准的异或常数 0xFFFFFFFF 合并）。
  // `const`：此方法承诺不修改 *this，因此可在 const 引用上调用。
  std::uint32_t Final() const;

  // 一次性便捷函数；等价于 Crc32{}.Update(...).Final()。
  // 为「可能长」的缓冲区计算 CRC 时，不建议使用此函数 — 它会丢失
  // 「分片调用」的灵活性。
  static std::uint32_t Compute(const void* data, std::size_t length);

 private:
  std::uint32_t state_;
};

}  // namespace common
}  // namespace xfer

#endif  // XFER_COMMON_CRC32_H
