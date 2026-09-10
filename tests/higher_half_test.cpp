#include <cstdint>
#include "tests.hpp"

namespace tests
{

void higher_half_check(IConsole *console)
{
    uint64_t rip;
    asm volatile("lea (%%rip), %0" : "=r"(rip));

    console->set_color(Color::LightMagenta, Color::Black);
    console->puts("[HIGH] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("running at RIP=0x%x (high-half if >0xFFFFFFFF80000000)\n",
                    (unsigned)(rip >> 32)); // 上位32bitを表示

    console->set_color(Color::LightMagenta, Color::Black);
    console->puts("[HIGH] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->puts("VGA via direct map OK\n");
}

} // namespace tests
