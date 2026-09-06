#include "syscall.hpp"
#include "vga.hpp"
#include "keyboard.hpp"
#include "gdt.hpp"
#include "process.hpp"
#include "file.hpp"
#include "path.hpp"

extern "C" void syscall_entry();
extern "C" uint64_t rdmsr(uint32_t msr);
extern "C" void wrmsr(uint32_t msr, uint64_t value);


// ─── fd → File* の解決 ──────────────────────────────────────────
namespace
{

using FileTable = std::array<FileSystem::File *, NUM_FILE_DESCRIPTORS>;

// プロセス文脈が無いときに使う fd 表。
// kernel_main から sys_open() などを直接呼べるようにするためのもので、
// プロセスが走っている間 (current_process() != nullptr) は使われない。
FileTable kernel_file_table{};

FileTable &current_file_table()
{
    Process *process = process::current_process();
    return process != nullptr ? process->ofile : kernel_file_table;
}

FileSystem::File *fd_to_file(int fd)
{
    FileTable &table = current_file_table();
    if (fd < 0 || fd >= static_cast<int>(table.size()))
    {
        return nullptr;
    }
    return table[fd];
}

// 空いている fd を探して file を割り当てる。失敗時は -1。
int fd_allocate(FileSystem::File *file)
{
    FileTable &table = current_file_table();
    for (int fd = 0; fd < static_cast<int>(table.size()); fd++)
    {
        if (table[fd] == nullptr)
        {
            table[fd] = file;
            return fd;
        }
    }
    return -1;
}

[[noreturn]] void sys_exit(uint64_t code)
{
    vga::vga->set_color(Color::Yellow, Color::Black);
    vga::vga->printf("\n[SYS]  exit(%u) called\n", (unsigned)code);
    vga::vga->set_color(Color::LightGrey, Color::Black);
    // フェーズ6ではプロセス連携をせず、ここで停止
    while (1)
        asm volatile("hlt");
}

} // namespace

namespace Syscall
{

int sys_open(const char *path, int flags)
{
    if (path == nullptr)
    {
        return -1;
    }

    FileSystem::InodeRef inode = FileSystem::namei(path);
    if (!inode)
    {
        if (!(flags & O_CREATE))
        {
            return -1; // 存在せず、作成も指定されていない
        }
        inode = FileSystem::create_file(path, InodeType::kFile);
        if (!inode)
        {
            return -1;
        }
    }

    const bool writable = (flags & (O_WRONLY | O_RDWR)) != 0;

    // ディレクトリの中身は dirlink() 経由でしか触らせない (書き込みでは開けない)
    if (inode->type == InodeType::kDirectory && writable)
    {
        return -1;
    }

    if ((flags & O_TRUNC) && inode->type == InodeType::kFile)
    {
        FileSystem::itrunc(inode);
    }

    FileSystem::File *file = FileSystem::file_allocate();
    if (file == nullptr)
    {
        return -1; // ファイルテーブル枯渇
    }

    const int fd = fd_allocate(file);
    if (fd < 0)
    {
        FileSystem::file_close(file);
        return -1; // fd 表が埋まっている
    }

    file->type     = FileSystem::FileType::kInode;
    file->inode    = static_cast<FileSystem::InodeRef &&>(inode);
    file->offset   = 0;
    file->readable = !(flags & O_WRONLY);
    file->writable = writable;

    return fd;
}

int sys_close(int fd)
{
    FileTable &table = current_file_table();
    if (fd < 0 || fd >= static_cast<int>(table.size()) || table[fd] == nullptr)
    {
        return -1;
    }
    FileSystem::file_close(table[fd]);
    table[fd] = nullptr;
    return 0;
}

int sys_read(int fd, void *buffer, uint32_t n)
{
    FileSystem::File *file = fd_to_file(fd);
    if (file == nullptr || buffer == nullptr)
    {
        return -1;
    }
    // readable の判定は file_read() が持つ (コンソールでも同じ規則で弾く)
    return FileSystem::file_read(file, static_cast<uint8_t *>(buffer), n);
}

int sys_write(int fd, const void *buffer, uint32_t n)
{
    FileSystem::File *file = fd_to_file(fd);
    if (file == nullptr || buffer == nullptr)
    {
        return -1;
    }
    return FileSystem::file_write(file, static_cast<const uint8_t *>(buffer), n);
}

extern "C" int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t /*a4*/, uint64_t /*a5*/)
{
    switch (num)
    {
        case SyscallNo::kRead:
            return sys_read(static_cast<int>(a1), reinterpret_cast<void *>(a2), static_cast<uint32_t>(a3));
        case SyscallNo::kWrite:
            return sys_write(static_cast<int>(a1), reinterpret_cast<const void *>(a2), static_cast<uint32_t>(a3));
        case SyscallNo::kExit:
            sys_exit(a1); // 戻らない
        case SyscallNo::kOpen:
            return sys_open(reinterpret_cast<const char *>(a1), static_cast<int>(a2));
        case SyscallNo::kClose:
            return sys_close(static_cast<int>(a1));
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
