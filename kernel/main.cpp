#include <cstdint>
#include <cstring>
#include <optional>
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
#include "file_system.hpp"

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
// 低位 identity map 撤去後は PML4[0] もプロセスごとに独立しているが、
// このテストは従来どおり PML4 スロット1 (512GiB〜) を使う。
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

// バイト列を 1行16バイトの hexdump 形式で表示する (xxd / od -tx1z 風)。
//   0000: 48 45 4c 4c 4f 20 56 49 52 54 49 4f 20 42 4c 4f  |HELLO VIRTIO BLO|
// 端数行は空白で桁を揃え、印字できないバイトは '.' に置き換える。
// 1行は 6 + 16*3 + 2 + 16 + 1 = 73 桁なので VGA の 80 桁に収まる。
static void hexdump(const uint8_t *data, size_t size)
{
    constexpr size_t COLUMNS = 16;

    for (size_t offset = 0; offset < size; offset += COLUMNS)
    {
        const size_t line_length = (size - offset < COLUMNS) ? (size - offset) : COLUMNS;

        vga::vga->printf("%04zx: ", offset);

        for (size_t i = 0; i < COLUMNS; i++)
        {
            if (i < line_length)
                vga::vga->printf("%02x ", static_cast<unsigned>(data[offset + i]));
            else
                vga::vga->puts("   ");
        }

        vga::vga->puts(" |");
        for (size_t i = 0; i < line_length; i++)
        {
            const char c = static_cast<char>(data[offset + i]);
            vga::vga->putchar((c >= 0x20 && c < 0x7F) ? c : '.');
        }
        vga::vga->puts("|\n");
    }
}

