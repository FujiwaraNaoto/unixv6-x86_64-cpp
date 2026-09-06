#include "syscall.hpp"
#include "vga.hpp"
#include "keyboard.hpp"
#include "gdt.hpp"
#include "process.hpp"
#include "file.hpp"

extern "C" void syscall_entry();
extern "C" uint64_t rdmsr(uint32_t msr);
extern "C" void wrmsr(uint32_t msr, uint64_t value);


// ─── fd → File* の解決 ──────────────────────────────────────────
namespace
{

FileSystem::File *fd_to_file(int fd)
{
    Process *process = process::current();
    if (process == nullptr || fd < 0 || fd >= static_cast<int>(process->ofile.size()))
    {
        return nullptr;
    }
    return process->ofile[fd];
}

// 空いている fd を探して file を割り当てる。失敗時は -1。
int fd_allocate(FileSystem::File *file)
{
    Process *process = process::current();
    if (process == nullptr)
    {
        return -1;
    }

    for (int fd = 0; fd < process->ofile.size(); fd++)
    {
        if (process->ofile[fd] == nullptr)
        {
            process->ofile[fd] = file;
            return fd;
        }
    }
    return -1;
}

} // namespace

static int64_t sys_read(uint64_t fd, uint64_t buffer, uint64_t len)
{
    FileSystem::File *file = fd_to_file(static_cast<int>(fd));
    if(file == nullptr || !file->readable)
    {
        return static_cast<int64_t>(-1);
    }
    return FileSystem::file_read(file, reinterpret_cast<uint8_t *>(buffer), static_cast<uint32_t>(len));
}

static int64_t sys_write(uint64_t fd, uint64_t buffer, uint64_t len)
{

     FileSystem::File *file = fd_to_file(static_cast<int>(fd));
    if (file == nullptr)
    {
        return static_cast<int64_t>(-1);
    }
    return FileSystem::file_write(file,
                                  reinterpret_cast<const uint8_t *>(buffer),
                                  static_cast<uint32_t>(len));

}

static int64_t sys_exit(uint64_t code)
{
    vga::vga->set_color(Color::Yellow, Color::Black);
    vga::vga->printf("\n[SYS]  exit(%u) called\n", (unsigned)code);
    vga::vga->set_color(Color::LightGrey, Color::Black);
    // フェーズ6ではプロセス連携をせず、ここで停止
    while (1)
        asm volatile("hlt");
    return static_cast<int64_t>(0); // never reached
}

namespace Syscall
{

extern "C" int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t /*a4*/, uint64_t /*a5*/)
{
    switch (num)
    {
        case SyscallNo::kRead:
            return sys_read(a1, a2, a3);
        case SyscallNo::kWrite:
            return sys_write(a1, a2, a3);
        case SyscallNo::kExit:
            return sys_exit(a1);
        default:
            vga::vga->set_color(Color::LightRed, Color::Black);
            vga::vga->printf("[SYS]  unknown syscall %u\n", (unsigned)num);
            vga::vga->set_color(Color::LightGrey, Color::Black);
            return static_cast<int64_t>(-1);
    }
}

void init()
{
    // 1. EFER.SCE (System Call Enable) を立てる
    uint64_t efer = rdmsr(MSR::EFER);
    efer |= 1; // SCE = bit0
    wrmsr(MSR::EFER, efer);

    // 2. STAR: セグメントセレクタを設定
    //    上位32bit [63:48]=ユーザー, [47:32]=カーネル
    //    syscall 時: CS=STAR[47:32],    SS=STAR[47:32]+8
    //    sysret 時: CS=STAR[63:48]+16, SS=STAR[63:48]+8 (どちらも RPL=3 が強制される)
    //
    //    CPU が「+8」「+16」で勝手に隣を拾う仕様なので、STAR に入れるのはセレクタそのもの
    //    ではなく「基準値」である点に注意。[63:48] の 0x10 はカーネルDataと同じ値になるが
    //    偶然の一致で、意味は「ユーザーDataの1つ前」。
    // NOTE: kSysretBase の値は GDT のレイアウトから導出される従属値なので、GDT の順序を変更すればこの値も変わる
    constexpr uint64_t kSyscallBase = gdt::SegmentSelector::kKernelCode;                // 0x08
    constexpr uint64_t kSysretBase  = (gdt::SegmentSelector::kUserData & ~0x03) - 0x08; // 0x10

    // 上の +8/+16 が実際に gdt.hpp のセレクタに着地することを保証する
    // (GDT の並びを崩したらリンク前に落ちる)
    static_assert(kSyscallBase + 0x08 == gdt::SegmentSelector::kKernelData,
                  "syscall は SS=CS+8 を要求する: Kernel Data は Kernel Code の直後でなければならない");
    static_assert(((kSysretBase + 0x08) | 0x03) == gdt::SegmentSelector::kUserData, "sysret は SS=base+8 を要求する");
    static_assert(((kSysretBase + 0x10) | 0x03) == gdt::SegmentSelector::kUserCode,
                  "sysret は CS=base+16 を要求する: User Code は User Data の直後でなければならない");

    uint64_t star = (kSyscallBase << 32)   // カーネルセグメント
                    | (kSysretBase << 48); // ユーザーセグメント
    wrmsr(MSR::STAR, star);

    // 3. LSTAR: syscall 時のジャンプ先
    wrmsr(MSR::LSTAR, reinterpret_cast<uint64_t>(syscall_entry));

    // 4. FMASK: syscall 時にクリアするRFLAGSビット (割り込みフラグIFを落とす)
    wrmsr(MSR::FMASK, 1 << 9); // IF = bit9

    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[SYS]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("syscall enabled  LSTAR=0x%lx\n", static_cast<unsigned long>(reinterpret_cast<uint64_t>(syscall_entry)));
}

} // namespace Syscall
