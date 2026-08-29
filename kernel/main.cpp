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

// CRT 相当: リンカが .init_array に並べたグローバルコンストラクタを
// 先頭から末尾まで順に呼ぶ。境界シンボルは kernel.ld で定義している。
extern "C"
{
    using ctor_t = void (*)();
    extern ctor_t __init_array_start[];
    extern ctor_t __init_array_end[];
}

// リング3で実行されるユーザープログラム
// (カーネル内に置くが、User許可ページにマップして実行する)
[[gnu::section(".user")]] static void user_program()
{
    const char msg[] = "Hello from ring 3!\n";
    asm volatile("mov $1, %%rax\n"  // write
                 "mov $1, %%rdi\n"  // stdout
                 "mov %0, %%rsi\n"  // buf
                 "mov $19, %%rdx\n" // len
                 "syscall\n"
                 :
                 : "r"(msg)
                 : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
    asm volatile("mov $60, %%rax\n" // exit
                 "xor %%rdi, %%rdi\n"
                 "syscall\n"
                 :
                 :
                 : "rax", "rdi");
    // 念のため
    while (1)
    {
        asm volatile("hlt");
    }
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


// 意図的に 0 除算 (#DE) を発生させて isr_common_handler を起こす。
// volatile を使わないと 1/0 はコンパイル時に畳まれ div 命令が出ないため、
// 実行時に必ず CPU 例外が起きるよう除数をメモリ経由にする。
[[maybe_unused]] static void intentional_division_by_zero()
{
    volatile int zero = 0;
    volatile int x    = 1 / zero;
    (void)x;
}

static void thread_A()
{
    for (int i = 0; i < 3; ++i)
    {
        vga::vga->set_color(Color::LightCyan, Color::Black);
        vga::vga->printf("[THREAD-A] iteration %u\n", static_cast<unsigned>(i));
        vga::vga->set_color(Color::LightGrey, Color::Black);
        process::yield();
    }
}

static void thread_B()
{
    for (int i = 0; i < 3; ++i)
    {
        vga::vga->set_color(Color::LightCyan, Color::Black);
        vga::vga->printf("[THREAD-B] iteration %u\n", static_cast<unsigned>(i));
        vga::vga->set_color(Color::LightGrey, Color::Black);
        process::yield();
    }
}


// PML4分離テスト用スレッド
// 同じ仮想アドレスに別の値を書いて確認する。
// アドレスは PML4 スロット1 (512GiB〜) に置く。スロット0は
// create_address_space() がカーネル空間 (identity map) として全プロセスで
// 共有するため、スロット0内のアドレスではプロセスごとに分離できない。
static constexpr uint64_t kAddrTestVirt = 0x8000000000; // PML4 index 1
static volatile uint64_t test_result_a  = 0;
static volatile uint64_t test_result_b  = 0;

static void addrspace_thread_a()
{
    volatile uint64_t *p = reinterpret_cast<volatile uint64_t *>(kAddrTestVirt);
    *p                   = 0xAAAA;
    process::yield();   // Bに切り替わる
    test_result_a = *p; // 戻ってきて自分の値を再確認
}

static void addrspace_thread_b()
{
    volatile uint64_t *p = reinterpret_cast<volatile uint64_t *>(kAddrTestVirt);
    *p                   = 0xBBBB;
    process::yield(); // Aに切り替わる
    test_result_b = *p;
}

// sleep/wakeup デモ用の待機理由 (任意のポインタ値)
static int sleep_channel;
static volatile bool sleeper_woke = false;
static void sleeper_thread()
{
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->puts("[SLEEP] sleeper going to sleep...\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);

    process::sleep(&sleep_channel); // ここで寝る

    // 起こされたら再開
    sleeper_woke = true;
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->puts("[SLEEP] sleeper woke up!\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);
}

static void waker_thread()
{
    // 少し他の処理を挟んでから起こす
    for (int i = 0; i < 3; i++)
        process::yield();

    vga::vga->set_color(Color::LightMagenta, Color::Black);
    vga::vga->puts("[WAKE]  waking sleeper...\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    process::wakeup(&sleep_channel);
}

// fork/exit/wait デモ用スレッド
static void parent_thread()
{
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->printf("[FORK] parent: calling fork()\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);

    int pid = process::fork();
    if (pid == 0)
    {
        // 子
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->printf("[FORK] child: I am the child, exiting with 42\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        process::exit(42);
    }
    else
    {
        // 親
        vga::vga->set_color(Color::Yellow, Color::Black);
        vga::vga->printf("[FORK] parent: forked child pid=%u, waiting...\n", (unsigned)pid);
        vga::vga->set_color(Color::LightGrey, Color::Black);

        int code;
        int wpid = process::wait(&code);
        vga::vga->set_color(Color::Yellow, Color::Black);
        vga::vga->printf("[FORK] parent: child %u exited with code %u\n", (unsigned)wpid, (unsigned)code);
        vga::vga->set_color(Color::LightGrey, Color::Black);
    }
}

extern "C" void kernel_main([[maybe_unused]] uint32_t mb_magic, [[maybe_unused]] uint32_t mb_addr)
{
    // 他のどのグローバル変数を使う前に、コンストラクタを実行する。
    // これで serial / vga はグローバル宣言だけで初期化される。
    call_global_constructors();

    vga::VGA vga_instance;
    vga::vga = &vga_instance; // グローバルにアクセス

    // print_banner();
    vga::vga->puts("Hello World\n");

    uint64_t rip;
    asm volatile("lea (%%rip), %0" : "=r"(rip));
    vga::vga->set_color(Color::LightMagenta, Color::Black);
    vga::vga->puts("[HIGH] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("running at RIP=0x%x (high-half if >0xFFFFFFFF80000000)\n",
                     (unsigned)(rip >> 32)); // 上位32bitを表示

    vga::vga->set_color(Color::LightMagenta, Color::Black);
    vga::vga->puts("[HIGH] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->puts("VGA via direct map OK\n");


    pic::InitializePIC(0x20, 0x28); // IRQ0-7は0x20-0x27、IRQ8-15は0x28-0x2Fに割り当てる

    pic::InitializePIT(100); // タイマー割り込みを約100Hzで発生させる

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
    pmm::PhysicalMemoryManager pmm(mmap, reinterpret_cast<uint64_t>(kernel_phys_end));
    vmm::VirtualMemoryManager vmm_instance = vmm::VirtualMemoryManager(&pmm);
    // カーネルヒープは高位 (0xFFFFFFFF90000000〜) に置く。
    // 低位の identity map (PML4[0]) には依存しない。
    heap::Heap heap_instance(heap::KERNEL_HEAP_BASE, heap::KERNEL_HEAP_END, &pmm, &vmm_instance);

    vmm::vmm_ptr   = &vmm_instance;  // グローバルにアクセスできるようにする
    pmm::pmm_ptr   = &pmm;           // グローバルにアクセスできるようにする
    heap::heap_ptr = &heap_instance; // グローバルにアクセスできるようにする
    auto state     = pmm.get_state();
    pmm::print_mm_state(state);

    uint64_t p1 = pmm.allocate();
    uint64_t p2 = pmm.allocate();
    uint64_t p3 = pmm.allocate();
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[PMM]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("alloc test: 0x%x  0x%x  0x%x\n",
                     static_cast<unsigned>(p1),
                     static_cast<unsigned>(p2),
                     static_cast<unsigned>(p3));
    pmm.free(p2);
    uint64_t p4 = pmm.allocate();
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[PMM]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("free+realloc: freed=0x%x  got=0x%x  %s\n",
                     static_cast<unsigned>(p2),
                     static_cast<unsigned>(p4),
                     p4 == p2 ? "OK" : "MISMATCH");
    {
        void *brk0 = heap::heap_ptr->sbrk(0);         // 現在の brk
        void *brk1 = heap::heap_ptr->sbrk(PAGE_SIZE); // 1ページ伸ばす
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->puts("[SBRK] ");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        vga::vga->printf("brk before=0x%016x  returned=0x%016x  now=0x%016x\n",
                         reinterpret_cast<uintptr_t>(brk0),
                         reinterpret_cast<uintptr_t>(brk1),
                         reinterpret_cast<uintptr_t>(heap::heap_ptr->sbrk(0)));

        // alloc/free テスト (morecore が自動で呼ばれる)
        void *p1 = heap::heap_ptr->alloc(64);
        void *p2 = heap::heap_ptr->alloc(128);
        void *p3 = heap::heap_ptr->alloc(32);
        heap::heap_ptr->free(p2);
        void *p4 = heap::heap_ptr->alloc(64); // p2 の領域が再利用されるはず
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->puts("[HEAP] ");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        vga::vga->printf("p1=0x%016x p2=0x%016x p3=0x%016x p4=0x%016x reuse=%s\n",
                         reinterpret_cast<uintptr_t>(p1),
                         reinterpret_cast<uintptr_t>(p2),
                         reinterpret_cast<uintptr_t>(p3),
                         reinterpret_cast<uintptr_t>(p4),
                         p4 == p2 ? "OK" : "MISMATCH");
    }

    // syscall の MSR (LSTAR/STAR/SFMASK) を設定する。
    // 注意: ハンドラは sysret でリング3へ戻るため、カーネル(リング0)から
    //       直接 syscall を撃つことはできない (sysret が CPL=3 を強制し、
    //       カーネルコードページに User 権限がないため #PF→#DF→トリプルフォルト
    //       になる)。実際の syscall テストは下の user_program (リング3) で行う。
    syscall::init();

    // // カーネルから syscall 命令を直接テスト (リング0→0)
    //     const char msg[] = "Hello from syscall!\n";
    //     uint64_t ret;
    //     asm volatile("mov $1, %%rax\n" // RAX = 1 (write)
    //                  "mov $1, %%rdi\n" // RDI = 1 (stdout)
    //                  "mov %1, %%rsi\n" // RSI = buf
    //                  "mov %2, %%rdx\n" // RDX = len
    //                  "syscall\n"
    //                  "mov %%rax, %0\n" // 戻り値
    //                  : "=r"(ret)
    //                  : "r"(msg), "r"((uint64_t)(sizeof(msg) - 1))
    //                  : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
    //     vga::vga->set_color(Color::LightGreen, Color::Black);
    //     vga::vga->puts("[SYS]  ");
    //     vga::vga->set_color(Color::LightGrey, Color::Black);
    //     vga::vga->printf("write() returned %u\n", (unsigned)ret);


    keyboard::initialize();

    gdt::initialize_gdt();
    static uint8_t kernel_stack[8192]; // 8KB のカーネルスタック
    gdt::set_kernel_stack(reinterpret_cast<uint64_t>(kernel_stack + sizeof(kernel_stack)));


    // {
    // uint64_t code_page = reinterpret_cast<uint64_t>(&user_program) & PAGE_MASK;
    // vmm::vmm_ptr->map_page(code_page, vmm::vmm_ptr->virtual_to_physical(code_page), vmm::PageFlag::User |
    // vmm::PageFlag::Present | vmm::PageFlag::Writable);

    //     // ユーザースタックを確保して User許可でマップ
    //     uint64_t ustack_phys = pmm.allocate();
    //     uint64_t ustack_virt = 0x600000;
    //     vmm::vmm_ptr->map_page(ustack_virt, ustack_phys,
    //                   vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User);

    //     // リング3へ遷移 (16バイト境界に揃える)
    //     usermode::enter(
    //         reinterpret_cast<uint64_t>(&user_program),
    //         (ustack_virt + PAGE_SIZE - 16));

    // }


    // {
    //     vga::vga->set_color(Color::LightCyan, Color::Black);
    //     vga::vga->puts("\n[KBD]  type something (echo test):\n> ");
    //     vga::vga->set_color(Color::LightGrey, Color::Black);
    //     while (1)
    //     {
    //         if (keyboard::has_input())
    //         {
    //             char c = keyboard::getchar();
    //             vga::vga->putchar(c);
    //             if (c == '\n')
    //             {
    //                 vga::vga->puts("> ");
    //             }
    //         }
    //         asm volatile("hlt");
    //     }
    // }


    process::ProcessManager process_manager(heap::heap_ptr);
    // Process *procA = process::create_process(thread_A, "Thread A");
    // Process *procB = process::create_process(thread_B, "Thread B");
    // vga::vga->printf("[DBG] procA=0x%x stateA=%d procB=0x%x stateB=%d\n",
    //                  static_cast<unsigned>(reinterpret_cast<uintptr_t>(procA)),
    //                  procA ? static_cast<int>(procA->state) : -1,
    //                  static_cast<unsigned>(reinterpret_cast<uintptr_t>(procB)),
    //                  procB ? static_cast<int>(procB->state) : -1);
    // {
    //     process::yield(); // 最初のプロセスに切り替える
    // }

    // Process::init();

    // {

    //     Process *pa = process::create_process(addrspace_thread_a, "addr-a");
    //     Process *pb = process::create_process(addrspace_thread_b, "addr-b");

    //     // 各プロセスの同一仮想アドレスに物理ページを別々にマップ
    //     uint64_t phys_a = pmm.allocate();
    //     uint64_t phys_b = pmm.allocate();
    //     vmm::vmm_ptr->map_page_in(pa->pml4, kAddrTestVirt, phys_a, vmm::PageFlag::Present | vmm::PageFlag::Writable);
    //     vmm::vmm_ptr->map_page_in(pb->pml4, kAddrTestVirt, phys_b, vmm::PageFlag::Present | vmm::PageFlag::Writable);

    //     // process::print_table();
    //     process::yield(); // スケジューラ起動

    //     //両方のスレッド終了後に結果を確認
    //     vga::vga->set_color(Color::LightGreen, Color::Black);
    //     vga::vga->puts("[ADDR] ");
    //     vga::vga->set_color(Color::LightGrey, Color::Black);
    //     vga::vga->printf("A wrote 0xAAAA read 0x%x / B wrote 0xBBBB read 0x%x  %s\n",
    //                     (unsigned)test_result_a,
    //                     (unsigned)test_result_b,
    //                     (test_result_a == 0xAAAA && test_result_b == 0xBBBB) ? "SEPARATED-OK" : "SHARED-FAIL");

    // }

    // // ─── Phase 7 セット3(前半): sleep/wakeup テスト ───
    // process::create_process(sleeper_thread, "sleeper");
    // process::create_process(waker_thread, "waker");
    // process::yield();

    // vga::vga->set_color(Color::LightGreen, Color::Black);
    // vga::vga->puts("[SLEEP] ");
    // vga::vga->set_color(Color::LightGrey, Color::Black);
    // vga::vga->printf("result: sleeper %s\n", sleeper_woke ? "WOKE-OK" : "STILL-SLEEPING-FAIL");

    process::create_process(parent_thread, "parent");
    process::yield();

    if(VirtIOBlock::initialize()){
        
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->puts("[VIRTIO] VirtIO Block Device initialized successfully\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        static std::array<uint8_t, 512> buffer;
        if(VirtIOBlock::read_block(0, buffer.data())){
            vga::vga->set_color(Color::LightGreen, Color::Black);
            vga::vga->puts("[VIRTIO] Read block 0 successfully\n");
            vga::vga->set_color(Color::LightGrey, Color::Black);
            vga::vga->puts("[VIRTIO] Block 0 data:\n");
            vga::vga->set_color(Color::LightCyan, Color::Black);
            for (size_t i = 0; i < buffer.size(); ++i) {
                vga::vga->printf("%02x ", buffer[i]);
                if ((i + 1) % 16 == 0) {
                    vga::vga->puts("\n");
                }
            }
            vga::vga->set_color(Color::LightGrey, Color::Black);
        }

    }else{
        vga::vga->set_color(Color::LightRed, Color::Black);
        vga::vga->puts("[VIRTIO] VirtIO Block Device initialization failed\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
    }


    while (1)
    {
        asm volatile("hlt");
    }
}
