#pragma once
#include "types.hpp"

// 実装は io/io.asm にある (System V AMD64 ABI)。
// extern "C" によりシンボル名は名前空間を付けずに out32b / in32b / io_wait となる。
namespace io {
extern "C" {
void out32b(uint16_t port, uint32_t val);
uint32_t in32b(uint16_t port);
void io_wait(void);
}
}
