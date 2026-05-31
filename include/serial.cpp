#include "serial.hpp"
#include "io.hpp"

namespace serial {

constexpr uint16_t PORT = 0x3F8;   // COM1 ベースアドレス

// レジスタオフセット (DLAB=0 時)
//  +0 THR/RBR/DLL   +1 IER/DLM   +2 IIR/FCR   +3 LCR
//  +4 MCR           +5 LSR       +6 MSR       +7 SCR
void initialize() {
    io::outb(PORT + 1, 0x00);   // 割り込み無効
    io::outb(PORT + 3, 0x80);   // DLAB=1 (分周比設定モード)
    io::outb(PORT + 0, 0x03);   // 分周比 下位 = 3  → 115200/3 = 38400 baud
    io::outb(PORT + 1, 0x00);   // 分周比 上位 = 0
    io::outb(PORT + 3, 0x03);   // DLAB=0, 8bit/パリティ無/ストップ1 (8N1)
    io::outb(PORT + 2, 0xC7);   // FIFO 有効・クリア・14バイト閾値
    io::outb(PORT + 4, 0x0B);   // DTR/RTS/OUT2 セット
}

// 送信ホールディングレジスタが空くまで待つ (LSR bit5)
static bool tx_ready() {
    return (io::inb(PORT + 5) & 0x20) != 0;
}

void putchar(char c) {
    if (c == '\n') {
        while (!tx_ready()) {}
        io::outb(PORT, '\r');
    }
    while (!tx_ready()) {}
    io::outb(PORT, (uint8_t)c);
}

void puts(const char *s) {
    while (*s) putchar(*s++);
}

} // namespace serial
