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

// 実体は kernel_main が作り、このポインタに設定する。
// グローバル変数として持つと .init_array の並び (= リンク順) で初期化されるため、
// 他のグローバルとの初期化順序が「たまたま動いている」状態になりやすい。
// main で明示的に構築することで、初期化順序の問題を避ける。
inline Serial *serial = nullptr;
} // namespace serial
