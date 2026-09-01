#pragma once

// 文字出力先の抽象。VGA / シリアルなど「文字を出せるもの」を差し替え可能にする。
//
// 派生クラスが実装するのは putchar() / puts() の 2 つだけで、書式出力
// (printf / print_uint) は基底側でその 2 つを使って実装してある。
// これにより出力先ごとに printf を書き直す必要がない。
class IConsole
{
  public:
    virtual void putchar(char c)     = 0;
    virtual void puts(const char *s) = 0;
    virtual ~IConsole()              = default;

    // 自前の簡易 printf。対応する変換は %d %u %x %p %s %c %% で、
    // 幅指定 (%8u) とゼロ埋め (%08x)、長さ修飾子の読み飛ばし (%llu) に対応する。
    void printf(const char *fmt, ...);
    void print_uint(unsigned long long n, int base, int width, char pad);
};

// 出力を捨てる IConsole。ログを取りたくない場面や、呼び出し側が
// コンソールを持たない場面で nullptr の代わりに使う。
class NullConsole final : public IConsole
{
  public:
    void putchar(char) override { }
    void puts(const char *) override { }
};
