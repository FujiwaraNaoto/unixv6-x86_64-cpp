#include "command.hpp"
#include "user_syscall.hpp"

[[gnu::section(".user")]] void put(const char *s, size_t len)
{
    user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(s), len);
}

[[gnu::section(".user")]] void put_char(char c)
{
    put(&c, 1);
}

[[gnu::section(".user")]] long read_line(char *line, size_t capacity)
{
    size_t length = 0;
    char c      = 0;
    while (user_syscall(SyscallNo::kRead, 0, reinterpret_cast<long>(&c), 1) == 1)
    {
        if (c == '\b')
        {
            if (length > 0)
            {
                length--;
                put_char(c);
            }
            continue;
        }
        put_char(c);
        if (c == '\n')
            break;
        if (length < capacity - 1)
            line[length++] = c;
    }
    line[length] = '\0';
    return length;
}

[[gnu::section(".user")]] int split(char *line, char **argv, int max_args)
{
    int argc = 0;
    while (*line != '\0' && argc < max_args)
    {
        while (*line == ' ' || *line == '\t')
            *line++ = '\0';
        if (*line == '\0')
            break;
        argv[argc++] = line;
        while (*line != '\0' && *line != ' ' && *line != '\t')
            line++;
    }
    argv[argc] = nullptr;
    return argc;
}

[[gnu::section(".user")]] size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

// カーネルの strcmp は .text (スーパーバイザページ) にあり ring3 から呼べないので自前で持つ
[[gnu::section(".user")]] bool strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
