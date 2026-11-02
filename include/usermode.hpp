#pragma once
#include <cstdint>
#include "address.hpp"
#include "vmm.hpp"

namespace usermode
{
// ユーザースタックを置く仮想アドレス (PML4[0] 配下 = プロセスごとの領域)。
// fork したとき copy_user_pages() が複製するのはこちら側だけ。
constexpr PageVirtualAddress kUserStack{0x600000};

// usermode::enter() に渡すスタックポインタ (16 バイト境界に揃える)。
constexpr uint64_t kUserStackTop = kUserStack.address + PAGE_SIZE - 16;

// pml4 のアドレス空間に .user セクションとユーザースタック (1 ページ) をマップする。失敗したら false。
[[nodiscard]] bool map_user_space(PhysicalAddress pml4);

[[noreturn]] void enter(uint64_t entry_point, uint64_t stack_pointer);

} // namespace usermode
