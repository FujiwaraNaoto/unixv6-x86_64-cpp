#ifndef ENUM_UTIL_HPP
#define ENUM_UTIL_HPP
#include <type_traits>

// enum class を扱うための基本部品。
//
// enum class は暗黙に整数へ変換されないので、レジスタに書くときなど生の値が必要な場面では
// 取り出しが要る。型ごとに static_cast を書くと意図が埋もれるため、ここに集約する。

template <typename E>
concept ScopedEnum = std::is_enum_v<E>;

// enum class の中身の整数を取り出す (C++23 の std::to_underlying 相当)。
// 戻り値の型は enum の基底型そのままなので、幅を取り違えることがない。
template <ScopedEnum E>
constexpr std::underlying_type_t<E> to_underlying(E value)
{
    return static_cast<std::underlying_type_t<E>>(value);
}

#endif // ENUM_UTIL_HPP
