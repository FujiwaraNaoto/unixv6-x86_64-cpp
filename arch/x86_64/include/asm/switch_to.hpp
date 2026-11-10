#include "trapframe.hpp"



struct Process;

extern "C" void switch_context(ProcessContext **old_ctx, ProcessContext *new_ctx);


void arch_setup_kernel_thread(Process *proc, void (*entry)());

// ring3からfork
void arch_setup_user_fork(Process *parent, Process *child);


int arch_fork_kernel(Process *parent, Process *child);


void arch_prepare_switch(Process *next);

// 今のコンテキストを *save_to に保存して next へ切り替える (CR3 / カーネルスタック / レジスタ)。
void arch_switch(ProcessContext **save_to, Process *next);
