#include <cstdint>
#include <array>
#include "process.hpp"
#include "heap.hpp"

extern "C" void switch_context(ProcessContext **old_ctx, ProcessContext *new_ctx);

namespace process
{
static std::array<Process, MAX_PROCESSES> process_table_;
static int next_pid_                      = 1;
static Process *current_proc_             = nullptr;
static ProcessContext *scheduler_context_ = nullptr;
static heap::Heap *heap_ptr_              = nullptr;

void initialize(heap::Heap *heap_ptr)
{
    for (size_t idx = 0; idx < process_table_.size(); ++idx)
    {
        process_table_[idx].state = ProcessState::Unused;
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
    while (1)
        asm volatile("hlt");
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

    uint8_t *stack_top = stack + KERNEL_STACK_SIZE;
    stack_top -= sizeof(ProcessContext);
    proc->context      = reinterpret_cast<ProcessContext *>(stack_top);
    proc->context->r15 = 0;
    proc->context->r14 = 0;
    proc->context->r13 = 0;
    proc->context->r12 = 0;
    proc->context->rbx = 0;
    proc->context->rbp = 0;
    proc->context->rip = reinterpret_cast<uint64_t>(trampoline);
    return proc;
}

// Switch to the next process in the “Ready” state using round-robin scheduling
void yield()
{
    Process *prev_proc = current_proc_;
    int start          = (prev_proc) ? (prev_proc - &process_table_[0]) : -1;

    for (std::size_t off = 1; off <= process_table_.size(); ++off)
    {

        std::size_t idx = (start + off) % process_table_.size();
        if (process_table_[idx].state == ProcessState::Ready)
        {
            current_proc_        = &process_table_[idx];
            current_proc_->state = ProcessState::Running;
            if (prev_proc && prev_proc->state == ProcessState::Running)
            {
                prev_proc->state = ProcessState::Ready;
            }
            ProcessContext **old_ctx = (prev_proc) ? &prev_proc->context : nullptr;
            static ProcessContext *dummy; // A dummy context to pass when `prev` is `nullptr`

            switch_context((old_ctx) ? old_ctx : &dummy, current_proc_->context);
            return;
        }
    }
    // This point is reached when there are no executable processes (=Ready Status)
    asm volatile("hlt");
}

} // namespace process
