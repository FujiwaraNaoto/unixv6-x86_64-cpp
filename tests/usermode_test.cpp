#include <cstdint>
#include "tests.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

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
    // ユーザープログラムのコードページを User 許可で貼り直す
    const uint64_t code_page = reinterpret_cast<uint64_t>(&user_program) & PAGE_MASK;
    const auto code_phys     = vmm::vmm_ptr->virtual_to_physical(code_page);
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

    // ユーザースタックを確保して User許可でマップ
    const auto ustack_allocated = pmm::pmm_ptr->allocate();
    if (!ustack_allocated)
    {
        console->set_color(Color::LightRed, Color::Black);
        console->puts("[USER] failed to allocate user stack\n");
        console->set_color(Color::LightGrey, Color::Black);
        hang();
    }
    const vmm::PhysicalAddress ustack_phys{*ustack_allocated};
    constexpr uint64_t ustack_virt = 0x600000;
    vmm::vmm_ptr->map_page(ustack_virt,
                           ustack_phys,
                           vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User);

    // リング3へ遷移 (16バイト境界に揃える)。ここから戻らない。
    usermode::enter(reinterpret_cast<uint64_t>(&user_program), ustack_virt + PAGE_SIZE - 16);
}

} // namespace tests
