#include <cstdint>
#include "vga.hpp"
#include "serial.hpp"
#include "idt.hpp"
#include "pic.hpp"
#include "io.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "heap.hpp"
#include "process.hpp"
#include "multiboot2.hpp"
#include "syscall.hpp"
#include "keyboard.hpp"
#include "gdt.hpp"
#include "usermode.hpp"
#include "virtioblock.hpp"
#include "buffer_cache.hpp"

// tests/ 以下は make tests (TESTS=1) のときだけコンパイル・リンクされる。
// 通常ビルドではテストコードはカーネルに一切含まれない。
#ifdef ENABLE_TESTS
#    include "tests.hpp"
#endif

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

// カーネル終端の物理アドレス (kernel.ld で kernel_end - KERNEL_VMA として定義)。
// 絶対シンボルなので RIP 相対では参照できないが、-mcmodel=kernel なら
// R_X86_64_32S (符号付き32bit絶対) で解決されるため直接参照できる。
extern "C" uint8_t kernel_phys_end[];

// カーネルのエントリポイント。
// ここではハードウェアとカーネルサブシステムの初期化だけを行い、
// 各機能の動作確認は tests/ 以下の tests::run_all() に任せる
// (make tests でビルドしたときのみ呼ばれる)。
extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, uint32_t mb_addr)
{
    // 他のどのグローバル変数を使う前に、コンストラクタを実行する。
    // これで serial / vga はグローバル宣言だけで初期化される。
    call_global_constructors();

    vga::VGA vga_instance;
    vga::vga = &vga_instance; // グローバルにアクセス

    vga::vga->puts("Hello World\n");

    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる
    pic::InitializePIT(100);        // タイマー割り込みを約100Hzで発生させる

    idt::InterruptDescriptorTable idt;
    vga::vga->puts("IDT / interrupt handlers\n");

    auto *mmap = find_mmap(mb_addr);
    if (!mmap)
    {
        vga::vga->puts("Memory map not found\n");
        asm volatile("hlt");
    }

    // PMM は物理アドレスを扱うので、高位カーネル化後は kernel_end (仮想) ではなく
    // kernel_phys_end (物理) を渡す。仮想を渡すと「カーネル領域スキップ」判定が
    // 常に真になり、さらに終端までのループが事実上無限ループになる。
    // mb_addr も渡すのは、GRUB が置いた multiboot2 情報構造体を PMM に予約させるため。
    // 情報構造体はカーネル終端より後ろにあるので、渡さないと空きページとして配られる。
    pmm::PhysicalMemoryManager pmm(mmap, reinterpret_cast<uint64_t>(kernel_phys_end), mb_addr);
    vmm::VirtualMemoryManager vmm_instance = vmm::VirtualMemoryManager(&pmm);
    // カーネルヒープは高位 (0xFFFFFFFF90000000〜) に置く。
    // 低位の identity map (PML4[0]) には依存しない。
    heap::Heap heap_instance(heap::KERNEL_HEAP_BASE, heap::KERNEL_HEAP_END, &pmm, &vmm_instance);

    vmm::vmm_ptr   = &vmm_instance;  // グローバルにアクセスできるようにする
    pmm::pmm_ptr   = &pmm;           // グローバルにアクセスできるようにする
    heap::heap_ptr = &heap_instance; // グローバルにアクセスできるようにする

    auto state = pmm.get_state();
    pmm::print_mm_state(state, vga::vga);

    // syscall の MSR (LSTAR/STAR/SFMASK) を設定する。
    Syscall::init();

    keyboard::initialize();

    gdt::initialize_gdt();
    static uint8_t kernel_stack[8192]; // 8KB のカーネルスタック
    gdt::set_kernel_stack(reinterpret_cast<uint64_t>(kernel_stack + sizeof(kernel_stack)));

    process::ProcessManager process_manager(heap::heap_ptr);

    BufferCache::Manager buffer_cache(BufferCache::BlockDevice{
        .read_block  = &VirtIOBlock::read_block,
        .write_block = &VirtIOBlock::write_block,
    });
    if (not buffer_cache.valid())
    {
        vga::vga->puts("BufferCache initialization failed\n");
        asm volatile("hlt");
    }
#ifdef ENABLE_TESTS
    // 各機能の動作確認 (どのテストを走らせるかは tests/tests.cpp で切り替える)
    tests::run_all();
#endif

    while (1)
    {
        asm volatile("hlt");
    }
}
