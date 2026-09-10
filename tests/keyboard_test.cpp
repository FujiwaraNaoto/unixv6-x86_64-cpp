#include "tests.hpp"
#include "keyboard.hpp"

namespace tests
{

void keyboard_echo(IConsole *console)
{
    console->set_color(Color::LightCyan, Color::Black);
    console->puts("\n[KBD]  type something (echo test):\n> ");
    console->set_color(Color::LightGrey, Color::Black);
    while (1)
    {
        if (keyboard::has_input())
        {
            char c = keyboard::getchar();
            console->putchar(c);
            if (c == '\n')
            {
                console->puts("> ");
            }
        }
        asm volatile("hlt");
    }
}

} // namespace tests
