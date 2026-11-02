#include <cstdint>
#include "tests.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "process.hpp"
#include "user_syscall.hpp"

namespace tests
{
namespace
{

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
void echo_entry(void *)
{
    Process *self = process::current_process();

    if (!usermode::map_user_space(self->pml4))
    {
        fail("failed to map user space\n");
        return;
    }

    usermode::enter(reinterpret_cast<uint64_t>(&user_echo_program), usermode::kUserStackTop);
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
