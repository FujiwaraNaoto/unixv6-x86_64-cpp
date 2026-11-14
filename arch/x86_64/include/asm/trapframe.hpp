#ifndef ASM_TRAPFRAME_HPP
#define ASM_TRAPFRAME_HPP

#include <cstddef>
#include <cstdint>

// syscall の入口 (syscall/syscall_entry.asm) がカーネルスタックに積む、
// リング3のレジスタ一式。並びは push している順の逆 (rsp が指す側が r15)。
// fork した子は、このフレームの rax だけを 0 に変えたコピーから復帰する。
struct [[gnu::packed]] TrapFrame
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11; // syscall 命令が入れたユーザーの RFLAGS (sysret が使う)
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rax; // syscall 番号。戻るときは戻り値
    uint64_t rcx; // syscall 命令が入れたユーザーの RIP (sysret が使う)
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t user_rsp; // syscall した時点のユーザーの RSP
};

// syscall_entry.asm がオフセットを直接書いているので、ずれたらここで気づけるようにする
static_assert(sizeof(TrapFrame) == 128, "TrapFrame size must match syscall_entry.asm");
static_assert(offsetof(TrapFrame, rax) == 80, "TF_RAX in syscall_entry.asm");
static_assert(offsetof(TrapFrame, rdi) == 112, "TF_RDI in syscall_entry.asm");
static_assert(offsetof(TrapFrame, user_rsp) == 120, "TF_USER_RSP in syscall_entry.asm");

// Callee-saved register (except rsp)
struct [[gnu::packed]] ProcessContext
{                 // ProcessContext*が指しているアドレスからのオフセット
    uint64_t rbp; // +0
    uint64_t rbx; // +8
    uint64_t r12; // +16
    uint64_t r13; // +24
    uint64_t r14; // +32
    uint64_t r15; // +40
    uint64_t rip; // +48 retが拾う戻りアドレス
};

static_assert(sizeof(ProcessContext) == 56, "6 pop + ret in switch.asm");
static_assert(offsetof(ProcessContext, rbp) == 0, "first pop in switch.asm");
static_assert(offsetof(ProcessContext, r15) == 40, "last pop in switch.asm");
static_assert(offsetof(ProcessContext, rip) == 48, "ret target in switch.asm");


// 構造体のメンバは先に書いたものほどアドレスが低い方に置かれる
struct [[gnu::packed]] UserForkFrame
{
    ProcessContext context; // low addres
    TrapFrame trap_frame;   // high address
};

// switch_context が context を pop し終えたとき、rsp がちょうど trap_frame を指すこと
static_assert(offsetof(UserForkFrame, trap_frame) == sizeof(ProcessContext), "fork_return expects rsp == &trap_frame");

#endif // ASM_TRAPFRAME_HPP
