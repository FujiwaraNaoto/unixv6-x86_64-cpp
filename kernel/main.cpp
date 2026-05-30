#include "types.hpp"
#include "vga.hpp"

extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, [[maybe_unused]] uint32_t mb_addr)
{
    vga::initialize();
    //print_banner();
    vga::puts("  [ ] IDT / interrupt handlers\n");
    vga::puts("  [ ] Physical memory allocator\n");

    while(1){
        asm volatile("hlt");
    }
}
