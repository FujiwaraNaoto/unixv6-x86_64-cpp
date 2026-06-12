#pragma once
#include "types.hpp"

// COM1 (0x3F8) シリアルポートへの出力。QEMU の -serial stdio で
// ホスト端末に直接テキストが流れるため、表示確認に使える。
namespace serial
{

class Serial
{
  public:
    Serial();                    // グローバル変数の構築時に COM1 を初期化する
    static void putchar(char c); // 1文字送信 ('\n' は "\r\n" に変換)
    static void puts(const char *s);
};

inline Serial serial;
} // namespace serial
