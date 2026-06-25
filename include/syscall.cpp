#include "syscall.hpp"
#include "vga.hpp"
#include "keyboard.hpp"

extern "C" void syscall_entry();
extern "C" uint64_t rdmsr(uint32_t msr);
extern "C" void wrmsr(uint32_t msr, uint64_t value);

static long sys_read(uint64_t fd, uint64_t buf, uint64_t len)
{
    if (fd != 0)
        return static_cast<long>(-1); // stdin のみ
    char *p    = reinterpret_cast<char *>(buf);
    size_t idx = 0;
    while (idx < len)
    {
        while (!keyboard::has_input())
        {
            // syscall は FMASK で IF=0 (割り込み禁止) で入るため、ここで sti して
            // キーボード割り込みを受けられるようにする。sti の直後に hlt を置くと
            // sti から1命令ぶん割り込みが遅延する x86 の仕様により、check と hlt の
            // 間に来た入力も取りこぼさずに wake できる (lost-wakeup 回避)。
            asm volatile("sti; hlt");
        }
        char c = keyboard::getchar();
        if (c == 0)
            break; // no more input
        p[idx] = c;
        idx++;
        if (c == '\n')
            break; // stop reading after newline
    }
    return static_cast<long>(idx);
}

static long sys_write(uint64_t fd, uint64_t buf, uint64_t len)
{
    if (fd != 1 && fd != 2)
        return static_cast<long>(-1); // stdout/stderr のみ
    const char *p = reinterpret_cast<const char *>(buf);
    for (uint64_t i = 0; i < len; i++)
        vga::vga->putchar(p[i]);
    return static_cast<long>(len);
}

static long sys_exit(uint64_t code)
{
    vga::vga->set_color(Color::Yellow, Color::Black);
    vga::vga->printf("\n[SYS]  exit(%u) called\n", (unsigned)code);
    vga::vga->set_color(Color::LightGrey, Color::Black);
    // フェーズ6ではプロセス連携をせず、ここで停止
    while (1)
        asm volatile("hlt");
    return 0;
}

namespace syscall
{

extern "C" long syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t /*a4*/, uint64_t /*a5*/)
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
            return static_cast<long>(-1);
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
    //    カーネルCS=0x08, ユーザーCS base=0x10 (今は仮)
    //    syscret 時: CS=STAR[63:48]+16, SS=STAR[63:48]+8
    //    syscall 時: CS=STAR[47:32],    SS=STAR[47:32]+8
    uint64_t star = (static_cast<uint64_t>(0x08) << 32)    // カーネルセグメント
                    | (static_cast<uint64_t>(0x10) << 48); // ユーザーセグメント (フェーズ7で本設定)
    wrmsr(MSR::STAR, star);

    // 3. LSTAR: syscall 時のジャンプ先
    wrmsr(MSR::LSTAR, reinterpret_cast<uint64_t>(syscall_entry));

    // 4. FMASK: syscall 時にクリアするRFLAGSビット (割り込みフラグIFを落とす)
    wrmsr(MSR::FMASK, 1 << 9); // IF = bit9

    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[SYS]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("syscall enabled  LSTAR=0x%x\n", static_cast<unsigned>(reinterpret_cast<uint64_t>(syscall_entry)));
}

} // namespace syscall
