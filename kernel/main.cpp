#include <cstdint>
#include <optional>
#include "vga.hpp"
#include "serial.hpp"
#include "exception.hpp"
#include "idt.hpp"
#include "pic.hpp"
#include "io.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "heap.hpp"
#include "process.hpp"
#include "multiboot2.hpp"
#include "systemcall.hpp"
#include "keyboard.hpp"
#include "gdt.hpp"
#include "usermode.hpp"
#include "virtioblock.hpp"
#include "buffer_cache.hpp"
#include "file_system.hpp"

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
    // シリアルは最初に作る。以降の初期化で問題が起きたときの報告先になる。
    // (kernel_main は戻らないので、ローカルに置いても寿命はカーネルと同じ)
    serial::Serial serial_instance;
    serial::serial = &serial_instance;

    // 出力を捨てる既定のコンソール。出力先を渡されなかったモジュールが使う。
    NullConsole null_console_instance;
    null_console = &null_console_instance;

    // 例外ハンドラは asm から呼ばれて引数を受け取れないので、出力先を登録しておく。
    // IDT の構築時に VGA へ差し替わるが、それまでの例外はシリアルに出る。
    exception::set_console(serial::serial);

    // グローバル変数のコンストラクタを実行する。
    // 初期化順序は .init_array の並び = リンク順 (Makefile の CPP_SRC の順) なので、
    // グローバル同士が互いのメンバを呼ぶと、まだ構築されていない相手に触ってしまう。
    // そうならないよう、順序が要るものは main で明示的に構築すること。
    call_global_constructors();

    vga::VGA vga_instance;
    vga::vga = &vga_instance; // グローバルにアクセス

    vga::vga->puts("Hello World\n");

    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる
    pic::InitializePIT(100);        // タイマー割り込みを約100Hzで発生させる

    idt::InterruptDescriptorTable idt(vga::vga);
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
    vmm::VirtualMemoryManager vmm_instance = vmm::VirtualMemoryManager(&pmm, vga::vga);
    // カーネルヒープは高位 (0xFFFFFFFF90000000〜) に置く。
    // 低位の identity map (PML4[0]) には依存しない。
    heap::Heap heap_instance(heap::KERNEL_HEAP_BASE, heap::KERNEL_HEAP_END, &pmm, &vmm_instance);

    vmm::vmm_ptr   = &vmm_instance;  // グローバルにアクセスできるようにする
    pmm::pmm_ptr   = &pmm;           // グローバルにアクセスできるようにする
    heap::heap_ptr = &heap_instance; // グローバルにアクセスできるようにする

    auto state = pmm.get_state();
    pmm::print_mm_state(state, vga::vga);

    // syscall の MSR (LSTAR/STAR/SFMASK) を設定する。
    SystemCall::init(vga::vga);

    keyboard::initialize();

    gdt::initialize_gdt();
    static uint8_t kernel_stack[8192]; // 8KB のカーネルスタック
    gdt::set_kernel_stack(reinterpret_cast<uint64_t>(kernel_stack + sizeof(kernel_stack)));

    process::ProcessManager process_manager(heap::heap_ptr);

    // 仮想 → 物理の変換方法はカーネル側の関心事なので、ドライバには関数として渡す。
    // (キャプチャなしラムダは関数ポインタへ暗黙変換される)
    const auto resolve_physical = [](const void *virtual_address) -> PhysicalAddress
    {
        if (vmm::vmm_ptr == nullptr)
        {
            return PhysicalAddress{}; // VMM 未初期化: 変換できない
        }
        // virtual_to_physical() 自体も未マップなら無効な PhysicalAddress を返すので、そのまま伝播させる。
        return vmm::vmm_ptr->virtual_to_physical(PageVirtualAddress{reinterpret_cast<uint64_t>(virtual_address)});
    };

    // ディスクに読み書きするので、BufferCache / FileSystem より先に初期化する。
    if (!VirtIOBlock::initialize(resolve_physical))
    {
        vga::vga->puts("VirtIO block device initialization failed\n");
        asm volatile("hlt");
    }

    auto device = BufferCache::BlockDevice{
        .read_block  = &VirtIOBlock::read_block,
        .write_block = &VirtIOBlock::write_block,
    };

    BufferCache::Manager buffer_cache(device);
    if (not buffer_cache.valid())
    {
        vga::vga->puts("BufferCache initialization failed\n");
        asm volatile("hlt");
    }
    // block_store を渡さずに Manager を作ったときの既定値。
    // (グローバルに置くと初期化順序がリンク順任せになるので、ここで作る)
    NullBlockStore null_block_store_instance;
    null_block_store = &null_block_store_instance;

    BufferCache::BlockStore block_store(device);
    FileSystem::Manager fs_manager(VirtIOBlock::capacity(), &block_store, vga::vga);
#ifdef ENABLE_TESTS
    // 各機能の動作確認 (どのテストを走らせるかは tests/tests.cpp で切り替える)
    tests::run_all(vga::vga);
#endif

    while (1)
    {
        asm volatile("hlt");
    }
}
