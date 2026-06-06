
#include "pic.hpp"
#include "io.hpp"


namespace
{
    const int PIC1_COMMAND = 0x20;
    const int PIC1_DATA    = 0x21;
    const int PIC2_COMMAND = 0xA0;
    const int PIC2_DATA    = 0xA1;
    const int PIC_EOI      = 0x20;
}

namespace pic
{
    //pic_init
    void InitializePIC(uint8_t offset1, uint8_t offset2)
    {
        // PICを初期化するコードをここに記述
        uint8_t m1 = io::inb(PIC1_DATA);  /* マスク保存 */
        uint8_t m2 = io::inb(PIC2_DATA);

        io::outb(PIC1_COMMAND,  0x11); io::io_wait();  /* ICW1: init + ICW4 */
        io::outb(PIC2_COMMAND,  0x11); io::io_wait();
        io::outb(PIC1_DATA, offset1); io::io_wait();  /* ICW2: ベクタオフセット */
        io::outb(PIC2_DATA, offset2); io::io_wait();
        io::outb(PIC1_DATA, 0x04); io::io_wait();  /* ICW3: cascade at IRQ2 */
        io::outb(PIC2_DATA, 0x02); io::io_wait();
        io::outb(PIC1_DATA, 0x01); io::io_wait();  /* ICW4: 8086 mode */
        io::outb(PIC2_DATA, 0x01); io::io_wait();

        io::outb(PIC1_DATA, m1);   /* マスク復元 */
        io::outb(PIC2_DATA, m2);


    }

    void InitializePIT()
    {
      
    io::outb(0x43, 0x36); // チャンネル0、モード3、二進数カウンタ
    uint16_t divisor = 1193182; //1193182Hz / 100Hz
    io::outb(0x40, divisor & 0xFF); // カウンタの下位8ビット
    io::outb(0x40, (divisor >> 8) & 0xFF); // カウンタの上位8ビット
    }

    void mask_irq(uint8_t irq)
    {
        // 指定されたIRQをマスクするコードをここに記述
        uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
        if (irq >= 8) irq -= 8;
        io::outb(port, io::inb(port) | (1 << irq));

    }

    void unmask_irq(uint8_t irq)
    {
        // 指定されたIRQのマスクを解除するコードをここに記述
        uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
        if (irq >= 8) irq -= 8;
        io::outb(port, io::inb(port) & ~(1 << irq));
    }

    void send_eoi(uint8_t irq)
    {
        // 指定されたIRQに対してEOIを送信するコードをここに記述
        if (irq >= 8) {
            io::outb(PIC2_COMMAND, PIC_EOI);
        }
        io::outb(PIC1_COMMAND, PIC_EOI);
    }
} // namespace pic
