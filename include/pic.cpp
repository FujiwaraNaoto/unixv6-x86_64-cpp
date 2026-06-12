
#include "pic.hpp"
#include "io.hpp"


namespace
{
const int PIC1_COMMAND = 0x20;
const int PIC1_DATA    = 0x21;
const int PIC2_COMMAND = 0xA0;
const int PIC2_DATA    = 0xA1;
const int PIC_EOI      = 0x20;

const int PIT_CHANNEL0 = 0x40;
const int PIT_COMMAND  = 0x43;
// 8253/8254 PIT input clock: 14.31818MHz / 12 = 1.193182MHz (established PC spec).
// https://f.osdev.org/viewtopic.php?t=15503
// https://en.wikipedia.org/wiki/Color_burst
const uint32_t PIT_FREQUENCY = 1193182;
} // namespace

namespace pic
{
// pic_init
void InitializePIC(uint8_t offset1, uint8_t offset2)
{
    uint8_t m1 = io::inb(PIC1_DATA); /* マスク保存 */
    uint8_t m2 = io::inb(PIC2_DATA);

    io::outb(PIC1_COMMAND, 0x11);
    io::io_wait(); /* ICW1: init + ICW4 */
    io::outb(PIC2_COMMAND, 0x11);
    io::io_wait();
    io::outb(PIC1_DATA, offset1);
    io::io_wait(); /* ICW2: ベクタオフセット */
    io::outb(PIC2_DATA, offset2);
    io::io_wait();
    io::outb(PIC1_DATA, 0x04);
    io::io_wait(); /* ICW3: cascade at IRQ2 */
    io::outb(PIC2_DATA, 0x02);
    io::io_wait();
    io::outb(PIC1_DATA, 0x01);
    io::io_wait(); /* ICW4: 8086 mode */
    io::outb(PIC2_DATA, 0x01);
    io::io_wait();

    io::outb(PIC1_DATA, m1); /* マスク復元 */
    io::outb(PIC2_DATA, m2);
}

void InitializePIT(uint32_t hz)
{
    io::outb(PIT_COMMAND, 0x36); // channel 0, mode 3 (square wave), lobyte/hibyte access, binary counter

    uint16_t divisor = PIT_FREQUENCY / hz;

    // The 16-bit divisor must be written as two 8-bit writes (low byte first,
    // then high byte). The PIT data port is only 8 bits wide, and the
    // lobyte/hibyte access mode set in the command byte above tells the chip
    // to latch the two consecutive writes in that order. A single 16-bit OUT
    // would not work: the x86 bus splits it into byte writes to ports 0x40
    // and 0x41, sending the high byte to channel 1 instead of channel 0.
    io::outb(PIT_CHANNEL0, divisor & 0xFF);
    io::outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void mask_irq(uint8_t irq)
{
    // 指定されたIRQをマスクするコードをここに記述
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8)
        irq -= 8;
    io::outb(port, io::inb(port) | (1 << irq));
}

void unmask_irq(uint8_t irq)
{
    // 指定されたIRQのマスクを解除するコードをここに記述
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8)
        irq -= 8;
    io::outb(port, io::inb(port) & ~(1 << irq));
}

void send_eoi(uint8_t irq)
{
    // 指定されたIRQに対してEOIを送信するコードをここに記述
    if (irq >= 8)
    {
        io::outb(PIC2_COMMAND, PIC_EOI);
    }
    io::outb(PIC1_COMMAND, PIC_EOI);
}
} // namespace pic
