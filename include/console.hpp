#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdarg>

// 文字色。出力先が色を表現できる場合にだけ使われる (VGA テキストモードの 16 色)。
enum class Color : uint8_t
{
    Black        = 0,
    Blue         = 1,
    Green        = 2,
    Cyan         = 3,
    Red          = 4,
    Magenta      = 5,
    Brown        = 6,
    LightGrey    = 7,
    DarkGrey     = 8,
    LightBlue    = 9,
    LightGreen   = 10,
    LightCyan    = 11,
    LightRed     = 12,
    LightMagenta = 13,
    Yellow       = 14,
    White        = 15,
};

// 文字出力先の抽象。VGA / シリアルなど「文字を出せるもの」を差し替え可能にする。
//
// 派生クラスが実装しなければならないのは write() 1つだけ。putchar / puts / printf は
// すべてその上に組み立ててあるので、出力先を増やしても書式処理は再実装しない。
// set_color は色を表現できる出力先 (VGA) だけが上書きする。
// 1文字ずつではなくバイト列で受け渡すため、実装側でまとめて転送できる。
class IConsole
{
  public:
    virtual void write(const char *s, size_t n) = 0;
    virtual ~IConsole()                         = default;

    // 以降の出力の色を変える。既定では何もしない (シリアルなど色を持たない出力先)。
    virtual void set_color(Color, Color) { }

    void putchar(char c)
    {
        write(&c, 1);
    }
    void puts(const char *s)
    {
        write(s, __builtin_strlen(s));
    }

    // 自前の簡易 printf。対応する変換は %d %i %u %x %p %s %c %% で、
    // 幅指定 (%8u)、ゼロ埋め (%08x)、長さ修飾子 (h/hh/l/ll/z) に対応する。
    //
    // format 属性を付けてあるので、書式と引数の型が合っていなければ
    // コンパイル時に -Wformat が警告する。長さ修飾子は C と同じ意味で
    // 解釈するので、uint64_t を渡すなら %lu / %llu が必要 (%u では不可)。
    // (メンバ関数なので this が第1引数。書式は第2、可変長引数は第3から)
    void vprintf(const char *fmt, va_list ap);
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

// 出力を捨てる IConsole。ログを取りたくない場面や、呼び出し側が
// コンソールを持たない場面で nullptr の代わりに使う。
class NullConsole final : public IConsole
{
  public:
    void write(const char *, size_t) override { }
};

// 実体は kernel_main が作り、このポインタに設定する。
// グローバル変数として持つと .init_array の並び (= リンク順) で初期化されるため、
// 他のグローバルとの初期化順序が「たまたま動いている」状態になりやすい。
// main で明示的に構築することで、初期化順序の問題を避ける。
inline NullConsole *null_console = nullptr;
