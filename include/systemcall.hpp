#pragma once
#include <cstdint>
#include "console.hpp"

// Linux x86-64 のシステムコール番号と合わせる
namespace SyscallNo
{
constexpr int kRead  = 0;
constexpr int kWrite = 1;
constexpr int kExit  = 60;
} // namespace SyscallNo

// MSR 番号(識別子)
// see https://wiki.osdev.org/SYSENTER#AMD:_SYSCALL/SYSRET
namespace MSR
{
constexpr uint32_t EFER  = 0xC0000080; // SCE (System Call Enable) bit, which enables syscall/sysret
constexpr uint32_t STAR  = 0xC0000081; // Segment selectors for syscall/sysret
constexpr uint32_t LSTAR = 0xC0000082; // Long mode syscall target. Address of the syscall entry point.
constexpr uint32_t FMASK =
    0xC0000084; // Flags mask for syscall. A bitmask of RFLAGS bits to clear when syscall is executed.
} // namespace MSR


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
