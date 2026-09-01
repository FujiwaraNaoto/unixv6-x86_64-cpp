#include "console.hpp"

namespace
{

// 長さ修飾子。C の printf と同じ意味で解釈する。
enum class Length
{
    None,     // int / unsigned int
    Char,     // hh (可変長引数では int に昇格済み)
    Short,    // h  (同上)
    Long,     // l
    LongLong, // ll
    Size,     // z
};

// 数字を逆順に組み立ててから write() で一括出力する。
// 1文字ずつ write() を呼ぶより転送回数が減る。
void print_number(IConsole &out, unsigned long long value, unsigned base, int width, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char reversed[64];
    int count = 0;

    if (value == 0)
    {
        reversed[count++] = '0';
    }
    else
    {
        while (value != 0 && count < static_cast<int>(sizeof(reversed)))
        {
            reversed[count++] = digits[value % base];
            value /= base;
        }
    }
    while (count < width && count < static_cast<int>(sizeof(reversed)))
    {
        reversed[count++] = pad;
    }

    char buffer[sizeof(reversed)];
    int length = 0;
    while (count-- > 0)
    {
        buffer[length++] = reversed[count];
    }
    out.write(buffer, static_cast<size_t>(length));
}

} // namespace

void IConsole::vprintf(const char *fmt, va_list ap)
{
    // '%' 以外の連続した文字はまとめて write() する
    const char *chunk = fmt;

    for (; *fmt != '\0'; fmt++)
    {
        if (*fmt != '%')
        {
            continue;
        }
        if (fmt > chunk)
        {
            write(chunk, static_cast<size_t>(fmt - chunk));
        }

        fmt++;
        if (*fmt == '\0')
        {
            chunk = fmt;
            break; // 末尾の '%' は捨てる
        }

        // ─── ゼロ埋めフラグと幅 ───
        char pad = ' ';
        if (*fmt == '0')
        {
            pad = '0';
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        // ─── 長さ修飾子 ───
        Length length = Length::None;
        if (*fmt == 'h')
        {
            fmt++;
            length = Length::Short;
            if (*fmt == 'h')
            {
                fmt++;
                length = Length::Char;
            }
        }
        else if (*fmt == 'l')
        {
            fmt++;
            length = Length::Long;
            if (*fmt == 'l')
            {
                fmt++;
                length = Length::LongLong;
            }
        }
        else if (*fmt == 'z')
        {
            fmt++;
            length = Length::Size;
        }

        switch (*fmt)
        {
            case 'd':
            case 'i':
            {
                long long value = 0;
                switch (length)
                {
                    case Length::LongLong:
                        value = va_arg(ap, long long);
                        break;
                    case Length::Long:
                        value = va_arg(ap, long);
                        break;
                    case Length::Size:
                        value = static_cast<long long>(va_arg(ap, size_t));
                        break;
                    // char / short は可変長引数の既定昇格で int になっている
                    default:
                        value = va_arg(ap, int);
                        break;
                }
                if (value < 0)
                {
                    putchar('-');
                    value = -value;
                }
                print_number(*this, static_cast<unsigned long long>(value), 10, width, pad);
                break;
            }
            case 'u':
            case 'x':
            {
                unsigned long long value = 0;
                switch (length)
                {
                    case Length::LongLong:
                        value = va_arg(ap, unsigned long long);
                        break;
                    case Length::Long:
                        value = va_arg(ap, unsigned long);
                        break;
                    case Length::Size:
                        value = va_arg(ap, size_t);
                        break;
                    default:
                        value = va_arg(ap, unsigned int);
                        break;
                }
                print_number(*this, value, (*fmt == 'x') ? 16 : 10, width, pad);
                break;
            }
            case 'p':
                write("0x", 2);
                print_number(*this, reinterpret_cast<unsigned long long>(va_arg(ap, void *)), 16, 16, '0');
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
            default:
                break; // 未知の変換は捨てる
        }
        chunk = fmt + 1;
    }

    if (fmt > chunk)
    {
        write(chunk, static_cast<size_t>(fmt - chunk));
    }
}

void IConsole::printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
