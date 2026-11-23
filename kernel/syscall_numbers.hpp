#ifndef KERNEL_SYSCALL_NUMBERS_HPP
#define KERNEL_SYSCALL_NUMBERS_HPP

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

#endif // KERNEL_SYSCALL_NUMBERS_HPP
