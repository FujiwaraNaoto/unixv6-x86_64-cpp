#include "exception.hpp"
#include "serial.hpp"
#include "pic.hpp"
#include "process.hpp"
#include "keyboard.hpp"
#include "console.hpp"
#include <array>

namespace exception
{
namespace
{

// ハンドラの出力先。未登録のときはシリアルに出す。
// 例外ハンドラでは NullConsole を既定値にしない: 登録前に例外が起きると、
// 何も表示されずに hlt で止まるだけになり原因を追えなくなるため。
// シリアルはヒープなどに依存しないので、クラッシュ報告の出力先として最も確実。
// 実体は kernel_main が作ることを期待しているので、nullptr で初期化しておく。
IConsole *handler_console = nullptr;

} // namespace

void set_console(IConsole *console)
{
    handler_console = (console != nullptr) ? console : serial::serial;
}

// isr.asmも参照
constexpr int NUM_EXCEPTIONS                          = 22;
static std::array<const char *, NUM_EXCEPTIONS> exception_messages = {
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

void isr_common_handler(register_state_t *regs)
{
    handler_console->set_color(Color::White, Color::Red);
    handler_console->printf("**Exception **");
    if (regs->int_no < exception_messages.size())
    {
        handler_console->printf(": %s\n", exception_messages[regs->int_no]);
    }
    else
    {
        handler_console->printf(": Unknown Exception\n");
    }

    handler_console->printf("RIP: 0x%016lx, CS=0x%016lx, RFLAGS=0x%016lx\n", regs->rip, regs->cs, regs->rflags);
    handler_console->printf(
        "RSP: 0x%016lx, SS=0x%016lx, ERR=0x%lx, INT=%lu\n", regs->rsp, regs->ss, regs->err_code, regs->int_no);
    while (1)
    {
        asm volatile("hlt");
    }
}

static uint64_t timer_ticks = 0;

// ─── IRQ ハンドラ ─────────────────────
extern "C" void irq0_handler()
{
    // IRQ0: タイマ
    timer_ticks++;
    if (timer_ticks % 100 == 0)
    {
#ifdef TIMER_TEST
        handler_console->set_color(Color::DarkGrey, Color::Black);
        handler_console->printf("[TIMER] %lu sec\n", timer_ticks / 100);
        handler_console->set_color(Color::LightGrey, Color::Black);
#endif
    }
    pic::send_eoi(0);
    // 注意: 割り込みゲートでは IF=0 で入るため、ここから yield()/switch_context
    // を呼ぶと IF が落ちたまま別プロセスへ移り、以降の割り込みが止まる。
    // スケジューリングは各スレッドが自分で yield() を呼ぶ協調型に任せる。
}

extern "C" void irq_handler(uint64_t irq_no)
{
    if (irq_no == 1)
    {
        keyboard::handle_irq();
    }
    pic::send_eoi(static_cast<uint8_t>(irq_no));
}

} // namespace exception
