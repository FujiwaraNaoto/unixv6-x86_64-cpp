#pragma once
#include "types.hpp"

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

namespace vga
{

// PC起動時にBIOSがVGAチップを80x25のテキストモードに設定する．
// 物理アドレス0xB8000から始まる4000バイトの領域が、80x25=2000文字分のテキストバッファとして使われる．
constexpr size_t WIDTH  = 80;
constexpr size_t HEIGHT = 25;
constexpr uint32_t ADDR = 0xB8000;

class VGA
{

  public:
    VGA()
        : attr_(make_attr(Color::LightGreen, Color::Black)), buffer_(reinterpret_cast<uint16_t *>(ADDR)), width_(WIDTH),
          height_(HEIGHT), row_(0), col_(0)
    {

        for (size_t i = 0; i < width_ * height_; i++)
        {
            buffer_[i] = make_entry(' ', attr_);
        }
        move_cursor();
    }
    void putchar(char c);
    void puts(const char *s);
    void printf(const char *fmt, ...);
    void print_uint(unsigned long long n, int base, int width, char pad);

  private:
    static uint8_t make_attr(Color fg, Color bg);
    static uint16_t make_entry(char c, uint8_t attr);

    volatile uint16_t *buffer();
    void move_cursor();
    void scroll();
    uint8_t attr_;
    uint16_t *buffer_;
    size_t width_;
    size_t height_;
    size_t row_;
    size_t col_;
};


} // namespace vga
