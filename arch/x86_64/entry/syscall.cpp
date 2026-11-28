#include <cstdint>
#include "asm/syscall.hpp"
#include "asm/msr.hpp"
#include "asm/trapframe.hpp"
#include "console.hpp"
#include "systemcall.hpp"
#include "process.hpp"
#include "gdt.hpp"

extern "C" void handle_syscall(TrapFrame *tf)
{
    Process *proc = process::current_process();
    if (proc)
    {
        proc->trap_frame = tf;
    }

    // syscall_entry.asm で積まれた TrapFrame のレジスタを引数にして syscall_dispatch を呼ぶ
    long ret = SystemCall::syscall_dispatch(tf->rax, tf->rdi, tf->rsi, tf->rdx, tf->r10, tf->r8);

    // 戻り値を TrapFrame の rax に書き戻す
    tf->rax = static_cast<uint64_t>(ret);
}

void arch_syscall_init(IConsole *console)
{
    // 1. EFER.SCE (System Call Enable) を立てる
    uint64_t efer = rdmsr(MSR::EFER);
    efer |= 1; // SCE = bit0
    wrmsr(MSR::EFER, efer);

    // 2. STAR: セグメントセレクタを設定
    //    上位32bit [63:48]=ユーザー, [47:32]=カーネル
    //    syscall 時: CS=STAR[47:32],    SS=STAR[47:32]+8
    //    sysret 時: CS=STAR[63:48]+16, SS=STAR[63:48]+8 (どちらも RPL=3 が強制される)
    //
    //    CPU が「+8」「+16」で勝手に隣を拾う仕様なので、STAR に入れるのはセレクタそのもの
    //    ではなく「基準値」である点に注意。[63:48] の 0x10 はカーネルDataと同じ値になるが
    //    偶然の一致で、意味は「ユーザーDataの1つ前」。
    // NOTE: kSysretBase の値は GDT のレイアウトから導出される従属値なので、GDT の順序を変更すればこの値も変わる
    constexpr uint64_t kSyscallBase = gdt::SegmentSelector::kKernelCode;                // 0x08
    constexpr uint64_t kSysretBase  = (gdt::SegmentSelector::kUserData & ~0x03) - 0x08; // 0x10

    // 上の +8/+16 が実際に gdt.hpp のセレクタに着地することを保証する
    // (GDT の並びを崩したらリンク前に落ちる)
    static_assert(kSyscallBase + 0x08 == gdt::SegmentSelector::kKernelData,
                  "syscall は SS=CS+8 を要求する: Kernel Data は Kernel Code の直後でなければならない");
    static_assert(((kSysretBase + 0x08) | 0x03) == gdt::SegmentSelector::kUserData, "sysret は SS=base+8 を要求する");
    static_assert(((kSysretBase + 0x10) | 0x03) == gdt::SegmentSelector::kUserCode,
                  "sysret は CS=base+16 を要求する: User Code は User Data の直後でなければならない");

    uint64_t star = (kSyscallBase << 32)   // カーネルセグメント
                    | (kSysretBase << 48); // ユーザーセグメント
    wrmsr(MSR::STAR, star);

    // 3. LSTAR: syscall 時のジャンプ先
    wrmsr(MSR::LSTAR, reinterpret_cast<uint64_t>(syscall_entry));

    // 4. FMASK: syscall 時にクリアするRFLAGSビット (割り込みフラグIFを落とす)
    wrmsr(MSR::FMASK, 1 << 9); // IF = bit9

    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[SYS]  ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("syscall enabled  LSTAR=0x%x\n", static_cast<unsigned>(reinterpret_cast<uint64_t>(syscall_entry)));
}
