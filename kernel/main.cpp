#include <cstdint>
#include "vga.hpp"
#include "serial.hpp"
#include "idt.hpp"
#include "pic.hpp"
#include "io.hpp"
#include "pmm.hpp"
#include "multiboot2.hpp"

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

extern "C" uint8_t kernel_end[]; // カーネルの終端アドレス (kernel.ld で定義)


// 意図的に 0 除算 (#DE) を発生させて isr_common_handler を起こす。
// volatile を使わないと 1/0 はコンパイル時に畳まれ div 命令が出ないため、
// 実行時に必ず CPU 例外が起きるよう除数をメモリ経由にする。
[[maybe_unused]] static void intentional_division_by_zero()
{
    volatile int zero = 0;
    volatile int x    = 1 / zero;
    (void)x;
}

extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, [[maybe_unused]] uint32_t mb_addr)
{
    // 他のどのグローバル変数を使う前に、コンストラクタを実行する。
    // これで serial / vga はグローバル宣言だけで初期化される。
    call_global_constructors();

    // print_banner();
    vga::vga.puts("Hello World\n");

    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる

    pic::InitializePIT(100); // タイマー割り込みを約100Hzで発生させる

    idt::InterruptDescriptorTable idt;
    vga::vga.puts("IDT / interrupt handlers\n");

    auto *mmap = find_mmap(mb_addr);

    if(mmap){
        pmm::PhysicalMemoryManager pmm(mmap, reinterpret_cast<uint64_t>(kernel_end));
        pmm::pmm_instance = &pmm; // グローバルにアクセスできるようにする
        auto state = pmm.get_state();
        pmm::print_mm_state(state);

        uint64_t p1 = pmm.allocate();
        uint64_t p2 = pmm.allocate();
        uint64_t p3 = pmm.allocate();
        vga::vga.set_color(Color::LightGreen, Color::Black); vga::vga.puts("[PMM]  ");
        vga::vga.set_color(Color::LightGrey,  Color::Black);
        vga::vga.printf("alloc test: 0x%x  0x%x  0x%x\n",
                    (unsigned)p1, (unsigned)p2, (unsigned)p3);
        pmm.free(p2);
        uint64_t p4 = pmm.allocate();
        vga::vga.set_color(Color::LightGreen, Color::Black); vga::vga.puts("[PMM]  ");
        vga::vga.set_color(Color::LightGrey,  Color::Black);
        vga::vga.printf("free+realloc: freed=0x%x  got=0x%x  %s\n",
                    (unsigned)p2, (unsigned)p4, p4 == p2 ? "OK" : "MISMATCH");
    } else {
        vga::vga.puts("Memory map not found\n");
    }



    while (1)
    {
        asm volatile("hlt");
    }
}
