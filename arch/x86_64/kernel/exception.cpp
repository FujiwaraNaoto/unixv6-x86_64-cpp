#include "exception.hpp"
#include "serial.hpp"
#include "pic.hpp"
#include "process.hpp"
#include "keyboard.hpp"
#include "console.hpp"
#include "enum_util.hpp"
#include "asm/irqflags.hpp"


// page fault 例外発生時に CR2 レジスタに格納される、アクセスしようとした仮想アドレスを返す
extern "C" uint64_t read_cr2();

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

enum class ExceptionType : uint8_t
{
    DivideByZero = 0,
    Debug,
    NonMaskableInterrupt,
    Breakpoint,
    Overflow,
    BoundRangeExceeded,
    InvalidOpcode,
    DeviceNotAvailable,
    DoubleFault,
    CoprocessorSegmentOverrun,
    InvalidTSS,
    SegmentNotPresent,
    StackSegmentFault,
    GeneralProtectionFault,
    PageFault,
    Reserved,
    FloatingPointException,
    AlignmentCheck,
    MachineCheck,
    SIMDException,
    VirtualizationException,
    ControlProtectionException,
    Sentinel, // 番兵。値が例外の数になる。isr.asmも参照
};

namespace
{
const char *exception_name(ExceptionType type)
{
    switch (type)
    {
        case ExceptionType::DivideByZero:
            return "Division By Zero";
        case ExceptionType::Debug:
            return "Debug";
        case ExceptionType::NonMaskableInterrupt:
            return "Non Maskable Interrupt";
        case ExceptionType::Breakpoint:
            return "Breakpoint";
        case ExceptionType::Overflow:
            return "Into Detected Overflow";
        case ExceptionType::BoundRangeExceeded:
            return "Bound Range Exceeded";
        case ExceptionType::InvalidOpcode:
            return "Invalid Opcode";
        case ExceptionType::DeviceNotAvailable:
            return "Device Not Available";
        case ExceptionType::DoubleFault:
            return "Double Fault";
        case ExceptionType::CoprocessorSegmentOverrun:
            return "Coprocessor Segment Overrun";
        case ExceptionType::InvalidTSS:
            return "Invalid TSS";
        case ExceptionType::SegmentNotPresent:
            return "Segment Not Present";
        case ExceptionType::StackSegmentFault:
            return "Stack-Segment Fault";
        case ExceptionType::GeneralProtectionFault:
            return "General Protection Fault";
        case ExceptionType::PageFault:
            return "Page Fault";
        case ExceptionType::Reserved:
            return "Reserved";
        case ExceptionType::FloatingPointException:
            return "x87 Floating-Point Exception";
        case ExceptionType::AlignmentCheck:
            return "Alignment Check";
        case ExceptionType::MachineCheck:
            return "Machine Check";
        case ExceptionType::SIMDException:
            return "SIMD Floating-Point Exception";
        case ExceptionType::VirtualizationException:
            return "Virtualization Exception";
        case ExceptionType::ControlProtectionException:
            return "Control Protection Exception";
        case ExceptionType::Sentinel:
            break;
    }
    return "Unknown Exception";
}
} // namespace


void isr_common_handler(register_state_t *regs)
{
    handler_console->set_color(Color::White, Color::Red);
    handler_console->printf("**Exception **");
    const char *name = "Unknown Exception";
    if (regs->int_no < to_underlying(ExceptionType::Sentinel))
    {
        name = exception_name(static_cast<ExceptionType>(regs->int_no));
    }
    handler_console->printf(": %s\n", name);

    handler_console->printf("RIP: 0x%016lx, CS=0x%016lx, RFLAGS=0x%016lx\n", regs->rip, regs->cs, regs->rflags);
    handler_console->printf(
        "RSP: 0x%016lx, SS=0x%016lx, ERR=0x%lx, INT=%lu\n", regs->rsp, regs->ss, regs->err_code, regs->int_no);

    if (regs->int_no == static_cast<uint8_t>(ExceptionType::PageFault))
    {
        handler_console->printf("Page Fault Address: 0x%016lx\n", read_cr2());
    }

    while (1)
    {
        cpu_idle();
    }
}

namespace
{
uint64_t timer_ticks = 0;
} // namespace

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

extern "C" void irq_handler(uint64_t irq_number)
{
    if (irq_number == 1)
    {
        keyboard::handle_irq();
    }
    pic::send_eoi(static_cast<uint8_t>(irq_number));
}

} // namespace exception
