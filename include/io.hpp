#pragma once
#include <cstdint>
#include <type_traits>

// 実装は io/io.asm にある (System V AMD64 ABI)。
// extern "C" によりシンボル名は名前空間を付けずに outb / in16b / out32b / in32b / io_wait となる。
namespace io
{
extern "C"
{
    void outb(uint16_t port, uint8_t val); // 1バイト幅レジスタ(UART等)用
    uint8_t inb(uint16_t port);
    void out16b(uint16_t port, uint16_t val); // 2バイト幅レジスタ(virtio legacy等)用
    uint16_t in16b(uint16_t port);
    void out32b(uint16_t port, uint32_t val);
    uint32_t in32b(uint16_t port);
    void io_wait(void);
}

// 1バイト幅レジスタ用の値を enum class で定義している場合 (pic.cpp の Icw1 / Icw4 など) に、
// 呼び出し側でキャストせずそのまま渡せるようにする。
// 中身が uint8_t の enum class だけを受け取ることで型システムの恩恵を受けつつ、キャストの手間を省く。
template <typename Enum>
requires std::is_enum_v<Enum> && std::is_same_v<std::underlying_type_t<Enum>, uint8_t>
inline void outb(uint16_t port, Enum value)
{
    outb(port, static_cast<uint8_t>(value));
}
} // namespace io
