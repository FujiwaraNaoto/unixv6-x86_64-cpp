#include "tests.hpp"

namespace tests
{

// 意図的に 0 除算 (#DE) を発生させて isr_common_handler を起こす。
// volatile を使わないと 1/0 はコンパイル時に畳まれ div 命令が出ないため、
// 実行時に必ず CPU 例外が起きるよう除数をメモリ経由にする。
void division_by_zero()
{
    volatile int zero = 0;
    volatile int x    = 1 / zero;
    (void)x;
}

} // namespace tests
