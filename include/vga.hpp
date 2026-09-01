#pragma once
#include <cstddef>
#include <cstdint>
#include "console.hpp"

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

class VGA : public IConsole
{

  public:
    VGA();
    void write(const char *s, size_t n) override;
    void set_color(Color fg, Color bg);

  private:
    void put(char c); // 1文字を画面に反映する (カーソル移動・スクロール込み)
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

inline VGA *vga;


} // namespace vga
