#pragma once
#include <cstddef>
#include <cstdint>
#include "console.hpp"

namespace vga
{

class VGA : public IConsole
{

  public:
    VGA();
    void write(const char *s, size_t n) override;
    void set_color(Color fg, Color bg) override;

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

// 実体は kernel_main が作り、このポインタに設定する。
// グローバル変数として持つと .init_array の並び (= リンク順) で初期化されるため、
// 他のグローバルとの初期化順序が「たまたま動いている」状態になりやすい。
// main で明示的に構築することで、初期化順序の問題を避ける。
inline VGA *vga = nullptr;


} // namespace vga
