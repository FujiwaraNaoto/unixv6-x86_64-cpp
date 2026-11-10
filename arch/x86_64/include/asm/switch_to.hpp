#include "trapframe.hpp"



struct Process;

extern "C" void switch_context(ProcessContext **old_ctx, ProcessContext *new_ctx);


void arch_setup_kernel_thread(Process *proc, void (*entry)());

// ring3からfork
void arch_setup_user_fork(Process *parent, Process *child);


int arch_fork_kernel(Process *parent, Process *child);


void arch_prepare_switch(Process *next);
