
#include "pic.hpp"
#include "vga.hpp"

static uint32_t timer_ticks = 0;

/* IRQ0: PIT タイマー (約 100 Hz) */
void irq0_handler(void) {
    timer_ticks++;
    if (timer_ticks % 100 == 0) {
        vga::vga.set_color(Color::DarkGrey, Color::Black);
        vga::vga.printf("[TIMER] %u sec\n", timer_ticks / 100);
        vga::vga.set_color(Color::LightGrey, Color::Black);
    }
    pic::send_eoi(0);
}

/* その他 IRQ: とりあえず EOI だけ送る */
void irq_handler(uint8_t irq) {
    pic::send_eoi(irq);
}
