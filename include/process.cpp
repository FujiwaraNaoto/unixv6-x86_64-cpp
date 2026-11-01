#include <cstdint>
#include <array>
#include "process.hpp"
#include "heap.hpp"
#include "vmm.hpp"
#include "address.hpp"
#include "gdt.hpp"
#include "file.hpp"

extern "C" void switch_context(ProcessContext **old_ctx, ProcessContext *new_ctx);

namespace
{


// 呼び出し時点の callee-saved と戻りアドレスを switch_context 形式で out に
// 保存し、呼び出し元(fork)の rsp を返す setjmp 風ヘルパ (syscall/fork_ret.asm)。
extern "C" uint64_t fork_capture(ProcessContext *out);

// fork した子が、スケジューラから最初に選ばれたときの着地点で子の TrapFrame を読み出して sysret し、リング3へ戻る。
extern "C" void fork_return();

} // namespace


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

} // namespace

void initialize(heap::Heap &heap)
{
    // 2 回目の initialize() はプロセステーブルを黙って消して、走っているプロセスの
    // スロットを Unused に戻してしまうので、進まずに止める。
    if (ptable.initialized)
    {
        while (1)
            asm volatile("hlt");
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


// fd 表の File をすべて閉じる。閉じた fd は nullptr にするので、2 回呼んでも問題ない。
static void close_all_files(Process *proc)
{
    for (File *&file : proc->files)
    {
        FileSystem::file_close(file);
        file = nullptr;
    }
}

// fd 0/1/2 をコンソールにつなぐ。失敗したら false (開けた分は呼び出し側が free_process_resources で閉じる)。
static bool open_console_files(Process *proc)
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
static void trampoline()
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
        asm volatile("hlt");
}

static void schedule(Process *prev_proc)
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

        // step2: TSS.rsp0と syscall_kernel_rsp
        // set TSS's RSP0 to the top of the current process's kernel stack, so that when an interrupt occurs, the
        // CPU will switch to this stack.
        gdt::set_kernel_stack(reinterpret_cast<uint64_t>(cpu.current->kernel_stack.top()));

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

    void *stack = ptable.heap->alloc(KERNEL_STACK_SIZE);
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
//
//   高  ┌────────────────┐ ← kernel_stack.top()
//       │ ProcessContext │   rbp..r15 = 0, rip = trampoline
//       ├────────────────┤ ← proc->context   switch_context: 6 pop + ret → trampoline
//       │                │
//       │ (空き)          │   trampoline 以降はここをスタックとして使う
//       │                │
//   低  └────────────────┘ ← kernel_stack.base

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

    uint8_t *stack_top = proc->kernel_stack.top();
    // スタックはアドレスの小さい方に成長していく。アセンブリでローカル変数確保の容量で引くことでProcessContextのメモリ領域を確保する
    auto *context = reinterpret_cast<ProcessContext *>(stack_top - sizeof(ProcessContext));

    *context = ProcessContext{
        .rbp = 0,
        .rbx = 0,
        .r12 = 0,
        .r13 = 0,
        .r14 = 0,
        .r15 = 0,
        .rip = reinterpret_cast<uint64_t>(trampoline),
    };

    proc->context = context;
    proc->state   = ProcessState::Runnable; // 構築完了。これでスケジューラが拾えるようになる
    return proc;
}

Process *current_process()
{
    return cpu.current;
}

Process *create_init_process(EntryPoint entry, void *arg)
{
    return create_process(entry, "init", arg);
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
// 現プロセスを chan を理由に Sleeping にして、別プロセスへ切り替える。
// wakeup(chan) で Runnable に戻され、再スケジュールされると
// swtch がこの続きに戻ってくる (= sleep から return する)。
//
void sleep(void *chan)
{
    Process *p = cpu.current;
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
// chan を理由に Sleeping な全プロセスを Runnable に戻す。
// 実際に走り出すのは次のスケジュールのタイミング。
//
void wakeup(void *chan)
{
    for (size_t i = 0; i < ptable.procs.size(); ++i)
    {
        if (ptable.procs[i].state == ProcessState::Sleeping && ptable.procs[i].sleep_channel == chan)
        {
            ptable.procs[i].state = ProcessState::Runnable;
        }
    }
}

// zombie になったプロセスのコンテキストを破棄して、別プロセスへ切り替える。
static void schedule_from_zombie(ProcessContext **discard_context)
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
        gdt::set_kernel_stack(reinterpret_cast<uint64_t>(cpu.current->kernel_stack.top()));
        switch_context(discard_context, cpu.current->context);
        return;
    }

    // Runnable なプロセスがない場合は、カーネル初期文脈に戻す
    cpu.current = nullptr;
    switch_context(discard_context, cpu.scheduler_context);
}