// ファイルシステム層を通した読み書きのテスト。
// 使うのは IBlockStore と FileSystem の公開 API だけで、
// BufferCache や VirtIOBlock には直接触れない。
static void filesystem_read_write_test(IBlockStore &store, IConsole *console)
{
    auto label = [console](const char *result, const char *what)
    { console->printf("[FSTEST] %-6s %s\n", result, what); };

    // ── ① ルート inode を読む ──
    {
        auto block = store.acquire(FileSystem::inode_block(ROOTINO));
        if (!block)
        {
            label("FAIL", "read root inode: acquire failed");
            return;
        }
        const auto *inodes    = reinterpret_cast<const DiskInode *>(block.data());
        const DiskInode &root = inodes[ROOTINO % INODES_PER_BLOCK];
        const bool ok         = (root.type == InodeType::kDirectory) && (root.nlink == 1) && (root.addrs[0] != 0);
        label(ok ? "OK" : "FAIL", "read root inode");
        console->printf("         type=%u nlink=%u size=%u addrs[0]=%u\n",
                        static_cast<unsigned>(root.type),
                        static_cast<unsigned>(root.nlink),
                        static_cast<unsigned>(root.size),
                        static_cast<unsigned>(root.addrs[0]));
    }

    // ── ② ルートディレクトリを読んで "." と ".." を確認する ──
    {
        uint32_t dir_block = 0;
        {
            auto block = store.acquire(FileSystem::inode_block(ROOTINO));
            if (!block)
            {
                label("FAIL", "read root dir: acquire inode failed");
                return;
            }
            const auto *inodes = reinterpret_cast<const DiskInode *>(block.data());
            dir_block          = inodes[ROOTINO % INODES_PER_BLOCK].addrs[0];
        }

        auto block = store.acquire(dir_block);
        if (!block)
        {
            label("FAIL", "read root dir: acquire data failed");
            return;
        }
        const auto *entries = reinterpret_cast<const DiskDirEntry *>(block.data());
        const int capacity  = FSBLOCK_SIZE / sizeof(DiskDirEntry);

        bool found_dot    = false;
        bool found_dotdot = false;
        int used          = 0;
        for (int i = 0; i < capacity; i++)
        {
            if (entries[i].inum == 0)
            {
                continue;
            }
            used++;
            const DirEntry entry = FileSystem::to_memory(entries[i]);
            if (entry.name == ".")
            {
                found_dot = entry.inum == ROOTINO;
            }
            else if (entry.name == "..")
            {
                found_dotdot = entry.inum == ROOTINO;
            }
        }
        label(found_dot && found_dotdot ? "OK" : "FAIL", "read root dir entries");
        console->printf("         entries=%d  \".\"=%s  \"..\"=%s\n",
                        used,
                        found_dot ? "yes" : "no",
                        found_dotdot ? "yes" : "no");
    }

    // ── ③ ブロックを確保して書き、読み戻して検証し、解放する ──
    {
        auto allocated = FileSystem::allocate_block();
        if (!allocated.has_value())
        {
            label("FAIL", "allocate_block");
            return;
        }
        const uint32_t blockno = *allocated;

        // 書き込む: 先頭にマーカー、残りは位置に応じた値
        {
            auto block = store.acquire(blockno);
            if (!block)
            {
                label("FAIL", "write: acquire failed");
                return;
            }
            uint8_t *data       = block.data();
            const char marker[] = "UNIXV6-FS-RW-TEST";
            std::memcpy(data, marker, sizeof(marker));
            for (uint32_t i = sizeof(marker); i < FSBLOCK_SIZE; i++)
            {
                data[i] = static_cast<uint8_t>(i & 0xFF);
            }
            if (!block.write())
            {
                label("FAIL", "write: write back failed");
                return;
            }
        }

        // 書いたブロックをキャッシュから追い出してから読み直す。
        // これをしないとキャッシュ上の同じバッファを見るだけになり、
        // ディスクまで届いたことの確認にならない。
        // 追い出しには blockno 以外のブロックを NBUF 個以上触る必要がある
        // (blockno 自身を触ると MRU 側に移動して逆に残ってしまう)。
        uint32_t touched = 0;
        for (uint32_t b = 1; touched < NBUF + 1; b++)
        {
            if (b == blockno)
            {
                continue;
            }
            auto evict = store.acquire(b);
            if (!evict)
            {
                break;
            }
            touched++;
        }

        // 読み戻して検証する
        {
            auto block = store.acquire(blockno);
            if (!block)
            {
                label("FAIL", "read back: acquire failed");
                return;
            }
            const uint8_t *data = block.data();
            const char marker[] = "UNIXV6-FS-RW-TEST";
            bool ok             = std::memcmp(data, marker, sizeof(marker)) == 0;
            for (uint32_t i = sizeof(marker); ok && i < FSBLOCK_SIZE; i++)
            {
                ok = data[i] == static_cast<uint8_t>(i & 0xFF);
            }
            label(ok ? "OK" : "FAIL", "write -> read back 512 bytes");
            console->printf("         block=%u first16: ", static_cast<unsigned>(blockno));
            for (int i = 0; i < 16; i++)
            {
                console->printf("%02x ", static_cast<unsigned>(data[i]));
            }
            console->puts("\n");
        }

        // 解放して、同じブロックが再び確保できることを確認する
        FileSystem::free_block(blockno);
        auto again = FileSystem::allocate_block();
        label(again.has_value() && *again == blockno ? "OK" : "FAIL", "free_block -> reallocate");
        if (again.has_value())
        {
            FileSystem::free_block(*again);
        }
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
        vga::vga->printf("brk before=0x%016lx  returned=0x%016lx  now=0x%016lx\n",
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
        vga::vga->printf("p1=0x%016lx p2=0x%016lx p3=0x%016lx p4=0x%016lx reuse=%s\n",
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
    Syscall::init();

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

    // 仮想 → 物理の変換方法はカーネル側の関心事なので、ドライバには関数として渡す。
    // (キャプチャなしラムダは関数ポインタへ暗黙変換される)
    const auto resolve_physical = [](const void *p) -> std::optional<uint64_t>
    {
        if (vmm::vmm_ptr == nullptr)
        {
            return std::nullopt; // VMM 未初期化: 変換できない
        }
        // virtual_to_physical() 自体も未マップなら nullopt を返すので、そのまま伝播させる
        return vmm::vmm_ptr->virtual_to_physical(reinterpret_cast<uint64_t>(p));
    };

    if (VirtIOBlock::initialize(resolve_physical))
    {

        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->puts("[VIRTIO] VirtIO Block Device initialized successfully\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);

        // バッファキャッシュ層をこのドライバの上に載せる。
        // 以降ブロックアクセスは BufferCache 経由で行い、上位層 (inode) からは
        // どのドライバかを見えなくする。
        // コンストラクタが初期化を行う。kernel_main は返らないので、この
        // インスタンスは以降の全アクセスより長生きする。
        BufferCache::Manager buffer_cache(BufferCache::BlockDevice{
            .read_block  = &VirtIOBlock::read_block,
            .write_block = &VirtIOBlock::write_block,
        });
        if (buffer_cache.valid())
        {
            vga::vga->set_color(Color::LightGreen, Color::Black);
            vga::vga->puts("[BCACHE] ");
            vga::vga->set_color(Color::LightGrey, Color::Black);
            vga::vga->printf("initialized: %llu buffers x %llu bytes\n",
                             static_cast<unsigned long long>(NBUF),
                             static_cast<unsigned long long>(BLOCK_SIZE));

            // 1回目: キャッシュに無いのでデバイスを叩く (miss)
            // BufferRef はスコープを抜けるときに自動で release() される
            if (BufferCache::BufferRef block0 = BufferCache::acquire(0))
            {
                vga::vga->set_color(Color::LightGreen, Color::Black);
                vga::vga->puts("[BCACHE] ");
                vga::vga->set_color(Color::LightGrey, Color::Black);
                vga::vga->puts("block 0:\n");
                vga::vga->set_color(Color::LightCyan, Color::Black);
                hexdump(block0->data, BLOCK_SIZE);
                vga::vga->set_color(Color::LightGrey, Color::Black);
                block0.reset(); // 明示的に手放す (以降のスコープでも自動解放される)

                // 2回目: 同じブロックなのでデバイスを叩かない (hit)
                {
                    BufferCache::BufferRef again = BufferCache::acquire(0);
                }

                // RAII が効いていることの確認:
                // release 漏れがあれば NBUF 個を超えた時点で acquire が失敗する
                bool no_leak = true;
                for (int i = 0; i < NBUF * 4; i++)
                {
                    BufferCache::BufferRef probe = BufferCache::acquire(0);
                    if (!probe)
                    {
                        no_leak = false;
                        break;
                    }
                }
                vga::vga->set_color(Color::LightGreen, Color::Black);
                vga::vga->puts("[BCACHE] ");
                vga::vga->set_color(Color::LightGrey, Color::Black);
                vga::vga->printf("BufferRef leak test: %s\n", no_leak ? "OK" : "LEAKED");

                const BufferCache::Statistics stats = BufferCache::statistics();
                vga::vga->set_color(Color::LightGreen, Color::Black);
                vga::vga->puts("[BCACHE] ");
                vga::vga->set_color(Color::LightGrey, Color::Black);
                vga::vga->printf("hits=%llu misses=%llu evictions=%llu writebacks=%llu\n",
                                 static_cast<unsigned long long>(stats.hits),
                                 static_cast<unsigned long long>(stats.misses),
                                 static_cast<unsigned long long>(stats.evictions),
                                 static_cast<unsigned long long>(stats.writebacks));

                // ─── ファイルシステム層 ───
                // 全ブロック数はデバイスの容量から取る (fs.img のサイズに追従する)。
                // 出力先は注入で渡す。ここを &serial::serial にすれば画面に出さずに
                // シリアルだけに出せる。
                const uint64_t total_blocks = VirtIOBlock::capacity();
                vga::vga->set_color(Color::LightGreen, Color::Black);
                vga::vga->puts("[FS]   ");
                vga::vga->set_color(Color::LightGrey, Color::Black);
                vga::vga->printf("device capacity: %llu blocks (%llu bytes)\n",
                                 static_cast<unsigned long long>(total_blocks),
                                 static_cast<unsigned long long>(total_blocks * BLOCK_SIZE));

                // ファイルシステムには BufferCache そのものではなく、
                // IBlockStore の実装として渡す (FileSystem は BufferCache を知らない)。
                static_assert(FSBLOCK_SIZE == BLOCK_SIZE, "block size mismatch between FS and cache");
                BufferCache::BlockStore block_store;

                FileSystem::Manager file_system(static_cast<uint32_t>(total_blocks), &block_store, vga::vga);
                if (file_system.valid())
                {
                    const SuperBlock &sb = FileSystem::superblock();
                    vga::vga->set_color(Color::LightGreen, Color::Black);
                    vga::vga->puts("[FS]   ");
                    vga::vga->set_color(Color::LightGrey, Color::Black);
                    vga::vga->printf("ready: ninodes=%llu nblocks=%llu inodestart=%llu bmapstart=%llu\n",
                                     static_cast<unsigned long long>(sb.ninodes),
                                     static_cast<unsigned long long>(sb.nblocks),
                                     static_cast<unsigned long long>(sb.inodestart),
                                     static_cast<unsigned long long>(sb.bmapstart));

                    vga::vga->set_color(Color::LightCyan, Color::Black);
                    filesystem_read_write_test(block_store, vga::vga);
                    vga::vga->set_color(Color::LightGrey, Color::Black);
                }
                else
                {
                    vga::vga->set_color(Color::LightRed, Color::Black);
                    vga::vga->puts("[FS]   filesystem initialization failed\n");
                    vga::vga->set_color(Color::LightGrey, Color::Black);
                }
            }
            else
            {
                vga::vga->set_color(Color::LightRed, Color::Black);
                vga::vga->puts("[BCACHE] acquire(0) failed\n");
                vga::vga->set_color(Color::LightGrey, Color::Black);
            }
        }
        else
        {
            vga::vga->set_color(Color::LightRed, Color::Black);
            vga::vga->puts("[BCACHE] initialization failed\n");
            vga::vga->set_color(Color::LightGrey, Color::Black);
        }
    }
    else
    {
        vga::vga->set_color(Color::LightRed, Color::Black);
        vga::vga->puts("[VIRTIO] VirtIO Block Device initialization failed\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
    }


    while (1)
    {
        asm volatile("hlt");
    }
}
