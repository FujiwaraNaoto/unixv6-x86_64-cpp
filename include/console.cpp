#include "console.hpp"

// stdarg: __builtin で直接実装
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)

void IConsole::print_uint(unsigned long long n, int base, int width, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[64];
    int i = 0;
    if (n == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (n)
        {
            tmp[i++] = digits[n % base];
            n /= base;
        }
    }
    while (i < width)
        tmp[i++] = pad;
    while (i--)
        putchar(tmp[i]);
}

void IConsole::printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            putchar(*fmt);
            continue;
        }
        fmt++;
        int width = 0;
        char pad  = ' ';
        if (*fmt == '0')
        {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        // 長さ修飾子 (l / ll / h / hh / z) は読み飛ばす。
        // 可変長引数は既に 64bit に昇格しており print_uint も unsigned long long を
        // 受けるので、修飾子の有無で取り出し方を変える必要はない。
        // 読み飛ばさないと switch のどのケースにも当たらず、"llx" のような文字列が
        // そのまま画面に出てしまう。
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z')
            fmt++;
        switch (*fmt)
        {
            case 'd':
            {
                auto v = va_arg(ap, long long);
                if (v < 0)
                {
                    putchar('-');
                    v = -v;
                }
                print_uint(static_cast<unsigned long long>(v), 10, width, pad);
                break;
            }
            case 'u':
                print_uint(va_arg(ap, unsigned long long), 10, width, pad);
                break;
            case 'x':
                print_uint(va_arg(ap, unsigned long long), 16, width, pad);
                break;
            case 'p':
                puts("0x");
                print_uint(reinterpret_cast<unsigned long long>(va_arg(ap, void *)), 16, 16, '0');
                break;
            case 's':
                puts(va_arg(ap, const char *));
                break;
            case 'c':
                putchar(static_cast<char>(va_arg(ap, int)));
                break;
            case '%':
                putchar('%');
                break;
        }
    }
    va_end(ap);
}
