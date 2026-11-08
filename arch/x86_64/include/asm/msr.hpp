#pragma once
#include <cstdint>


// MSR 番号(識別子)
// see https://wiki.osdev.org/SYSENTER#AMD:_SYSCALL/SYSRET
namespace MSR
{
constexpr uint32_t EFER  = 0xC0000080; // SCE (System Call Enable) bit, which enables syscall/sysret
constexpr uint32_t STAR  = 0xC0000081; // Segment selectors for syscall/sysret
constexpr uint32_t LSTAR = 0xC0000082; // Long mode syscall target. Address of the syscall entry point.
constexpr uint32_t FMASK =
    0xC0000084; // Flags mask for syscall. A bitmask of RFLAGS bits to clear when syscall is executed.
} // namespace MSR

extern "C" void syscall_entry();
extern "C" uint64_t rdmsr(uint32_t msr);
extern "C" void wrmsr(uint32_t msr, uint64_t value);
