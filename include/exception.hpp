#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP
#include <cstdint>
#include "console.hpp"

namespace exception
{

struct [[gnu::packed]] register_state_t
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// 例外・割り込みハンドラの出力先を登録する。
// ハンドラはアセンブリのスタブから呼ばれるので、引数で出力先を渡すことはできない。
// そのため登録した出力先をモジュール内に保持し、ハンドラはそれを使う。
// IDT を有効にする (lidt) 前に呼ぶこと。nullptr を渡した場合はシリアルに出力する。
void set_console(IConsole *console);

// isr.asm から呼ばれるため C リンケージ (名前マングリング無効)
extern "C" void isr_common_handler(register_state_t *regs);
extern "C" void irq0_handler();
extern "C" void irq_handler(uint64_t irq_no);

} // namespace exception

#endif // EXCEPTION_HPP
