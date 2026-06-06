#include "types.hpp"
#include "vga.hpp"
#include "serial.hpp"
#include "idt.hpp"
#include "pic.hpp"
#include "io.hpp"

// CRT 相当: リンカが .init_array に並べたグローバルコンストラクタを
// 先頭から末尾まで順に呼ぶ。境界シンボルは kernel.ld で定義している。
extern "C"
{
    using ctor_t = void (*)();
    extern ctor_t __init_array_start[];
    extern ctor_t __init_array_end[];
}

static void call_global_constructors()
{
    for (ctor_t *fn = __init_array_start; fn != __init_array_end; ++fn)
        (*fn)();
}


extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, [[maybe_unused]] uint32_t mb_addr)
{
    // 他のどのグローバル変数を使う前に、コンストラクタを実行する。
    // これで serial / vga はグローバル宣言だけで初期化される。
    call_global_constructors();

    // print_banner();
    vga::vga.puts("  [ ] IDT / interrupt handlers\n");
    vga::vga.puts("  [ ] Physical memory allocator\n");

    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる

    pic::InitializePIT();

    idt::InterruptDescriptorTable idt;
    vga::vga.puts("  IDT / interrupt handlers\n");
    

    while (1)
    {
        asm volatile("hlt");
    }
}
