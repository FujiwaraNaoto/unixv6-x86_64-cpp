#include "types.hpp"
#include "vga.hpp"
#include "serial.hpp"

extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, [[maybe_unused]] uint32_t mb_addr)
{
    // NOTE:
    // グローバル変数でコンストラクタを呼び出して確実に初期化させるためには　CRT相当を自作しなければならず，難しいため，ここで明示的に初期化する．
    serial::serial.initialize();
    // print_banner();
    vga::vga.puts("  [ ] IDT / interrupt handlers\n");
    vga::vga.puts("  [ ] Physical memory allocator\n");

    while (1)
    {
        asm volatile("hlt");
    }
}
