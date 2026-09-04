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

// 幅指定を適用して文字列を出力する。
// left が真なら右側を空白で埋め、偽なら左側を pad で埋める。
void write_padded(IConsole &out, const char *text, int length, int width, bool left, char pad)
{
    char padding[64];
    int fill = width - length;
    if (fill > static_cast<int>(sizeof(padding)))
    {
        fill = sizeof(padding);
    }
    for (int i = 0; i < fill; i++)
    {
        padding[i] = left ? ' ' : pad; // 左詰めの余白は常に空白 ("%-08d" のゼロ埋めは無効)
    }

    if (!left && fill > 0)
    {
        out.write(padding, static_cast<size_t>(fill));
    }
    out.write(text, static_cast<size_t>(length));
    if (left && fill > 0)
    {
        out.write(padding, static_cast<size_t>(fill));
    }
}

// 数字を逆順に組み立ててから write() で一括出力する。
// 1文字ずつ write() を呼ぶより転送回数が減る。
void print_number(IConsole &out, unsigned long long value, unsigned base, int width, bool left, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char reversed[32];
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

    char buffer[sizeof(reversed)];
    int length = 0;
    while (count-- > 0)
    {
        buffer[length++] = reversed[count];
    }
    write_padded(out, buffer, length, width, left, pad);
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

        // ─── フラグ ───
        // 対応するのは '-' (左詰め) と '0' (ゼロ埋め)。
        // '+' / ' ' / '#' は解釈しないが、読み飛ばして出力に漏らさない。
        bool left = false;
        char pad  = ' ';
        for (bool parsing = true; parsing;)
        {
            switch (*fmt)
            {
                case '-':
                    left = true;
                    fmt++;
                    break;
                case '0':
                    pad = '0';
                    fmt++;
                    break;
                case '+':
                case ' ':
                case '#':
                    fmt++;
                    break;
                default:
                    parsing = false;
                    break;
            }
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
                print_number(*this, static_cast<unsigned long long>(value), 10, width, left, pad);
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
                print_number(*this, value, (*fmt == 'x') ? 16 : 10, width, left, pad);
                break;
            }
            case 'p':
                write("0x", 2);
                print_number(*this, reinterpret_cast<unsigned long long>(va_arg(ap, void *)), 16, 16, false, '0');
                break;
            case 's':
            {
                const char *text = va_arg(ap, const char *);
                if (text == nullptr)
                {
                    text = "(null)";
                }
                write_padded(*this, text, static_cast<int>(__builtin_strlen(text)), width, left, ' ');
                break;
            }
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
