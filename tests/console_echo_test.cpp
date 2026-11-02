#include <cstdint>
#include "tests.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "process.hpp"
#include "user_syscall.hpp"

// .user セクションの範囲 (kernel.ld が定義する)。この中だけを User 許可でマップする。
extern "C" char __user_start[];
extern "C" char __user_end[];

namespace tests
{
namespace
{

constexpr PageVirtualAddress kUserStack{0x600000};

// リング3で実行するエコー。fd 0 から 1 文字読んで fd 1 に書く。
// 1 文字ずつ読むので、改行を待たずに打った文字がすぐ表示される。
[[gnu::section(".user")]] void user_echo_program()
{
    const char prompt[] = "> ";
    user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(prompt), 2);
    while (1)
    {
        char c         = 0;
        const long got = user_syscall(SyscallNo::kRead, 0, reinterpret_cast<long>(&c), 1);
        if (got != 1 || user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(&c), 1) != 1)
        {
            user_syscall(SyscallNo::kExit, 1, 0, 0);
        }
        if (c == '\n')
        {
            user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(prompt), 2);
        }
    }
}

IConsole *echo_console = nullptr;

void fail(const char *message)
{
    echo_console->set_color(Color::LightRed, Color::Black);
    echo_console->puts("[ECHO] ");
    echo_console->set_color(Color::LightGrey, Color::Black);
    echo_console->puts(message);
}

// このプロセスのアドレス空間に .user とユーザースタックをマップし、リング3へ降りる。ここから戻らない。
void echo_entry()
{
    Process *self = process::current_process();

    for (uint64_t va = reinterpret_cast<uint64_t>(__user_start) & PAGE_MASK;
         va < reinterpret_cast<uint64_t>(__user_end);
         va += PAGE_SIZE)
    {
        const PhysicalAddress phys = vmm::vmm_ptr->virtual_to_physical(PageVirtualAddress{va});
        if (!phys)
        {
            fail("failed to resolve user program physical address\n");
            return;
        }
        vmm::vmm_ptr->map_page_in(self->pml4,
                                  PageVirtualAddress{va},
                                  phys,
                                  vmm::PageFlag::Present | vmm::PageFlag::User);
    }

    const PhysicalAddress stack_phys = pmm::pmm_ptr->allocate();
    if (!stack_phys)
    {
        fail("failed to allocate user stack\n");
        return;
    }
    vmm::vmm_ptr->map_page_in(self->pml4,
                              kUserStack,
                              stack_phys,
                              vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User);

    usermode::enter(reinterpret_cast<uint64_t>(&user_echo_program), kUserStack.address + PAGE_SIZE - 16);
}

} // namespace

void console_echo(IConsole *console)
{
    echo_console = console;
    console->puts("[ECHO] type something (read fd 0 -> write fd 1 from ring 3):\n");

    if (process::create_process(echo_entry, "echo") == nullptr)
    {
        fail("create_process failed\n");
    }
    while (1)
    {
        process::yield();
        asm volatile("sti; hlt");
    }
}

} // namespace tests
