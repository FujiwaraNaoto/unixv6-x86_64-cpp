#ifndef BITMASK_ENUM_HPP
#define BITMASK_ENUM_HPP
#include <type_traits>
#include "enum_util.hpp"

// ビットフラグを表す enum class に、| と & を一度だけ定義するための仕組み。
//
// enum class は暗黙に整数へ変換されないので、フラグを組み合わせるには演算子が要る。
// それを型ごとに手書きすると同じ形が増えるため、「この enum はビットマスクである」
// という目印を付けた型にだけ、下のテンプレート演算子が効くようにする。
// (enum は継承できないので CRTP は使えない。目印で opt-in するのがこの場合の定石)
//
// 目印は enum と同じ名前空間に置いた関数宣言で、ADL で見つける。
// テンプレートの特殊化と違い「グローバル名前空間で書かなければならない」制約が無いので、
// 名前空間の中や匿名 namespace にある enum にもそのまま付けられる。
//
// 使い方: enum class の定義の直後に 1 行書く (宣言だけでよく、定義は不要)。
//
//   enum class PageFlag : uint64_t { Present = 1ULL << 0, ... };
//   void enable_bitmask_enum(PageFlag);
//
// opt-in していない enum には効かないので、種別を表す enum (VirtIOBlockRequestType など)
// をうっかり | で混ぜるとコンパイルエラーになる。

template <typename E>
concept BitmaskEnum = ScopedEnum<E> && requires(E e)
{
    enable_bitmask_enum(e);
};

template <BitmaskEnum E>
constexpr E operator|(E a, E b)
{
    using Raw = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<Raw>(a) | static_cast<Raw>(b));
}

template <BitmaskEnum E>
constexpr E operator&(E a, E b)
{
    using Raw = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<Raw>(a) & static_cast<Raw>(b));
}

#endif // BITMASK_ENUM_HPP
