#pragma once
#include <cstdint>
#include <cstddef>
#include "console.hpp"

// COM1 (0x3F8) シリアルポートへの出力。QEMU の -serial stdio で
// ホスト端末に直接テキストが流れるため、表示確認に使える。
namespace serial
{

class Serial : public IConsole
{
  public:
    Serial(); // グローバル変数の構築時に COM1 を初期化する
    void write(const char *s, size_t n) override;

  private:
    static void put(char c); // 1文字送信 ('\n' は "\r\n" に変換)
};

inline Serial serial;
} // namespace serial
