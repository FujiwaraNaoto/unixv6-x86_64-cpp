
#include "exception.hpp"
#include "vga.hpp"
#include "pic.hpp"

namespace exception
{
    //isr.asmも参照
    constexpr int NUM_EXCEPTIONS = 22;
    static const char* exception_messages[NUM_EXCEPTIONS] = {
        "Division By Zero",
        "Debug",
        "Non Maskable Interrupt",
        "Breakpoint",
        "Into Detected Overflow",
        "Bound Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack-Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating-Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
    };

    void isr_common_handler(register_state_t *regs){
        vga::vga.set_color(Color::White, Color::Red);
        vga::vga.printf("**Exception **");
        if(regs->int_no < NUM_EXCEPTIONS){
            vga::vga.printf(": %s\n", exception_messages[regs->int_no]);

        }else{
            vga::vga.printf(": Unknown Exception\n");
        }

        vga::vga.printf("RIP: %016llx,CS=0x%016llx, RFLAGS=0x%016llx\n", regs->rip, regs->cs, regs->rflags);
        vga::vga.printf("RSP: %016llx, SS=0x%016llx\n", regs->rsp, regs->ss);
        while(1){
            asm volatile("hlt");
        }



    }

    static uint64_t timer_ticks = 0;

    // ─── IRQ ハンドラ (isr.asm から呼ばれる) ─────────────────────
    // 今は EOI を返すだけの最小実装。実処理は今後追加する。
    extern "C" void irq0_handler()
    {
        // IRQ0: タイマ
        timer_ticks++;
        if (timer_ticks % 100 == 0) {
            vga::vga.set_color(Color::DarkGrey, Color::Black);
            vga::vga.printf("[TIMER] %u sec\n", timer_ticks / 100);
            vga::vga.set_color(Color::LightGrey, Color::Black);
        }
        pic::send_eoi(0);
    }

    // TODO: IRQ1 (キーボード) 以降のハンドラも実装する。 --- IGNORE ---
    extern "C" void irq_handler(uint64_t irq_no)
    {
        pic::send_eoi(static_cast<uint8_t>(irq_no));
    }

}
