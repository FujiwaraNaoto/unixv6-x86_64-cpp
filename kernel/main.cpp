#include "types.hpp"
#include "vga.hpp"
#include "serial.hpp"
#include "idt.hpp"
#include "pic.hpp"

extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, [[maybe_unused]] uint32_t mb_addr)
{
    // NOTE:
    // グローバル変数でコンストラクタを呼び出して確実に初期化させるためには　CRT相当を自作しなければならず，難しいため，ここで明示的に初期化する．
    serial::serial.initialize();
    // print_banner();
    vga::vga.puts("  [ ] IDT / interrupt handlers\n");
    vga::vga.puts("  [ ] Physical memory allocator\n");

    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる
    idt::InterruptDescriptorTable idt;
    vga::vga.puts("  IDT / interrupt handlers\n");
    

    while (1)
    {
        asm volatile("hlt");
    }
}
