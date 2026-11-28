#ifndef ASM_ADDRESS_HPP
#define ASM_ADDRESS_HPP
#include <cstdint>
#include <cstddef>
#include <optional>
#include "units.hpp"

constexpr size_t KERNEL_STACK_SIZE = 16_KiB;

// カーネルスタックの中の位置 (仮想アドレス)。rsp の値などを表す。
// どのプロセスのスタックかは持たないので、別のスタックの同じ位置へ写すときは
// KernelStack::offset_of() と KernelStack::at() を通す。
struct KernelStackAddress
{
    uint64_t address;
};

// プロセスごとのカーネルスタック領域。heap から確保した KERNEL_STACK_SIZE バイト。
// 「下端 (allocate が返した先頭)」と「上端 (rsp の出発点)」を取り違えないよう、型で区別する。
struct KernelStack
{
    uint8_t *base = nullptr; // allocate が返した先頭 (一番低いアドレス)。nullptr = 未確保

    uint8_t *top() const
    {
        return base + KERNEL_STACK_SIZE;
    } // rsp の出発点 (上端)

    // address が下端から何バイト目か
    uint64_t offset_of(KernelStackAddress address) const
    {
        return address.address - reinterpret_cast<uint64_t>(base);
    }

    // 下端から offset バイト目の位置
    KernelStackAddress at(uint64_t offset) const
    {
        return KernelStackAddress{reinterpret_cast<uint64_t>(base) + offset};
    }

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

#endif // ASM_ADDRESS_HPP
