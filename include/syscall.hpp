#pragma once
#include <cstdint>

// Linux x86-64 のシステムコール番号と合わせる
namespace SyscallNo
{
    constexpr int kRead  = 0;
    constexpr int kWrite = 1;
    constexpr int kExit  = 60;
}

// MSR アドレス
namespace MSR {
    constexpr uint32_t EFER  = 0xC0000080;
    constexpr uint32_t STAR  = 0xC0000081;
    constexpr uint32_t LSTAR = 0xC0000082;
    constexpr uint32_t FMASK = 0xC0000084;
}



namespace syscall {
    // MSR を設定して syscall/sysret を有効化
    void init();

    // ディスパッチャ (syscall_entry.asm から呼ばれる)
    // 戻り値が RAX に入る
    extern "C" uint64_t syscall_dispatch(
        uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
        uint64_t a4, uint64_t a5);
}
