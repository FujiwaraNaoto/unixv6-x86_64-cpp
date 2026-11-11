#include <cstdint>
#include <new>
#include "asm/setup.hpp"
#include "idt.hpp"
#include "pic.hpp"
#include "gdt.hpp"
#include "multiboot2.hpp"

// カーネル終端の物理アドレス (kernel.ld で kernel_end - KERNEL_VMA として定義)。
// 絶対シンボルなので RIP 相対では参照できないが、-mcmodel=kernel なら
// R_X86_64_32S (符号付き32bit絶対) で解決されるため直接参照できる。
extern "C" uint8_t kernel_phys_end[];

namespace
{
// IDT の置き場所。lidt はこのオブジェクトの中の表を指すので、arch_setup() から戻った後も
// 生きている必要がある (ローカル変数にすると戻った時点で消える)。
// 関数内の static にすると初期化のガード (__cxa_guard_acquire) が要るが、
// このカーネルには無いので、静的な領域を用意して placement new で構築する。
alignas(idt::InterruptDescriptorTable) uint8_t idt_storage[sizeof(idt::InterruptDescriptorTable)];
} // namespace

bool arch_setup(IConsole *console, uint32_t boot_info, BootMemoryMap *memory_map)
{
    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる
    pic::InitializePIT(100);        // タイマー割り込みを約100Hzで発生させる

    new (idt_storage) idt::InterruptDescriptorTable(console);
    console->puts("IDT / interrupt handlers\n");

    // PMM は物理アドレスを扱うので、高位カーネル化後は kernel_end (仮想) ではなく
    // kernel_phys_end (物理) を渡す。仮想を渡すと「カーネル領域」の予約が
    // 物理メモリ全体を覆ってしまい、空きページが無くなる。
    // boot_info を渡すのは、GRUB が置いた multiboot2 情報構造体も予約に入れるため。
    // 情報構造体はカーネル終端より後ろにあるので、予約しないと空きページとして配られる。
    if (!collect_memory_regions(boot_info, reinterpret_cast<uint64_t>(kernel_phys_end), memory_map))
    {
        console->puts("Memory map not found\n");
        return false;
    }

    gdt::initialize_gdt();
    static uint8_t kernel_stack[8192]; // 8KB のカーネルスタック
    gdt::set_kernel_stack(reinterpret_cast<uint64_t>(kernel_stack + sizeof(kernel_stack)));

    return true;
}
