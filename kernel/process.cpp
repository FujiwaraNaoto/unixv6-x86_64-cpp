#include <cstdint>
#include <array>
#include "process.hpp"
#include "heap.hpp"
#include "vmm.hpp"
#include "address.hpp"
#include "file.hpp"
#include "asm/irqflags.hpp"
#include "asm/switch_to.hpp"


namespace process
{
namespace
{

// Global state for managing processes.
struct ProcessTable
{
    std::array<Process, MAX_PROCESSES> procs;
    int next_pid     = 1;
    heap::Heap *heap = nullptr;
    bool initialized = false;
};

// State that each CPU must have its own copy of.
// TODO: make this an array when going SMP(=Symmetric Multiprocessing).
struct Cpu
{
    Process *current                  = nullptr;
    ProcessContext *scheduler_context = nullptr;
};

ProcessTable ptable;
Cpu cpu;

// 孤児の引き取り先 (pid 1)。create_init_process() が設定する。テストのビルドでは nullptr のまま。
Process *init_proc = nullptr;

} // namespace

void initialize(heap::Heap &heap)
{
    // 2 回目の initialize() はプロセステーブルを黙って消して、走っているプロセスの
    // スロットを Unused に戻してしまうので、進まずに止める。
    if (ptable.initialized)
    {
        while (1)
            cpu_idle();
    }

    for (Process &p : ptable.procs)
    {
        p.state         = ProcessState::Unused;
        p.sleep_channel = nullptr;
    }
    cpu.current        = nullptr;
    ptable.heap        = &heap;
    ptable.initialized = true;
}


namespace
{
// fd 表の File をすべて閉じる。閉じた fd は nullptr にするので、2 回呼んでも問題ない。
void close_all_files(Process *proc)
{
    for (File *&file : proc->files)
    {
        FileSystem::file_close(file);
        file = nullptr;
    }
}

// fd 0/1/2 をコンソールにつなぐ。失敗したら false (開けた分は呼び出し側が free_process_resources で閉じる)。
bool open_console_files(Process *proc)
{
    proc->files[0] = FileSystem::file_open_console(true, false);
    proc->files[1] = FileSystem::file_open_console(false, true);
    if (proc->files[0] == nullptr || proc->files[1] == nullptr)
        return false;
    proc->files[2] = FileSystem::file_duplicate(proc->files[1]);
    return true;
}

// A function called when a new process is executed for the first time
// ProcessContext の rip に設定される
void trampoline()
{
    Process *proc = cpu.current;
    if (proc && proc->entry)
    {
        proc->entry(proc->entry_arg);
    }
    close_all_files(proc);
    proc->state = ProcessState::Zombie;
    yield();

    // unreachable, but just in case, halt the CPU to avoid unexpected behavior. The process will be in Zombie state,
    // and won't be scheduled again, but it will still consume CPU time if we don't halt here.
    while (1)
        cpu_idle();
}

void schedule(Process *prev_proc)
{
    size_t start = prev_proc ? (prev_proc - &ptable.procs[0]) : -1;
    for (size_t off = 1; off <= ptable.procs.size(); ++off)
    {
        int idx = (start + off) % ptable.procs.size();
        if (ptable.procs[idx].state != ProcessState::Runnable)
            continue;


        cpu.current        = &ptable.procs[idx];
        cpu.current->state = ProcessState::Running;

        // 選ばれたのが今動いていたプロセス自身なら切り替え不要。
        // ここで switch_context(&self->context, self->context) をやると
        // 既に破棄された古いコンテキストへ ret して暴走するため、そのまま続行する。
        if (cpu.current == prev_proc)
            return;

        if (cpu.current->pml4)
        {
            vmm::vmm_ptr->switch_address_space(cpu.current->pml4); // step1: CR3を子の空間へ
        }

        arch_prepare_switch(cpu.current);

        // prev がプロセスならそのコンテキストへ、kernel_main (スケジューラ) からの
        // 呼び出しなら cpu.scheduler_context へ保存する。後者は Runnable が尽きたときの
        // 復帰先になる
        ProcessContext **old_ctx = prev_proc ? &prev_proc->context : &cpu.scheduler_context;

        // step2: rspを子のフレームへ
        switch_context(old_ctx, cpu.current->context);
        return;
    }

    if (prev_proc)
    {
        cpu.current = nullptr;
        switch_context(&prev_proc->context, cpu.scheduler_context);
    }
}
} // namespace


// 空きスロットを確保し、カーネルスタックとアドレス空間を用意する。
// 成功すると Embryo 状態の Process を返す。スタックの中身 (context など) は呼び出し側が置く。
// 失敗したら確保済みの資源を解放し、スロットを Unused に戻して nullptr を返す。
Process *allocate_process()
{
    Process *proc = nullptr;
    for (Process &p : ptable.procs)
    {
        if (p.state == ProcessState::Unused)
        {
            proc = &p;
            break;
        }
    }
    if (proc == nullptr)
    {
        return nullptr; // プロセステーブルが満杯
    }

    proc->pid           = ptable.next_pid++;
    proc->state         = ProcessState::Embryo;
    proc->parent        = nullptr;
    proc->sleep_channel = nullptr;
    proc->trap_frame    = nullptr; // リング3から syscall で入ったときだけ設定される
    // 失敗したときに free_process_resources() が「まだ確保していない資源」を
    // 解放しようとしないよう、空の状態から始める。
    proc->kernel_stack = KernelStack{};
    proc->pml4         = PhysicalAddress{};
    proc->files        = {};

    void *stack = ptable.heap->allocate(KERNEL_STACK_SIZE);
    if (stack == nullptr)
    {
        free_process_resources(proc);
        return nullptr;
    }
    proc->kernel_stack = KernelStack{static_cast<uint8_t *>(stack)};

    proc->pml4 = vmm::vmm_ptr->create_address_space();
    if (!proc->pml4)
    {
        free_process_resources(proc); // カーネルスタックもここで解放される
        return nullptr;
    }

    return proc;
}


// リング0で動くカーネルスレッドを作る。entry は trampoline() 経由で呼ばれる。
Process *create_process(EntryPoint entry, const char *name, void *arg)
{
    Process *proc = allocate_process();
    if (proc == nullptr)
    {
        return nullptr; // The process table is full.
    }
    if (!open_console_files(proc))
    {
        free_process_resources(proc);
        return nullptr;
    }
    proc->entry     = entry;
    proc->entry_arg = arg;
    proc->name      = name; // kstring が容量超過分を切り捨てて null 終端する
    arch_setup_kernel_thread(proc,trampoline);
    proc->state   = ProcessState::Runnable; // 構築完了。これでスケジューラが拾えるようになる
    return proc;
}

Process *current_process()
{
    return cpu.current;
}

Process *create_init_process(EntryPoint entry, void *arg)
{
    init_proc = create_process(entry, "init", arg);
    return init_proc;
}

// Switch to the next process in the “Runnable” state using round-robin scheduling
void yield()
{
    Process *prev_proc = cpu.current;
    if (prev_proc && prev_proc->state == ProcessState::Running)
    {
        prev_proc->state = ProcessState::Runnable; // Running -> Runnable
    }
    schedule(prev_proc);
}


// ─── sleep ────────────────────────────────────────────────────────
//
// 現プロセスを channel を理由に Sleeping にして、別プロセスへ切り替える。
// wakeup(channel) で Runnable に戻され、再スケジュールされると
// swtch がこの続きに戻ってくる (= sleep から return する)。
//
void sleep(void *channel)
{
    Process *p = cpu.current;
    if (!p)
        return; // カーネル初期文脈では sleep しない
    // lost wakeup 対策: 状態変更中は割り込みを止める
    irq_disable();

    p->sleep_channel = channel;
    p->state         = ProcessState::Sleeping;

    schedule(p); // 別プロセスへ。ここで一旦戻らない

    // ─── wakeup されて再開したらここに戻る ───
    p->sleep_channel = nullptr;
    irq_enable();
}

// ─── wakeup ───────────────────────────────────────────────────────
//
// channel を理由に Sleeping な全プロセスを Runnable に戻す。
// 実際に走り出すのは次のスケジュールのタイミング。
//
void wakeup(void *channel)
{
    for (size_t i = 0; i < ptable.procs.size(); ++i)
    {
        if (ptable.procs[i].state == ProcessState::Sleeping && ptable.procs[i].sleep_channel == channel)
        {
            ptable.procs[i].state = ProcessState::Runnable;
        }
    }
}

namespace
{
// zombie になったプロセスのコンテキストを破棄して、別プロセスへ切り替える。
void schedule_from_zombie(ProcessContext **discard_context)
{
    Process *prev_proc = cpu.current;
    int start          = prev_proc ? (prev_proc - &ptable.procs[0]) : -1;
    for (size_t off = 1; off <= ptable.procs.size(); ++off)
    {
        int idx = (start + off) % ptable.procs.size();
        if (ptable.procs[idx].state != ProcessState::Runnable)
            continue;

        cpu.current        = &ptable.procs[idx];
        cpu.current->state = ProcessState::Running;
        if (cpu.current->pml4)
        {
            vmm::vmm_ptr->switch_address_space(cpu.current->pml4);
        }
        arch_prepare_switch(cpu.current);
        switch_context(discard_context, cpu.current->context);
        return;
    }

    // Runnable なプロセスがない場合は、カーネル初期文脈に戻す
    cpu.current = nullptr;
    switch_context(discard_context, cpu.scheduler_context);
}
} // namespace


[[noreturn]] void exit(int status)
{
    Process *p = cpu.current;
    if (!p)
    {
        // カーネル初期文脈から exit() されるのは呼び出し側のバグ。
        // noreturn なので return はできない (呼び出し側は後ろのコードが無い前提で最適化される)
        while (1)
        {
            cpu_idle();
        }
    }

    close_all_files(p);

    irq_disable(); // 状態変更中は割り込みを止める

    p->exit_status = status;
    p->state       = ProcessState::Zombie;

    if (p->parent)
    {
        // 親プロセスが wait() している場合に備えて wakeup する。
        //
        // wait() は回収できる Zombie の子がいないとき sleep(p) で寝る。この p は
        // 「親自身の Process*」であり、ここでの p->parent と同じ値になる。つまり
        // このチャネルの一致が wait() 側 sleep() との対応関係そのもので、これを
        // 呼ばないと Sleeping な親は schedule() に永久にスキップされ、子も Zombie
        // のまま回収されずカーネルスタックが漏れる。
        //
        // 子の側からは親が今 wait() で寝ているか分からないので、判定せず無条件に
        // 呼ぶ。wakeup() は Sleeping かつチャネル一致のプロセスだけを対象にするため、
        // 親が寝ていなければ何もしない。親がまだ wait() を呼んでいない場合も、後で
        // 呼んだ時点で Zombie が走査で見つかり sleep() に到達しないので問題ない。
        //
        // 上の state = Zombie より後に呼ぶこと。逆順だと起こされた親が走査しても
        // まだ Zombie でなく、再び寝てしまい今度は誰も起こさない。
        wakeup(p->parent);
    }

    // 孤児プロセスになるため init に引き取らせる。Linux Kernelと同様
    for (Process &child : ptable.procs)
    {
        if (child.state == ProcessState::Unused || child.parent != p)
            continue;
        child.parent = init_proc;
        if (init_proc != nullptr && child.state == ProcessState::Zombie)
        {
            // この子は既に exit 済みで、その時の wakeup は元の親 (p) 宛てだった。
            // init 宛ての wakeup は誰も送らないので、ここで起こす。
            // 起きた init は wait() で子を走査し直して、この Zombie を回収する。
            wakeup(init_proc);
        }
    }

    //二度と戻らないので、スケジューラに制御を渡す
    static ProcessContext *discard_context;
    schedule_from_zombie(&discard_context);

    // unreachable, but just in case, halt the CPU to avoid unexpected behavior. The process will be in Zombie state,
    while (1)
        cpu_idle();
}

void free_process_resources(Process *proc)
{
    close_all_files(proc);

    if (proc->kernel_stack)
    {
        ptable.heap->free(proc->kernel_stack.base);
        proc->kernel_stack = KernelStack{};
    }

    if (proc->pml4)
    {
        vmm::vmm_ptr->destroy_address_space(proc->pml4);
    }

    proc->pml4          = PhysicalAddress{};
    proc->parent        = nullptr;
    proc->sleep_channel = nullptr;
    proc->exit_status   = 0;
    proc->state         = ProcessState::Unused;
}

std::optional<ProcessId> wait(int *exit_code_out)
{
    Process *p = cpu.current;

    while (1)
    {
        bool has_child = false;

        for (size_t i = 0; i < ptable.procs.size(); ++i)
        {
            Process *child = &ptable.procs[i];
            // 未使用のスロットは子ではない。fork が途中で失敗した場合、state だけ Unused に
            // 戻されて parent が残るので、ここで弾かないと「居ない子」を待って永久に寝る。
            if (child->state == ProcessState::Unused)
                continue;
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

                const ProcessId pid = child->pid;
                free_process_resources(child);

                return pid;
            }
        }

        if (!has_child)
        {
            return std::nullopt; // 待つ相手が居ない
        }

        // when child processes exist but none of them are zombies, the parent process should sleep until a child
        // process exits exit() will wake up the parent process when a child process exits, so the parent process can
        // check again if any child processes are zombies.
        sleep(p);
    }
}


