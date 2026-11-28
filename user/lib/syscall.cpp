#include "syscall.hpp"
#include "syscall_numbers.hpp"
#include "user_syscall.hpp"
#include <cstddef>

// TODO: ユーザELFを別のファイルとして作りローダで読むようにするとELF全体をUserでマップでき
// 属性[[gnu::section(".user")]]は必要なくなる
[[gnu::section(".user")]] std::size_t read(int fd, void *buffer, std::size_t count)
{
    return user_syscall(SyscallNo::kRead, fd, reinterpret_cast<long>(buffer), static_cast<long>(count));
}

[[gnu::section(".user")]] std::size_t write(int fd, const void *buffer, std::size_t count)
{
    return user_syscall(SyscallNo::kWrite, fd, reinterpret_cast<long>(buffer), static_cast<long>(count));
}

[[gnu::section(".user")]] int close(int fd)
{
    return user_syscall(SyscallNo::kClose, fd, 0, 0);
}

[[gnu::section(".user")]] int fork()
{
    return user_syscall(SyscallNo::kFork, 0, 0, 0);
}

[[gnu::section(".user")]] int wait(int *status)
{
    return user_syscall(SyscallNo::kWait, reinterpret_cast<long>(status), 0, 0);
}

// libc と同名なので include-cleaner が <cstdlib> を要求し、引数まで誤検出する
// NOLINTBEGIN(misc-include-cleaner)
[[gnu::section(".user")]] void exit(int code)
{
    user_syscall(SyscallNo::kExit, code, 0, 0);
    while (1) // exit は戻らないが、noreturn を守るため
        ;
}
// NOLINTEND(misc-include-cleaner)
