#pragma once
#include "types.hpp"

// COM1 (0x3F8) シリアルポートへの出力。QEMU の -serial stdio で
// ホスト端末に直接テキストが流れるため、表示確認に使える。
namespace serial {
    void initialize();          // 38400 baud, 8N1 で COM1 を初期化
    void putchar(char c);       // 1文字送信 ('\n' は "\r\n" に変換)
    void puts(const char *s);
}
