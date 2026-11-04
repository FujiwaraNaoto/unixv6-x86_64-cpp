#include "user_syscall.hpp"

[[gnu::section(".user")]] void init_main(){
    const char* prompt="> ";
    char line[1<<10];

    while(1){
        user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(prompt), 2);
        long length = 0;
        char c      = 0;

        while (user_syscall(SyscallNo::kRead, 0, reinterpret_cast<long>(&c), 1) == 1)
        {
            user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(&c), 1);
            line[length++] = c;
            if (c == '\n' || length == sizeof(line))
                break;
        }
        user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(line), length);
    }
}

