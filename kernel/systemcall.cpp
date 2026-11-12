#include "systemcall.hpp"
#include "serial.hpp"
#include "file.hpp"
#include "console.hpp"
#include "process.hpp"
#include "asm/irqflags.hpp"
#include "asm/syscall.hpp"
#include <cstdint>
#include <limits>


namespace
{
// システムコールの出力先。SystemCall::init() で登録される。
// syscall_dispatch は引数で受け取れないので、ここに保持する。
// syscall は init() で有効にするので、登録前に dispatch が走ることはない。
IConsole *syscall_console = nullptr;

// 現在のプロセスの fd 表から FileDescriptor を引く。範囲外・閉じている fd は nullptr。
FileDescriptor *lookup_file(uint64_t fd)
{
    Process *proc = process::current_process();
    if (proc == nullptr || fd >= proc->files.size())
        return nullptr;
    return proc->files[fd];
}

long sys_read(uint64_t fd, uint64_t buf, uint64_t len)
{
    FileDescriptor *file = lookup_file(fd);
    if (file == nullptr || len > std::numeric_limits<uint32_t>::max())
        return static_cast<long>(-1);
    return static_cast<long>(file->read(reinterpret_cast<void *>(buf), len)); // 失敗は -1 のまま返る
}

long sys_write(uint64_t fd, uint64_t buf, uint64_t len)
{
    FileDescriptor *file = lookup_file(fd);
    if (file == nullptr || len > std::numeric_limits<uint32_t>::max())
        return static_cast<long>(-1);
    return static_cast<long>(file->write(reinterpret_cast<const void *>(buf), len));
}

long sys_close(uint64_t fd)
{
    FileDescriptor *file = lookup_file(fd);
    if (file == nullptr)
        return static_cast<long>(-1);
    process::current_process()->files[fd] = nullptr;
    file->close();
    return 0;
}

long sys_exit(uint64_t code)
{
    syscall_console->set_color(Color::Yellow, Color::Black);
    syscall_console->printf("\n[SYS]  exit(%u) called\n", (unsigned)code);
    syscall_console->set_color(Color::LightGrey, Color::Black);

    if (process::current_process() != nullptr)
    {
        process::exit(static_cast<int>(code));
    }
    while (1)
        cpu_idle();
    return 0;
}

long sys_fork()
{
    return static_cast<long>(process::fork());
}

long sys_wait(uint64_t status_ptr)
{
    // ユーザー空間へは rax 経由の生の整数で返すので、ここで optional をほどく。
    auto pid = process::wait(reinterpret_cast<int *>(status_ptr));
    if (!pid)
    {
        return -1; // 子プロセスが居ない
    }
    return static_cast<long>(pid->value);
}
} // namespace

namespace SystemCall
{

extern "C" long syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t /*a4*/, uint64_t /*a5*/)
{
    switch (num)
    {
        case SyscallNo::kRead:
            return sys_read(a1, a2, a3);
        case SyscallNo::kWrite:
            return sys_write(a1, a2, a3);
        case SyscallNo::kClose:
            return sys_close(a1);
        case SyscallNo::kFork:
            return sys_fork();
        case SyscallNo::kWait:
            return sys_wait(a1);
        case SyscallNo::kExit:
            return sys_exit(a1);
        default:
            syscall_console->set_color(Color::LightRed, Color::Black);
            syscall_console->printf("[SYS]  unknown syscall %u\n", (unsigned)num);
            syscall_console->set_color(Color::LightGrey, Color::Black);
            return static_cast<long>(-1);
    }
}

void init(IConsole *console)
{
    // syscall を有効にする前に出力先を登録しておく
    syscall_console = (console != nullptr) ? console : serial::serial;
    FileSystem::file_set_console(syscall_console);

    arch_syscall_init(syscall_console);
}

} // namespace SystemCall
