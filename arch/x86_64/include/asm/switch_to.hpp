#ifndef ASM_SWITCH_TO_HPP
#define ASM_SWITCH_TO_HPP
#include "asm/trapframe.hpp"


struct Process;

extern "C" void switch_context(ProcessContext **old_ctx, ProcessContext *new_ctx);


void arch_setup_kernel_thread(Process *proc, void (*entry)());

// ring3からfork
void arch_setup_user_fork(Process *parent, Process *child);


int arch_fork_kernel(Process *parent, Process *child);


// 今のコンテキストを *save_to に保存して next へ切り替える (CR3 / カーネルスタック / レジスタ)。
void arch_switch(ProcessContext **save_to, Process *next);

void arch_switch_to_scheduler(ProcessContext **save_to, ProcessContext *scheduler_context);

#endif // ASM_SWITCH_TO_HPP
