#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstdint>

// 処理したバイト数 (>= 0) か、失敗なら kError (-1) を返す関数の戻り値 (POSIX の read/write と同じ)
using ssize_t = int64_t;

// ssize_t を返す関数の失敗
constexpr ssize_t kError = -1;

#endif // TYPES_HPP
