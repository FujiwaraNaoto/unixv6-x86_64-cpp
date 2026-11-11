#pragma once
#include <cstdint>
#include "console.hpp"
#include "memory_region.hpp"

// CPU と割り込みまわりの初期化をして、物理メモリの情報を memory_map に集める。
// kernel_main が VGA を作った後、PMM を作る前に呼ぶ。
// x86-64 では PIC / PIT、IDT、GDT と TSS を設定し、multiboot2 のメモリマップを読む。
// boot_info は x86-64 では multiboot2 情報構造体の物理アドレス。
// console は割り込み・例外ハンドラと初期化メッセージの出力先。
// メモリマップが見つからなければ false を返す。
bool arch_setup(IConsole *console, uint32_t boot_info, BootMemoryMap *memory_map);
