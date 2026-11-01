#include <cstdint>
#include "tests.hpp"
#include "process.hpp"
#include "usermode.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "systemcall.hpp"
#include "user_syscall.hpp"

namespace tests
{
namespace
{

// ─── リング3で動く部分 ───────────────────────────────────────────
// .rodata を参照しないよう、文字列はすべてスタック上に作る
// (カーネルの .rodata はユーザーにマップしていないので、触ると #PF になる)。

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

    char result[10]    = {'F', 'O', 'R', 'K', '-', 'N', 'G', '\n', 0, 0};
    const bool fork_ok = pid > 0 && waited == pid && status == 7;
    if (fork_ok)
    {
        result[5] = 'O';
        result[6] = 'K';
    }
    user_write(result, 8);

    // 判定結果は exit の値で返す (0 = OK)。カーネル側のテストはプロセスの
    // exit_status からこれを読む。
    user_syscall(SyscallNo::kExit, fork_ok ? 0 : 1, 0, 0);
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
void user_fork_entry(void *)
{
    Process *self = process::current_process();
    if (self == nullptr || !self->pml4)
    {
        fail("no process context\n");
        return;
    }

    if (!usermode::map_user_space(self->pml4))
    {
        fail("failed to map user space\n");
        return;
    }

    // ここから戻らない。終了は exit システムコール (process::exit) 経由。
    usermode::enter(reinterpret_cast<uint64_t>(&user_fork_program), usermode::kUserStackTop);
}

} // namespace

bool fork_from_ring3(IConsole *console)
{
    fork_user_console = console;

    Process *proc = process::create_process(user_fork_entry, "ufork");
    if (proc == nullptr)
    {
        fail("create_process failed\n");
        return false;
    }

    // プロセスが終わる (親子とも exit する) まで戻らない
    process::yield();

    // このプロセスは誰にも wait されないので、終了後も Zombie として exit_status が残る。
    // リング3 側は自分の判定を exit の値で返している (0 = OK)。
    return proc->state == ProcessState::Zombie && proc->exit_status == 0;
}

} // namespace tests
