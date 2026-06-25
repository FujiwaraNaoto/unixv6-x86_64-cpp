#include "keyboard.hpp"
#include "pic.hpp"
#include "io.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>


namespace
{
constexpr std::array<char, 128> scancode_map = {
    0,    27,  '1', '2',  '3',  '4',  '5', '6', '7',  '8', // 0x00-0x09
    '9',  '0', '-', '=',  '\b', '\t', 'q', 'w', 'e',  'r', // 0x0A-0x13
    't',  'y', 'u', 'i',  'o',  'p',  '[', ']', '\n', 0,   // 0x14-0x1D (0x1D=Ctrl)
    'a',  's', 'd', 'f',  'g',  'h',  'j', 'k', 'l',  ';', // 0x1E-0x27
    '\'', '`', 0,   '\\', 'z',  'x',  'c', 'v', 'b',  'n', // 0x28-0x31 (0x2A=LShift)
    'm',  ',', '.', '/',  0,    '*',  0,   ' ', 0,    0,   // 0x32-0x3B (0x36=RShift,0x38=Alt)
};

constexpr std::array<char, 128> scancode_map_shift = {
    0,   27,  '!', '@', '#',  '$',  '%', '^', '&',  '*', // 0x00-0x09
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E',  'R', // 0x0A-0x13
    'T', 'Y', 'U', 'I', 'O',  'P',  '{', '}', '\n', 0,   // 0x14-0x1D (0x1D=Ctrl)
    'A', 'S', 'D', 'F', 'G',  'H',  'J', 'K', 'L',  ':', // 0x1E-0x27
    '"', '~', 0,   '|', 'Z',  'X',  'C', 'V', 'B',  'N', // 0x28-0x31 (0x2A=LShift)
    'M', '<', '>', '?', 0,    '*',  0,   ' ', 0,    0,   // 0x32-0x3B (0x36=RShift,0x38=Alt)
};

// 特殊キーのスキャンコード
constexpr uint8_t SC_L_SHIFT_DOWN = 0x2A;
constexpr uint8_t SC_L_SHIFT_UP   = 0xAA;
constexpr uint8_t SC_R_SHIFT_DOWN = 0x36;
constexpr uint8_t SC_R_SHIFT_UP   = 0xB6;

// キーを離すとスキャンコードのbit7が1になるので、キーを離すスキャンコードを判定するためのマスク
constexpr uint8_t SC_RELEASE_MASK = 0x80;

// 拡張キー (カーソルキー・Delete など) は 0xE0 を前置した2バイトで送られてくる
constexpr uint8_t SC_EXTENDED_PREFIX = 0xE0;
constexpr uint8_t SC_E0_DELETE       = 0x53; // 0xE0 0x53 = Delete

std::array<char, 256> buffer_       = {};
std::atomic<size_t> head_           = 0;
std::atomic<size_t> tail_           = 0;
std::atomic<bool> shift_pressed_    = false;
std::atomic<bool> extended_         = false; // 直前に 0xE0 を受け取ったか

// バッファが満杯でなければ文字を1つ積む (満杯なら捨てる)
void enqueue(char c)
{
    if (c == 0)
        return;
    size_t head = head_;
    size_t next = (head + 1) % buffer_.size();
    if (next != tail_) // 満杯なら next == tail となり空と区別できないため捨てる
    {
        buffer_[head] = c;
        head_         = next;
    }
}
} // namespace

namespace keyboard
{
void initialize()
{
    pic::unmask_irq(1); // IRQ1 (キーボード) の割り込みを有効化
    head_ = tail_  = 0;
    shift_pressed_ = false;
    extended_      = false;
}
bool has_input()
{
    return head_ != tail_;
}

char getchar()
{
    if (head_ == tail_)
        return 0; // return 0 if no character is available
    char c = buffer_[tail_];
    tail_  = (tail_ + 1) % buffer_.size();
    return c;
}

void handle_irq()
{
    uint8_t scancode = io::inb(0x60); // キーボードコントローラのデータポートからスキャンコードを読み取る

    // 拡張キーは 0xE0 + 本体スキャンコードの2バイト。まず 0xE0 を覚えておき、
    // 次の割り込みで本体を処理する。
    if (scancode == SC_EXTENDED_PREFIX)
    {
        extended_ = true;
        return;
    }

    if (extended_)
    {
        extended_ = false;
        // 拡張キーは押下のみ扱う (離した時は bit7 が立つので無視)
        if (!(scancode & SC_RELEASE_MASK) && scancode == SC_E0_DELETE)
        {
            enqueue('\b'); // Delete も Backspace と同じく直前の文字を消す
        }
        return;
    }

    // update shift key state
    if (scancode == SC_L_SHIFT_DOWN || scancode == SC_R_SHIFT_DOWN)
    {
        shift_pressed_ = true;
        return;
    }
    else if (scancode == SC_L_SHIFT_UP || scancode == SC_R_SHIFT_UP)
    {
        shift_pressed_ = false;
        return;
    }

    // ignore if the key is released
    if (scancode & SC_RELEASE_MASK)
        return;

    if (scancode >= scancode_map.size())
    {
        return;
    }

    // convert scancode to character using the appropriate map based on Shift key state
    char c = shift_pressed_ ? scancode_map_shift[scancode] : scancode_map[scancode];
    enqueue(c);
}
} // namespace keyboard
