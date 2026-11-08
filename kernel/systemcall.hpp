#ifndef KERNEL_SYSTEMCALL_HPP
#define KERNEL_SYSTEMCALL_HPP
#include <cstdint>
#include "console.hpp"
#include "asm/msr.hpp"

// Linux x86-64 のシステムコール番号と合わせる
namespace SyscallNo
{
constexpr int kRead  = 0;
constexpr int kWrite = 1;
constexpr int kClose = 3;
constexpr int kFork  = 57;
constexpr int kExit  = 60;
constexpr int kWait  = 61;
} // namespace SyscallNo

namespace SystemCall
{
// MSR を設定して syscall/sysret を有効化する。
//
// console はシステムコールの出力先 (write の出力、exit / 不明な番号の通知、
// 初期化時のメッセージ)。syscall_dispatch はアセンブリの syscall_entry から
// 呼ばれるので引数で出力先を渡せない。そのためここで受け取ってモジュール内に保持する。
// nullptr を渡した場合はシリアルに出力する (write の出力はユーザプログラムの結果
// そのものなので、黙って捨てない)。
void init(IConsole *console);

extern "C" long syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
} // namespace SystemCall

#endif // KERNEL_SYSTEMCALL_HPP
