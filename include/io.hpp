#pragma once
#include <cstdint>

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
} // namespace io
