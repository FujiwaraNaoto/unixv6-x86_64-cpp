#pragma once
#include "types.hpp"

// 実装は io/io.asm にある (System V AMD64 ABI)。
// extern "C" によりシンボル名は名前空間を付けずに outb / inb / io_wait となる。
namespace io {
extern "C" {
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void io_wait(void);
}
}
