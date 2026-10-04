#include <cstdint>
#include "tests.hpp"
#include "process.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "systemcall.hpp"

// .user セクションの範囲 (kernel.ld が定義する)
extern "C" char __user_start[];
extern "C" char __user_end[];

namespace tests
{
namespace
{

// ユーザースタックを置く仮想アドレス (PML4[0] 配下 = プロセスごとの領域)。
// fork したとき copy_user_pages() が複製するのはこちら側だけなので、
// 親子で別々になることを確かめるための変数はここ (スタック上) に置く。
constexpr PageVirtualAddress kUserStack{0x600000};

// ─── リング3で動く部分 ───────────────────────────────────────────
// .rodata を参照しないよう、文字列はすべてスタック上に作る
// (カーネルの .rodata はユーザーにマップしていないので、触ると #PF になる)。

[[gnu::section(".user")]] long user_syscall(long num, long a1, long a2, long a3)
{
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

[[gnu::section(".user")]] void user_write(const char *text, long length)
{
    user_syscall(SyscallNo::kWrite, 1 /* stdout */, reinterpret_cast<long>(text), length);
}

// fork して、親子がそれぞれ自分のユーザーメモリを持っていることを示す。
[[gnu::section(".user")]] void user_fork_program()
{
    char mark[8] = {'S', 'H', 'A', 'R', 'E', 'D', '\n', 0}; // ユーザースタック上

    const long pid = user_syscall(SyscallNo::kFork, 0, 0, 0);
    if (pid == 0)
    {
        // 子: 自分のコピーだけを書き換えて表示し、7 を返して終了する
        mark[0] = 'C';
        user_write(mark, 7); // "CHARED"
        user_syscall(SyscallNo::kExit, 7, 0, 0);
    }

    // 親: 子の終了を待ってから、自分の mark を表示する。
    // 子の書き換えが見えていなければ (= "SHARED" のままなら) アドレス空間が分かれている。
    // status は int (カーネルの process::wait が書き込むのも int)。
    // long で受けると上位 4 バイトが初期値のまま残って比較に失敗する。
    int status        = -1;
    const long waited = user_syscall(SyscallNo::kWait, reinterpret_cast<long>(&status), 0, 0);
    user_write(mark, 7); // "SHARED" のはず

    char result[10] = {'F', 'O', 'R', 'K', '-', 'N', 'G', '\n', 0, 0};
    if (pid > 0 && waited == pid && status == 7)
    {
        result[5] = 'O';
        result[6] = 'K';
    }
    user_write(result, 8);

    user_syscall(SyscallNo::kExit, 0, 0, 0);
}

// ─── カーネル側 (プロセスのエントリ) ─────────────────────────────

IConsole *fork_user_console = nullptr;

void fail(const char *message)
{
    fork_user_console->set_color(Color::LightRed, Color::Black);
    fork_user_console->puts("[UFORK] ");
    fork_user_console->set_color(Color::LightGrey, Color::Black);
    fork_user_console->puts(message);
}

// このプロセスのアドレス空間に .user とユーザースタックをマップし、リング3へ降りる。
void user_fork_entry()
{
    Process *self = process::current_process();
    if (self == nullptr || !self->pml4)
    {
        fail("no process context\n");
        return;
    }

    // リング3で実行するコード。書き込みは許可しない。
    for (uint64_t va = reinterpret_cast<uint64_t>(__user_start) & PAGE_MASK;
         va < reinterpret_cast<uint64_t>(__user_end);
         va += PAGE_SIZE)
    {
        const PhysicalAddress phys = vmm::vmm_ptr->virtual_to_physical(PageVirtualAddress{va});
        if (!phys)
        {
            fail("failed to resolve .user physical address\n");
            return;
        }
        vmm::vmm_ptr->map_page_in(self->pml4, PageVirtualAddress{va}, phys,
                                  vmm::PageFlag::Present | vmm::PageFlag::User);
    }

    // ユーザースタック (fork で複製される側)
    const PhysicalAddress stack_phys = pmm::pmm_ptr->allocate();
    if (!stack_phys)
    {
        fail("failed to allocate user stack\n");
        return;
    }
    vmm::vmm_ptr->map_page_in(self->pml4, kUserStack, stack_phys,
                              vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User);

    // ここから戻らない。終了は exit システムコール (process::exit) 経由。
    usermode::enter(reinterpret_cast<uint64_t>(&user_fork_program), kUserStack.address + PAGE_SIZE - 16);
}

} // namespace

void fork_from_ring3(IConsole *console)
{
    fork_user_console = console;

    if (process::create_process(user_fork_entry, "ufork") == nullptr)
    {
        fail("create_process failed\n");
        return;
    }

    // プロセスが終わる (親子とも exit する) まで戻らない
    process::yield();
}

} // namespace tests
