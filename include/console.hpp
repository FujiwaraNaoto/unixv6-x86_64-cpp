#pragma once
#include <cstddef>
#include <cstdarg>

// 文字出力先の抽象。VGA / シリアルなど「文字を出せるもの」を差し替え可能にする。
//
// 派生クラスが実装するのは write() 1つだけ。putchar / puts / printf は
// すべてその上に組み立ててあるので、出力先を増やしても書式処理は再実装しない。
// 1文字ずつではなくバイト列で受け渡すため、実装側でまとめて転送できる。
class IConsole
{
  public:
    virtual void write(const char *s, size_t n) = 0;
    virtual ~IConsole()                         = default;

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
