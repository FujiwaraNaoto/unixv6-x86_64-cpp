#ifndef ASM_IRQFLAGS_HPP
#define ASM_IRQFLAGS_HPP
// 割り込みを禁止する
inline void irq_disable()
{
    asm volatile("cli");
}

// 割り込みを許可する
inline void irq_enable()
{
    asm volatile("sti");
}

// 次の割り込みが来るまで CPU を止める (hlt)。IF は変えない。
// IF=0 のまま呼ぶと NMI 以外では起きないので、止めっぱなしにしたいときに使う。
inline void cpu_idle()
{
    asm volatile("hlt");
}

// 割り込みを許可して、次の割り込みが来るまで待つ
// sti の効果は次の 1 命令の後から有効になるので、sti と hlt の間に割り込みが入らない。
// 「条件を確認 → 寝る」の間に来た割り込みで hlt から起きられる (lost wakeup を防ぐ)。
// 2 つに分けて irq_enable(); cpu_idle(); と書くと、その保証が無くなる。
inline void irq_enable_and_wait()
{
    asm volatile("sti; hlt");
}
#endif // ASM_IRQFLAGS_HPP
