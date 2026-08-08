#include "usermode.hpp"
#include "vga.hpp"

extern "C" [[noreturn]] void enter_usermode(uint64_t entry, uint64_t user_stack);

namespace usermode
{

void enter(uint64_t entry, uint64_t user_stack)
{
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[USER] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->puts("entering ring 3...\n");
    enter_usermode(entry, user_stack);
}

} // namespace usermode
