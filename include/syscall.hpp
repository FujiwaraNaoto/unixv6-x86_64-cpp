#pragma once
#include <cstdint>

// Linux x86-64 のシステムコール番号と合わせる
namespace SyscallNo
{
constexpr int kRead  = 0;
constexpr int kWrite = 1;
constexpr int kExit  = 60;
constexpr int kOpen  = 2;
constexpr int kClose = 3;
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



constexpr int O_RDONLY = 0x000;
constexpr int O_WRONLY = 0x001;
constexpr int O_RDWR   = 0x002;
constexpr int O_CREATE = 0x040;
constexpr int O_TRUNC  = 0x200;


namespace Syscall
{
// MSR を設定して syscall/sysret を有効化
void init();

// ユーザー空間からの入口。raw な uint64_t 引数を下の型付き実装へ振り分ける。
extern "C" int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

// ─── システムコールの実体 ───────────────────────────────────────
// syscall 命令を経由せずカーネル内から直接呼べるように、型付きで公開する。
// (kernel_main からのテストや、将来カーネル側でファイルを開く処理で使う)
//
// fd はプロセスごとの表に対する添字。プロセス文脈が無い場合 (kernel_main から
// 直接呼んだ場合) は、カーネル用の表が代わりに使われる。
//
// path を開く。O_CREATE 付きなら存在しないときに作る。失敗時は -1。
int sys_open(const char *path, int flags);
// fd を閉じる。成功で 0、失敗で -1。
int sys_close(int fd);
// fd から最大 n バイト読む。読めたバイト数、失敗で -1。
int sys_read(int fd, void *buffer, uint32_t n);
// fd へ最大 n バイト書く。書けたバイト数、失敗で -1。
int sys_write(int fd, const void *buffer, uint32_t n);
} // namespace Syscall
