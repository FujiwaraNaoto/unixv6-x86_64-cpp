#include "user_syscall.hpp"

namespace tests
{

// リング3で動く。カーネルに降りる唯一の入口。
//
// 呼び出し規約は Linux 互換 (syscall/syscall_entry.asm を参照):
//   RAX = 番号, RDI/RSI/RDX = 引数, 戻り値は RAX
// syscall 命令は RCX に戻り先 RIP、R11 に RFLAGS を入れて壊すので、clobber に並べる。
//
// .user セクションに置くことで、ユーザー許可ページとしてマップできる
// (カーネルの .text は User=0 なので、リング3から実行すると #PF になる)。
[[gnu::section(".user")]] long user_syscall(long num, long a1, long a2, long a3)
{
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

} // namespace tests
