#pragma once
#include "types.hpp"

enum class Color : uint8_t {
    Black        = 0,  Blue         = 1,  Green      = 2,  Cyan        = 3,
    Red          = 4,  Magenta      = 5,  Brown      = 6,  LightGrey   = 7,
    DarkGrey     = 8,  LightBlue    = 9,  LightGreen = 10, LightCyan   = 11,
    LightRed     = 12, LightMagenta = 13, Yellow     = 14, White       = 15,
};

namespace vga {
    void initialize();
    void clear();

    /**
     * テキストバッファに画面のカーソルを動かす処理
     * 
     * fg: font color, bg: background color
     */
    void set_color(Color fg, Color bg);
    void putchar(char c);
    void puts(const char *s);
    void printf(const char *fmt, ...);
}
