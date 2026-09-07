#include <cstdint>
#include "tests.hpp"
#include "vga.hpp"

namespace tests
{

void higher_half_check()
{
    uint64_t rip;
    asm volatile("lea (%%rip), %0" : "=r"(rip));

    vga::vga->set_color(Color::LightMagenta, Color::Black);
    vga::vga->puts("[HIGH] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("running at RIP=0x%x (high-half if >0xFFFFFFFF80000000)\n",
                     (unsigned)(rip >> 32)); // 上位32bitを表示

    vga::vga->set_color(Color::LightMagenta, Color::Black);
    vga::vga->puts("[HIGH] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->puts("VGA via direct map OK\n");
}

} // namespace tests
