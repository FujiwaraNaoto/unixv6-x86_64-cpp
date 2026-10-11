#ifndef TESTS_USER_SYSCALL_HPP
#define TESTS_USER_SYSCALL_HPP
#include "systemcall.hpp"

namespace tests
{

// リング3から syscall 命令を撃つ。実体は user_syscall.cpp の .user セクションにある。
//
// NOTE: この関数を呼ぶ側も [[gnu::section(".user")]] に置くこと。
//       .user セクション全体が User 許可でマップされるので、同じセクション内にいれば
//       リング3から call で飛べる (セクションは連続して配置されるため rel32 で届く)。
long user_syscall(long num, long a1, long a2, long a3);

} // namespace tests

#endif // TESTS_USER_SYSCALL_HPP
