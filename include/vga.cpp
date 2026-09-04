#include "vga.hpp"
#include "io.hpp"
#include "serial.hpp"

namespace
{
// PC起動時にBIOSがVGAチップを80x25のテキストモードに設定する．
// 物理アドレス0xB8000から始まる4000バイトの領域が、80x25=2000文字分のテキストバッファとして使われる．
constexpr size_t WIDTH             = 80;
constexpr size_t HEIGHT            = 25;
constexpr uint64_t ADDR            = 0xB8000;
constexpr uint64_t DIRECT_MAP_BASE = 0xFFFF800000000000ULL;
} // namespace

namespace vga
{


VGA::VGA()
{
    attr_   = make_attr(Color::LightGreen, Color::Black);
    buffer_ = reinterpret_cast<uint16_t *>(ADDR);
    width_  = WIDTH;
    height_ = HEIGHT;
    row_    = 0;
    col_    = 0;
    for (size_t i = 0; i < width_ * height_; i++)
    {
        buffer_[i] = make_entry(' ', attr_);
    }
    move_cursor();
}

// bit0~bit3の4bit分が前景色, bit4~bit7の4bit分が背景色
uint8_t VGA::make_attr(Color fg, Color bg)
{
    return static_cast<uint8_t>((static_cast<uint8_t>(bg) << 4) | (static_cast<uint8_t>(fg) & 0x0F));
}

void VGA::set_color(Color fg, Color bg)
{
    attr_ = make_attr(fg, bg);
}

// 文字コードを下位8bit, 属性を上位8bitに詰めた16bit値を作る
uint16_t VGA::make_entry(char c, uint8_t attr)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(c)) | (static_cast<uint16_t>(attr) << 8);
}
volatile uint16_t *VGA::buffer()
{
    return reinterpret_cast<volatile uint16_t *>(ADDR + DIRECT_MAP_BASE); // direct map経由でアクセスする
}

// 0x3D4はインデックスレジスタ, 0x3D5はデータレジスタ.
// 0x0Fはカーソル位置の下位8bit, 0x0Eは上位8bitを指定する値.
void VGA::move_cursor()
{
    uint16_t pos = static_cast<uint16_t>(row_ * WIDTH + col_);
    io::out32b(0x3D4, 0x0F);
    io::out32b(0x3D5, static_cast<uint8_t>(pos & 0xFF));
    io::out32b(0x3D4, 0x0E);
    io::out32b(0x3D5, static_cast<uint8_t>(pos >> 8));
}

void VGA::scroll()
{
    auto b = buffer();
    for (size_t r = 1; r < HEIGHT; r++)
        for (size_t c = 0; c < WIDTH; c++)
            b[(r - 1) * WIDTH + c] = b[r * WIDTH + c];
    for (size_t c = 0; c < WIDTH; c++)
        b[(HEIGHT - 1) * WIDTH + c] = make_entry(' ', attr_);
    row_ = HEIGHT - 1;
}

// バイト列をまとめて出力する。シリアルへのミラーは 1 回で済ませ、
// 画面側は 1 文字ずつ put() に流す。
void VGA::write(const char *s, size_t n)
{
    serial::serial.write(s, n); // 端末確認用にシリアルへもミラー出力
    for (size_t i = 0; i < n; i++)
    {
        put(s[i]);
    }
}

void VGA::put(char c)
{
    auto b = buffer();
    if (c == '\n')
    {
        col_ = 0;
        if (++row_ >= HEIGHT)
            scroll();
    }
    else if (c == '\r')
    {
        col_ = 0;
    }
    else if (c == '\t')
    {
        col_ = (col_ + 8) & ~7u;
        if (col_ >= WIDTH)
        {
            col_ = 0;
            if (++row_ >= HEIGHT)
                scroll();
        }
    }
    else if (c == '\b') // Backspace: 直前の文字を消して戻る (Delete も keyboard 側で '\b' に正規化済み)
    {
        if (col_ > 0)
        {
            col_--;
        }
        else if (row_ > 0)
        {
            row_--;
            col_ = WIDTH - 1;
        }
        b[row_ * WIDTH + col_] = make_entry(' ', attr_);
    }
    else
    {
        b[row_ * WIDTH + col_] = make_entry(c, attr_);
        if (++col_ >= WIDTH)
        {
            col_ = 0;
            if (++row_ >= HEIGHT)
                scroll();
        }
    }
    move_cursor();
}

} // namespace vga