namespace
{


// ring3のプロセスがfork()を読んだ時に子が親と同じ場所から戻り値0でユーザモードに戻れる状態を
// 作りスケジューラに渡す関数
// リング3のプロセスから呼ばれたfork
int fork_user(Process *parent, Process *child)
{
    
    // ユーザページの複製
    if (!vmm::vmm_ptr->copy_user_pages(parent->pml4, child->pml4))
    {
        // copy_user_pages() は複製済みのユーザーページを自分で解放してから戻る。
        // 残りの資源 (カーネルスタック・アドレス空間) と parent はここで片付ける。
        free_process_resources(child);
        return -1;
    }
    arch_setup_user_fork(parent,child);
    child->state = ProcessState::Runnable;
    return static_cast<int>(child->pid); // return the child's pid to the parent process
}

} // namespace


int fork()
{
    Process *parent = cpu.current;
    if (!parent)
    {
        return -1; // カーネル初期文脈では fork しない
    }
    Process *child = allocate_process();
    if (!child)
    {
        return -1;
    }

    //子供に親の情報をコピーする
    child->entry     = parent->entry;
    child->entry_arg = parent->entry_arg;
    child->name      = parent->name;
    child->parent    = parent;

    for (size_t fd = 0; fd < parent->files.size(); fd++)
    {
        child->files[fd] = FileSystem::file_duplicate(parent->files[fd]);
    }

    bool is_user_thread = (parent->trap_frame != nullptr);
    if (is_user_thread)
    {
        // Copy the parent's trap frame to the child's trap frame
        return fork_user(parent, child);
    }
    else
    {
        return arch_fork_kernel(parent, child);
    }
}

} // namespace process
