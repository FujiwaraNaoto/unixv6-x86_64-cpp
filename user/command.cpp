#include "command.hpp"
#include "lib/syscall.hpp"
#include <cstddef>

[[gnu::section(".user")]] void put(const char *s, std::size_t len)
{
    write(1, s, len);
}

[[gnu::section(".user")]] void put_char(char c)
{
    put(&c, 1);
}

[[gnu::section(".user")]] long read_line(char *line, std::size_t capacity)
{
    std::size_t length = 0;
    char c             = 0;
    while (read(0, &c, 1) == 1)
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

// libc と同名なので include-cleaner が <cstring> を要求し、引数やローカル変数まで誤検出する
// NOLINTBEGIN(misc-include-cleaner)
[[gnu::section(".user")]] std::size_t strlen(const char *s)
{
    std::size_t n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

// カーネルの strcmp は .text (スーパーバイザページ) にあり ring3 から呼べないので自前で持つ
[[gnu::section(".user")]] int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
// NOLINTEND(misc-include-cleaner)
