#include "types.hpp"

namespace exception
{
    typedef struct __attribute__((packed)) {
        uint64_t r15,r14,r13,r12,r11,r10,r9,r8;
        uint64_t rbp,rdi,rsi,rdx,rcx,rbx,rax;
        uint64_t int_no, err_code;
        uint64_t rip, cs, rflags, rsp, ss;
    } register_state_t;
    
    // isr.asm から呼ばれるため C リンケージ (名前マングリング無効)
    extern "C" void isr_common_handler(register_state_t *regs);
}

// IRQ ハンドラ (isr.asm から呼ばれる)。C リンケージ。
extern "C" void irq0_handler();
extern "C" void irq_handler(uint64_t irq_no);