[[noreturn]] void exit(int status)
{
    Process *p = cpu.current;
    if (!p)
    {
        // カーネル初期文脈から exit() されるのは呼び出し側のバグ。
        // noreturn なので return はできない (呼び出し側は後ろのコードが無い前提で最適化される)
        while (1)
            asm volatile("cli; hlt");
    }

    close_all_files(p);

    asm volatile("cli"); // 状態変更中は割り込みを止める

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
    //二度と戻らないので、スケジューラに制御を渡す
    static ProcessContext *discard_context;
    schedule_from_zombie(&discard_context);

    // unreachable, but just in case, halt the CPU to avoid unexpected behavior. The process will be in Zombie state,
    while (1)
        asm volatile("hlt");
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

// child_stack + KERNEL_STACK_SIZE  ← 上端 (syscall_kernel_rsp もここ)
// ┌──────────────────────┐
// │ TrapFrame (親のコピー) │ ← child->trap_frame   rax = 0
// │   rip, user rsp, ... │
// ├──────────────────────┤
// │ ProcessContext       │ ← child->context
// │   r15..rbp = 0       │
// │   rip = fork_return  │
// ├──────────────────────┤
// │                      │
// │   (空き: 以後のカーネル │
// │    スタックとして使う)  │
// │                      │
// └──────────────────────┘
// child_stack                       ← 下端 (アドレスが低い)


//カーネル側(上半分)は親と共有する．ユーザ側(下半分)は親のページを複製(同じ仮想アドレス，別の物理ページ)
// リング3のプロセスから呼ばれたfork
int fork_user(Process *parent, Process *child, uint8_t *child_stack)
{
    // ユーザページの複製
    if (!vmm::vmm_ptr->copy_user_pages(parent->pml4, child->pml4))
    {
        // copy_user_pages() は複製済みのユーザーページを自分で解放してから戻る。
        // 残りの資源 (カーネルスタック・アドレス空間) と parent はここで片付ける。
        free_process_resources(child);
        return -1;
    }


    // fork した子のカーネルスタック上端に置く初期フレーム。
    // 構造体のメンバは先に書いたものほど低いアドレスに置かれる (図では下)。
    //
    //   高  ┌────────────────┐ ← child_stack + KERNEL_STACK_SIZE
    //       │ TrapFrame      │
    //       ├────────────────┤ ← child->trap_frame  fork_return 着地時の rsp → sysret
    //       │ ProcessContext │
    //   低  └────────────────┘ ← child->context     switch_context: 6 pop + ret → fork_return

    // 子のカーネルスタックの一番上に、親の TrapFrame の複製を置く
    uint8_t *stack_top = child_stack + KERNEL_STACK_SIZE;
    // sizeof(UserForkFrame)することでUserForkFrame分を確保する. ローカル変数確保でsubする理由と同じ
    auto *frame           = reinterpret_cast<UserForkFrame *>(stack_top - sizeof(UserForkFrame));
    frame->trap_frame     = *parent->trap_frame; // copy parent's frame
    frame->trap_frame.rax = 0;                   // child returns 0 from fork()

    //スケジューラ(switch_context)がretで飛ぶ先(rip)を指定する
    frame->context = ProcessContext{
        .rbp = 0,
        .rbx = 0,
        .r12 = 0,
        .r13 = 0,
        .r14 = 0,
        .r15 = 0,
        .rip = reinterpret_cast<uint64_t>(fork_return),
    };

    // Process からこのフレームを指す。
    // スケジューラは child->context を読んで switch_context でそこへ飛び、
    // fork_return は child->trap_frame の位置に rsp が合った状態で sysret する。
    child->context    = &frame->context;
    child->trap_frame = &frame->trap_frame;

    child->state = ProcessState::Runnable;
    return static_cast<int>(child->pid); // return the child's pid to the parent process
}

// カーネルスレッドから呼ばれたfork().
// カーネルスレッドの fork。親のカーネルスタックを丸ごと複製し、子はリング0のまま
// fork_capture() の直後(★)から再開する。リング3のユーザスタックはコピーしない。
// 箱は各関数のスタックフレーム (戻りアドレス・保存レジスタ・ローカル変数)。
// 関数のコード自体は親子で共有している。
//
//          親のカーネルスタック                     子のカーネルスタック
//   高  ┌───────────────────────┐             ┌───────────────────────┐
//       │ parent_thread のフレーム│             │parent_thread のフレーム│ (複製)
//       ├───────────────────────┤             ├───────────────────────┤
//       │ fork() のフレーム       │  ── 複製 ─→  │fork() のフレーム       │ (複製)
//       ├───────────────────────┤             ├───────────────────────┤
//       │ fork_kernel() のフレーム│             │fork_kernel() のフレーム│ (複製)
//       ├───────────────────────┤ ← caller_rsp├───────────────────────┤ ← to_child(caller_rsp)
//       │                       │             │ ProcessContext        │    ★に着地したときの rsp
//       │                       │             │  rbp..r15 = snap      │ (親が後から書く)
//       │                       │             │  rip = ★              │
//       │ (空き)                 │             ├───────────────────────┤ ← child->context
//       │                       │              |(空き)                 │
//   低  └───────────────────────┘              └───────────────────────┘
//       parent->kernel_stack.base             child_stack

// カーネル側(上半分)は親と共有(同じ表)．ユーザ側(下半分)は空のまま
// カーネルスレッドの fork で、リング3のユーザスタックはコピーしない
int fork_kernel(Process *parent, Process *child, uint8_t *child_stack)
{
    uint8_t *parent_stack = parent->kernel_stack.base;

    // 親のスタック上のアドレスを、子のスタック上の同じ相対位置に写す
    // 子のスタックは仮想アドレスが親と違うため補正が必要
    auto to_child = [&](uint64_t parent_addr)
    { return parent_addr - reinterpret_cast<uint64_t>(parent_stack) + reinterpret_cast<uint64_t>(child_stack); };


    // copy the parent's kernel stack to the child's kernel stack
    // (make the child process's kernel stack identical to the parent's kernel stack)
    for (size_t i = 0; i < KERNEL_STACK_SIZE; i++)
    {
        child_stack[i] = parent_stack[i];
    }

    // 2. 今の callee-saved と復帰点 (= 直後の★) を、switch_context が読む形式で snap に取っておく。
    //    戻り値は fork_capture から戻った直後の rsp (= 図の caller_rsp)。
    ProcessContext snap;
    uint64_t caller_rsp = fork_capture(&snap);

    // ★ fork_capture() は setjmp と同じく 2 回戻ってくる。
    //   1 回目: 親が普通に戻る。cpu.current == parent なので素通りする。
    //   2 回目: 子がスケジュールされ、switch_context の ret でここに着地する。
    //           リング0のまま、複製したスタックの上で動いている。
    //           yield() が先に cpu.current = child にしているので 0 を返し、
    //           あとは複製された fork() のフレームを通って呼び出し元へ戻っていく。
    if (cpu.current == child)
    {
        return 0; // 子: fork() は 0 を返す
    }

    // ── 以降は親のみ実行 ──
    // 子が★に着地したときの rsp を求め、そのすぐ下に ProcessContext を置く。
    // switch_context が 6 本 pop + ret すると、rsp はちょうど child_rsp_at_resume になる。
    uint64_t child_rsp_at_resume = to_child(caller_rsp);
    child->context               = reinterpret_cast<ProcessContext *>(child_rsp_at_resume - sizeof(ProcessContext));
    *child->context              = snap;

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

    // カーネルスタックは allocate_process() が確保済み。
    auto *child_stack = child->kernel_stack.base;

    bool is_user_thread = (parent->trap_frame != nullptr);
    if (is_user_thread)
    {
        // Copy the parent's trap frame to the child's trap frame
        return fork_user(parent, child, child_stack);
    }
    else
    {
        return fork_kernel(parent, child, child_stack);
    }
}

} // namespace process
