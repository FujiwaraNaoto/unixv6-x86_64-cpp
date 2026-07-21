#include <cstdint>
#include <array>
#include "process.hpp"
#include "heap.hpp"
#include "gdt.hpp"

extern "C" void switch_context(ProcessContext **old_ctx, ProcessContext *new_ctx);

namespace
{


extern "C" void fork_child_return();

} // namespace


namespace process
{
static std::array<Process, MAX_PROCESSES> process_table_;
static int next_pid_                      = 1;
static Process *current_proc_             = nullptr;
static ProcessContext *scheduler_context_ = nullptr;
static heap::Heap *heap_ptr_              = nullptr;

ProcessManager::ProcessManager(heap::Heap *heap_ptr)
{
    for (size_t idx = 0; idx < process_table_.size(); ++idx)
    {
        process_table_[idx].state         = ProcessState::Unused;
        process_table_[idx].sleep_channel = nullptr;
    }
    current_proc_ = nullptr;
    heap_ptr_     = heap_ptr;
}


// A function called when a new process is executed for the first time
// ProcessContext の rip に設定される
static void trampoline()
{
    Process *proc = current_proc_;
    if (proc && proc->entry)
    {
        proc->entry();
    }
    proc->state = ProcessState::Zombie;
    yield();

    // unreachable, but just in case, halt the CPU to avoid unexpected behavior. The process will be in Zombie state,
    // and won't be scheduled again, but it will still consume CPU time if we don't halt here.
    while (1)
        asm volatile("hlt");
}

static void schedule(Process *prev_proc)
{
    size_t start = prev_proc ? (prev_proc - &process_table_[0]) : -1;
    for (size_t off = 1; off <= process_table_.size(); ++off)
    {
        int idx = (start + off) % process_table_.size();
        if (process_table_[idx].state != ProcessState::Ready)
            continue;


        current_proc_        = &process_table_[idx];
        current_proc_->state = ProcessState::Running;

        // 選ばれたのが今動いていたプロセス自身なら切り替え不要。
        // ここで switch_context(&self->context, self->context) をやると
        // 既に破棄された古いコンテキストへ ret して暴走するため、そのまま続行する。
        if (current_proc_ == prev_proc)
            return;

        if (current_proc_->pml4 != 0)
        {
            vmm::vmm_ptr->switch_address_space(current_proc_->pml4);
        }

        // set TSS's RSP0 to the top of the current process's kernel stack, so that when an interrupt occurs, the
        // CPU will switch to this stack.
        gdt::set_kernel_stack(current_proc_->kernel_stack + KERNEL_STACK_SIZE);

        // prev がプロセスならそのコンテキストへ、kernel_main (スケジューラ) からの
        // 呼び出しなら scheduler_context_ へ保存する。後者は Ready が尽きたときの
        // 復帰先になる
        ProcessContext **old_ctx = prev_proc ? &prev_proc->context
                                             : reinterpret_cast<ProcessContext **>(&scheduler_context_);

        switch_context(old_ctx, current_proc_->context);
        return;
    }

    if (prev_proc)
    {
        current_proc_ = nullptr;
        switch_context(&prev_proc->context, scheduler_context_);
    }


    // if (!prev_proc || prev_proc->state != ProcessState::Running)
    // {
    //     asm volatile("sti"); //割り込みは受け付ける
    //     asm volatile("hlt");
    // }
}

Process *create_process(EntryPoint entry, const char *name)
{
    Process *proc = nullptr;
    for (size_t idx = 0; idx < process_table_.size(); ++idx)
    {
        if (process_table_[idx].state == ProcessState::Unused)
        {
            proc = &process_table_[idx];
            break;
        }
    }

    if (proc == nullptr)
    {
        return nullptr; // The process table is full.
    }
    proc->pid   = next_pid_++;
    proc->state = ProcessState::Embryo;
    proc->entry = entry;
    proc->name  = name; // kstring が容量超過分を切り捨てて null 終端する

    uint8_t *stack = static_cast<uint8_t *>(heap_ptr_->alloc(KERNEL_STACK_SIZE));
    if (stack == nullptr)
    {
        proc->state = ProcessState::Unused; // スタック確保失敗した
        return nullptr;
    }
    proc->kernel_stack = reinterpret_cast<uint64_t>(stack);

    // allocate a new page table for each processes
    proc->pml4 = vmm::vmm_ptr->create_address_space();
    if (not proc->pml4)
    {
        proc->state = ProcessState::Unused; // ページテーブル確保失敗した
        return nullptr;
    }

    uint8_t *stack_top = stack + KERNEL_STACK_SIZE;
    stack_top -= sizeof(ProcessContext);
    proc->context       = reinterpret_cast<ProcessContext *>(stack_top);
    proc->context->r15  = 0;
    proc->context->r14  = 0;
    proc->context->r13  = 0;
    proc->context->r12  = 0;
    proc->context->rbx  = 0;
    proc->context->rbp  = 0;
    proc->context->rip  = reinterpret_cast<uint64_t>(trampoline);
    proc->sleep_channel = nullptr; // 初期状態では起きている

    proc->state = ProcessState::Ready; // 構築完了。これでスケジューラが拾えるようになる
    vga::vga->printf("[PROCESS] Created process %s (pid=%u)\n", proc->name.c_str(), static_cast<unsigned>(proc->pid));
    return proc;
}

// Switch to the next process in the “Ready” state using round-robin scheduling
void yield()
{
    Process *prev_proc = current_proc_;
    if (prev_proc && prev_proc->state == ProcessState::Running)
    {
        prev_proc->state = ProcessState::Ready; // Running -> Ready
    }
    schedule(prev_proc);
}


// ─── sleep ────────────────────────────────────────────────────────
//
// 現プロセスを chan を理由に Sleeping にして、別プロセスへ切り替える。
// wakeup(chan) で Ready に戻され、再スケジュールされると
// swtch がこの続きに戻ってくる (= sleep から return する)。
//
void sleep(void *chan)
{
    Process *p = current_proc_;
    if (!p)
        return; // カーネル初期文脈では sleep しない
    // lost wakeup 対策: 状態変更中は割り込みを止める
    asm volatile("cli");

    p->sleep_channel = chan;
    p->state         = ProcessState::Sleeping;

    schedule(p); // 別プロセスへ。ここで一旦戻らない

    // ─── wakeup されて再開したらここに戻る ───
    p->sleep_channel = nullptr;
    asm volatile("sti");
}

// ─── wakeup ───────────────────────────────────────────────────────
//
// chan を理由に Sleeping な全プロセスを Ready に戻す。
// 実際に走り出すのは次のスケジュールのタイミング。
//
void wakeup(void *chan)
{
    for (size_t i = 0; i < process_table_.size(); ++i)
    {
        if (process_table_[i].state == ProcessState::Sleeping && process_table_[i].sleep_channel == chan)
        {
            process_table_[i].state = ProcessState::Ready;
        }
    }
}

// zombie になったプロセスのコンテキストを破棄して、別プロセスへ切り替える。
static void schedule_from_zombie(ProcessContext **discard_context)
{
    Process *prev_proc = current_proc_;
    int start          = prev_proc ? (prev_proc - &process_table_[0]) : -1;
    for (size_t off = 1; off <= process_table_.size(); ++off)
    {
        int idx = (start + off) % process_table_.size();
        if (process_table_[idx].state != ProcessState::Ready)
            continue;

        current_proc_        = &process_table_[idx];
        current_proc_->state = ProcessState::Running;
        if (current_proc_->pml4 != 0)
        {
            vmm::vmm_ptr->switch_address_space(current_proc_->pml4);
        }
        gdt::set_kernel_stack(current_proc_->kernel_stack + KERNEL_STACK_SIZE);
        switch_context(discard_context, current_proc_->context);
        return;
    }

    // Ready なプロセスがない場合は、カーネル初期文脈に戻す
    current_proc_ = nullptr;
    switch_context(discard_context, scheduler_context_);
}


[[noreturn]] void exit(int status)
{
    Process *p = current_proc_;
    if (!p)
        return; // カーネル初期文脈では exit しない

    asm volatile("cli"); // 状態変更中は割り込みを止める

    p->exit_status = status;
    p->state       = ProcessState::Zombie;

    if (p->parent)
    {
        // 親プロセスが wait() している場合に備えて wakeup する -> なぜ?
        wakeup(p->parent);
    }
    //二度と戻らないので、スケジューラに制御を渡す
    static ProcessContext discard_context;
    schedule_from_zombie(&discard_context);

    // unreachable, but just in case, halt the CPU to avoid unexpected behavior. The process will be in Zombie state,
    while (1)
        asm volatile("hlt");
}

void free_process_resources(Process *proc)
{
    if (proc->kernel_stack)
    {
        process::heap_ptr_->free(reinterpret_cast<void *>(proc->kernel_stack));
        proc->kernel_stack = 0;
    }

    proc->pml4          = 0;
    proc->parent        = nullptr;
    proc->sleep_channel = nullptr;
    proc->exit_status   = static_cast<int>(ProcessState::Unused);
}

int wait(int *exit_code_out)
{
    Process *p = current_proc_;

    while (1)
    {
        bool has_child = false;

        for (size_t i = 0; i < process_table_.size(); ++i)
        {
            Process *child = &process_table_[i];
            if (child->parent != p)
                continue;
            has_child = true;
            if (child->state == ProcessState::Zombie)
            {
                // zombieになった子プロセスを回収する
                int exit_code = child->exit_status;
                if (exit_code_out)
                {
                    *exit_code_out = exit_code;
                }

                int pid = child->pid;
                free_process_resources(child);

                return pid;
            }
        }

        if (!has_child)
        {
            return -1;
        }

        // when child processes exist but none of them are zombies, the parent process should sleep until a child
        // process exits exit() will wake up the parent process when a child process exits, so the parent process can
        // check again if any child processes are zombies.
        sleep(p);
    }
}


int fork()
{
    Process *parent = current_proc_;
    Process *child  = nullptr;
    for (size_t i = 0; i < process_table_.size(); ++i)
    {
        if (process_table_[i].state == ProcessState::Unused)
        {
            child = &process_table_[i];
            break;
        }
    }
    if (!child)
    {
        return -1; // process table is full
    }

    //子供に親の情報をコピーする
    child->pid           = next_pid_++;
    child->state         = ProcessState::Embryo;
    child->entry         = parent->entry;
    child->name          = parent->name;
    child->parent        = parent;
    child->entry         = parent->entry;
    child->name          = parent->name;
    child->sleep_channel = nullptr;

    // allocate a new page table for the child process
    uint8_t *child_stack = static_cast<uint8_t *>(heap_ptr_->alloc(KERNEL_STACK_SIZE));
    if (!child_stack)
    {
        child->state = ProcessState::Unused; // スタック確保失敗した
        return -1;
    }
    child->kernel_stack = reinterpret_cast<uint64_t>(child_stack);

    // allocate a new page table for the child process
    child->pml4 = vmm::vmm_ptr->create_address_space();
    if (!child->pml4)
    {
        heap_ptr_->free(reinterpret_cast<void *>(child_stack));
        child->state = ProcessState::Unused; // ページテーブル確保失敗
        return -1;
    }

    // copy the parent's kernel stack to the child's kernel stack
    // (make the child process's kernel stack identical to the parent's kernel stack)
    for (size_t i = 0; i < KERNEL_STACK_SIZE; i++)
    {
        child_stack[i] = reinterpret_cast<uint8_t *>(parent->kernel_stack)[i];
    }

    // set the child's context to be identical to the parent's context
    uint64_t offset = reinterpret_cast<uint64_t>(child_stack) - parent->kernel_stack;
    child->context  = reinterpret_cast<ProcessContext *>(reinterpret_cast<uint64_t>(parent->context) + offset);

    child->context->rip = reinterpret_cast<uint64_t>(fork_child_return);

    child->state = ProcessState::Ready;
    return child->pid; // return the child's pid to the parent process
}

} // namespace process
