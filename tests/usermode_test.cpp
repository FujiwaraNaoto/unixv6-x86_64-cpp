#include <cstdint>
#include "tests.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "gdt.hpp"
#include "process.hpp"

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
    asm volatile("mov $1, %%rax\n"  // write
                 "mov $1, %%rdi\n"  // stdout
                 "mov %0, %%rsi\n"  // buf
                 "mov $19, %%rdx\n" // len
                 "syscall\n"
                 :
                 : "r"(msg)
                 : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
    // syscall をまたいでレジスタが保たれるかを確かめる。
    // 入口がカーネルスタックへ移り、TrapFrame に積んで戻せていないと、ここで崩れる。
    // (callee-saved の rbx/r12-r15 は、カーネル側が壊しても誰も戻してくれない)
    const char check[] = "regs\n";
    uint64_t mismatch  = 0;
    asm volatile("mov $0x1111111111111111, %%rbx\n"
                 "mov $0x2222222222222222, %%r12\n"
                 "mov $0x3333333333333333, %%r13\n"
                 "mov $0x4444444444444444, %%r14\n"
                 "mov $0x5555555555555555, %%r15\n"
                 "mov $1, %%rax\n" // write (中身はどうでもよいので短く)
                 "mov $1, %%rdi\n"
                 "mov %1, %%rsi\n"
                 "mov $5, %%rdx\n"
                 "syscall\n"
                 "xor %0, %0\n"
                 "mov $0x1111111111111111, %%rcx\n cmp %%rcx, %%rbx\n je 1f\n or $1,  %0\n1:\n"
                 "mov $0x2222222222222222, %%rcx\n cmp %%rcx, %%r12\n je 2f\n or $2,  %0\n2:\n"
                 "mov $0x3333333333333333, %%rcx\n cmp %%rcx, %%r13\n je 3f\n or $4,  %0\n3:\n"
                 "mov $0x4444444444444444, %%rcx\n cmp %%rcx, %%r14\n je 4f\n or $8,  %0\n4:\n"
                 "mov $0x5555555555555555, %%rcx\n cmp %%rcx, %%r15\n je 5f\n or $16, %0\n5:\n"
                 : "=&r"(mismatch)
                 : "r"(check)
                 : "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r11", "r12", "r13", "r14", "r15", "memory");

    const char ok[]    = "REG-OK\n";
    const char ng[]    = "REG-NG\n";
    const char *result = (mismatch == 0) ? ok : ng;
    asm volatile("mov $1, %%rax\n"
                 "mov $1, %%rdi\n"
                 "mov %0, %%rsi\n"
                 "mov $7, %%rdx\n"
                 "syscall\n"
                 :
                 : "r"(result)
                 : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");


    asm volatile("mov $60, %%rax\n" // exit
                 "xor %%rdi, %%rdi\n"
                 "syscall\n"
                 :
                 :
                 : "rax", "rdi");
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
//   asm volatile("mov $1, %%rax\n" // RAX = 1 (write)
//                "mov $1, %%rdi\n" // RDI = 1 (stdout)
//                "mov %1, %%rsi\n" // RSI = buf
//                "mov %2, %%rdx\n" // RDX = len
//                "syscall\n"
//                "mov %%rax, %0\n" // 戻り値
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
