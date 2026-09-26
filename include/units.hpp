#ifndef UNITS_HPP
#define UNITS_HPP
#include <cstdint>

// IEC 80000-13 の 2 進接頭辞リテラル。1KiB = 1024 B。
// 桁数ではなく「意味」で大きさを書けるようにする (例: 4_KiB, 16_MiB, 1_GiB)。
//
// 予約されない名前 (_ で始まるサフィックス) を使う。標準ライブラリは
// アンダースコアなしのサフィックスだけを使うので、名前が衝突することはない。
//
// 型は uint64_t 固定。ページテーブルのエントリやアドレス計算で使うので、
// 32bit ではオーバーフローする値 (1_GiB など) も安全に扱える。

constexpr uint64_t operator""_KiB(unsigned long long n)
{
    return n * 1024ULL;
}

constexpr uint64_t operator""_MiB(unsigned long long n)
{
    return n * 1024ULL * 1024ULL;
}

constexpr uint64_t operator""_GiB(unsigned long long n)
{
    return n * 1024ULL * 1024ULL * 1024ULL;
}

#endif // UNITS_HPP
