#ifndef USER_LIB_SYSCALL_HPP
#define USER_LIB_SYSCALL_HPP
#include <cstddef>

// ユーザープログラム用のシステムコールのラッパー。

std::size_t read(int fd, void *buffer, std::size_t count);
std::size_t write(int fd, const void *buffer, std::size_t count);
int close(int fd);
int fork();
int wait(int *status);
[[noreturn]] void exit(int code);

#endif // USER_LIB_SYSCALL_HPP
