#include <cstdint>

extern "C" void kernel_main(uint32_t mb_magic, uint32_t mb_addr)
{
    vga::init();
    print_banner();
    vga::puts("  [ ] IDT / interrupt handlers\n");
    vga::puts("  [ ] Physical memory allocator\n");

    while(1){
        asm volatile("hlt");
    }
}
