#include <cstdint>
#include "tests.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "gdt.hpp"
#include "process.hpp"
#include "user_syscall.hpp"

// .user セクションの範囲 (kernel.ld が定義する)。この中だけを User 許可でマップする。
extern "C" char __user_start[];
extern "C" char __user_end[];

namespace tests
{
namespace
{

// リング3で実行されるユーザープログラム
// (カーネル内に置くが、User許可ページにマップして実行する)
[[gnu::section(".user")]] void user_program()
{
    const char msg[] = "Hello from ring 3!\n";
    user_syscall(SyscallNo::kWrite, 1 /* stdout */, reinterpret_cast<long>(msg) /* buf */, 19 /* len */);
    // syscall をまたいで callee-saved レジスタ (rbx / r12-r15) が保たれるかを確かめる。
    // 入口がカーネルスタックへ移り、TrapFrame に積んで戻せていないとここで崩れる。
    //
    // アセンブリは「決まった値を入れる → syscall → 結果をメモリへ書き出す」だけを行い、
    // 値の比較は下の C++ でやる。1 つの asm 文に収めているのは、文を分けると syscall の
    // 前後でコンパイラがこれらのレジスタを自由に使ってしまうため。
    constexpr uint64_t kRbx = 0x1111111111111111;
    constexpr uint64_t kR12 = 0x2222222222222222;
    constexpr uint64_t kR13 = 0x3333333333333333;
    constexpr uint64_t kR14 = 0x4444444444444444;
    constexpr uint64_t kR15 = 0x5555555555555555;

    const char check[] = "regs\n";
    uint64_t after[5]  = {}; // syscall から戻った直後の rbx, r12, r13, r14, r15

    asm volatile(R"(
        .intel_syntax noprefix
        mov rbx, %p[in_rbx]
        mov r12, %p[in_r12]
        mov r13, %p[in_r13]
        mov r14, %p[in_r14]
        mov r15, %p[in_r15]

        mov rax, 1              # write (中身はどうでもよいので短く)
        mov rdi, 1              # stdout
        mov rsi, %[buf]
        mov rdx, 5
        syscall

        mov [%[out] + 0x00], rbx
        mov [%[out] + 0x08], r12
        mov [%[out] + 0x10], r13
        mov [%[out] + 0x18], r14
        mov [%[out] + 0x20], r15
        .att_syntax prefix
    )"
                 :
                 : [out] "r"(after), [buf] "r"(check), [in_rbx] "i"(kRbx), [in_r12] "i"(kR12),
                   [in_r13] "i"(kR13), [in_r14] "i"(kR14), [in_r15] "i"(kR15)
                 : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r11", "r12", "r13", "r14", "r15", "memory");

    const bool regs_kept =
        after[0] == kRbx && after[1] == kR12 && after[2] == kR13 && after[3] == kR14 && after[4] == kR15;

    const char ok[]    = "REG-OK\n";
    const char ng[]    = "REG-NG\n";
    const char *result = regs_kept ? ok : ng;
    user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(result), 7);
    user_syscall(SyscallNo::kExit, 0, 0, 0);

    // 念のため
    while (1)
    {
        asm volatile("hlt");
    }
}

[[noreturn]] void hang()
{
    while (1)
    {
        asm volatile("hlt");
    }
}

} // namespace

// NOTE: syscall ハンドラは sysret でリング3へ戻るため、カーネル(リング0)から
//       直接 syscall を撃つことはできない (sysret が CPL=3 を強制し、
//       カーネルコードページに User 権限がないため #PF→#DF→トリプルフォルト
//       になる)。よって syscall のテストはリング3に降りてから行う。
//
//   // カーネルから syscall 命令を直接テストしようとした場合 (動かない):
//   const char msg[] = "Hello from syscall!\n";
//   uint64_t ret;
//   asm volatile(".intel_syntax noprefix\n"
//                "mov rax, 1\n"  // RAX = 1 (write)
//                "mov rdi, 1\n"  // RDI = 1 (stdout)
//                "mov rsi, %1\n" // RSI = buf
//                "mov rdx, %2\n" // RDX = len
//                "syscall\n"
//                "mov %0, rax\n" // 戻り値
//                ".att_syntax prefix\n"
//                : "=r"(ret)
//                : "r"(msg), "r"((uint64_t)(sizeof(msg) - 1))
//                : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
void usermode_ring3(IConsole *console)
{
    // .user セクション全体を User 許可で貼り直す (1 ページに収まらなくなっても動くように)
    for (uint64_t va = reinterpret_cast<uint64_t>(__user_start) & PAGE_MASK;
         va < reinterpret_cast<uint64_t>(__user_end);
         va += PAGE_SIZE)
    {
        const PageVirtualAddress code_page{va};
        const auto code_phys = vmm::vmm_ptr->virtual_to_physical(code_page);
        if (!code_phys)
        {
            console->set_color(Color::LightRed, Color::Black);
            console->puts("[USER] failed to resolve user_program physical address\n");
            console->set_color(Color::LightGrey, Color::Black);
            hang();
        }
        vmm::vmm_ptr->map_page(code_page,
                               code_phys,
                               vmm::PageFlag::User | vmm::PageFlag::Present | vmm::PageFlag::Writable);
    }

    // ユーザースタックを確保して User許可でマップ
    const PhysicalAddress ustack_phys = pmm::pmm_ptr->allocate();
    if (!ustack_phys)
    {
        console->set_color(Color::LightRed, Color::Black);
        console->puts("[USER] failed to allocate user stack\n");
        console->set_color(Color::LightGrey, Color::Black);
        hang();
    }
    constexpr PageVirtualAddress ustack_virt{0x600000};
    vmm::vmm_ptr->map_page(ustack_virt,
                           ustack_phys,
                           vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User);
    // syscall と割り込みで降りてくる先のカーネルスタックを用意する。
    // ここはプロセスではなくカーネル初期文脈から降りるので、自分で設定しておく
    // (プロセスから降りる場合は schedule() が設定する)。
    static uint8_t user_kernel_stack[KERNEL_STACK_SIZE];
    gdt::set_kernel_stack(reinterpret_cast<uint64_t>(user_kernel_stack) + sizeof(user_kernel_stack));


    // リング3へ遷移 (16バイト境界に揃える)。ここから戻らない。
    usermode::enter(reinterpret_cast<uint64_t>(&user_program), ustack_virt.address + PAGE_SIZE - 16);
}

} // namespace tests
