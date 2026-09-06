#include "tests.hpp"
#include "keyboard.hpp"
#include "vga.hpp"

namespace tests
{

void keyboard_echo()
{
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->puts("\n[KBD]  type something (echo test):\n> ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    while (1)
    {
        if (keyboard::has_input())
        {
            char c = keyboard::getchar();
            vga::vga->putchar(c);
            if (c == '\n')
            {
                vga::vga->puts("> ");
            }
        }
        asm volatile("hlt");
    }
}

} // namespace tests
