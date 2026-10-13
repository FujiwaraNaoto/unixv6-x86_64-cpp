#ifndef INCLUDE_ADDRESS_HPP
#define INCLUDE_ADDRESS_HPP
#include <cstdint>
#include <optional>
#include "units.hpp"

constexpr size_t KERNEL_STACK_SIZE = 16_KiB;

// プロセスごとのカーネルスタック領域。heap から確保した KERNEL_STACK_SIZE バイト。
// 「下端 (alloc が返した先頭)」と「上端 (rsp の出発点)」を取り違えないよう、型で区別する。
struct KernelStack
{
    uint8_t *base = nullptr; // alloc が返した先頭 (一番低いアドレス)。nullptr = 未確保

    uint8_t *top() const
    {
        return base + KERNEL_STACK_SIZE;
    } // rsp の出発点 (上端)

    explicit operator bool() const
    {
        return base != nullptr;
    }
};

// 物理アドレス。
// 仮想アドレスや普通の整数と取り違えないよう、型で区別する。
// 物理 0 も実在するページなので、「無効」は 0 ではなく nullopt で表す。
struct PhysicalAddress
{
    std::optional<uint64_t> address;

    // if (!phys) で「無効 (nullopt)」を判定できるようにする。
    explicit operator bool() const
    {
        return address.has_value();
    }
};

// マップ対象の仮想アドレス (ユーザー空間やヒープなど任意の場所)。
// VirtualAddress (direct map 上のページテーブルを指すポインタ) とは用途が違うので、
// 中身を読み書きするポインタではなく、ただのアドレス値として持つ。
struct PageVirtualAddress
{
    uint64_t address;
};

// direct map 上の仮想アドレス。
// 物理アドレス (uint64_t) と取り違えないよう、型で区別する。
struct VirtualAddress
{
    uint64_t *ptr;

    // ページテーブルのエントリを table[i] の形で読み書きできるようにする
    uint64_t &operator[](uint64_t index) const
    {
        return ptr[index];
    }

    // if (!table) で「テーブルが無い (nullptr)」を判定できるようにする。
    // explicit なので整数などへ暗黙に変換されることはない。
    explicit operator bool() const
    {
        return ptr != nullptr;
    }
};

#endif // INCLUDE_ADDRESS_HPP
