
#include <cstdint>
#include "asm/switch_to.hpp"
#include "gdt.hpp"
#include "vmm.hpp"
#include "process.hpp"

namespace
{


// 呼び出し時点の callee-saved と戻りアドレスを switch_context 形式で out に
// 保存し、呼び出し元(fork)の rsp を返す setjmp 風ヘルパ (syscall/fork_ret.asm)。
extern "C" uint64_t fork_capture(ProcessContext *out);

// fork した子が、スケジューラから最初に選ばれたときの着地点で子の TrapFrame を読み出して sysret し、リング3へ戻る。
extern "C" void fork_return();

} // namespace


//
//   高  ┌────────────────┐ ← kernel_stack.top()
//       │ ProcessContext │   rbp..r15 = 0, rip = trampoline
//       ├────────────────┤ ← proc->context   switch_context: 6 pop + ret → trampoline
//       │                │
//       │ (空き)          │   trampoline 以降はここをスタックとして使う
//       │                │
//   低  └────────────────┘ ← kernel_stack.base
void arch_setup_kernel_thread(Process *proc, void (*entry)()){
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
        .rip = reinterpret_cast<uint64_t>(entry),
    };

    proc->context = context;
    
}

// ring3からfork

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

void arch_setup_user_fork(Process *parent, Process *child){
    
    // カーネルスタックは allocate_process() が確保済み。
    uint8_t *child_stack = child->kernel_stack.base;
        

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

int arch_fork_kernel(Process *parent, Process *child){

    uint8_t *parent_stack = parent->kernel_stack.base;

    // 親のスタック上のアドレスを、子のスタック上の同じ相対位置に写す
    // 子のスタックは仮想アドレスが親と違うため補正が必要
    //   子のアドレス = 親のアドレス - 親の下端 + 子の下端
    auto to_child = [&](KernelStackAddress parent_address)
    {
        const uint64_t offset = parent->kernel_stack.offset_of(parent_address); // 親のアドレス - 親の下端
        return child->kernel_stack.at(offset);                                  // 子の下端 + offset
    };

    // カーネルスタックは allocate_process() が確保済み。
    uint8_t *child_stack = child->kernel_stack.base;

    // copy the parent's kernel stack to the child's kernel stack
    // (make the child process's kernel stack identical to the parent's kernel stack)
    for (size_t i = 0; i < KERNEL_STACK_SIZE; i++)
    {
        child_stack[i] = parent_stack[i];
    }

    // 2. 今の callee-saved と復帰点 (= 直後の★) を、switch_context が読む形式で snap に取っておく。
    //    戻り値は fork_capture から戻った直後の rsp (= 図の caller_rsp)。
    ProcessContext snap;
    const KernelStackAddress caller_rsp{fork_capture(&snap)};

    // ★ fork_capture() は setjmp と同じく 2 回戻ってくる。
    //   1 回目: 親が普通に戻る。cpu.current == parent なので素通りする。
    //   2 回目: 子がスケジュールされ、switch_context の ret でここに着地する。
    //           リング0のまま、複製したスタックの上で動いている。
    //           yield() が先に cpu.current = child にしているので 0 を返し、
    //           あとは複製された fork() のフレームを通って呼び出し元へ戻っていく。
    if (process::current_process() == child)
    {
        return 0; // 子: fork() は 0 を返す
    }

    // ── 以降は親のみ実行 ──
    // 子が★に着地したときの rsp を求め、そのすぐ下に ProcessContext を置く。
    // switch_context が 6 本 pop + ret すると、rsp はちょうど child_rsp_at_resume になる。
    const KernelStackAddress child_rsp_at_resume = to_child(caller_rsp);
    child->context  = reinterpret_cast<ProcessContext *>(child_rsp_at_resume.address - sizeof(ProcessContext));
    *child->context = snap;

    child->state = ProcessState::Runnable;
    return static_cast<int>(child->pid); // return the child's pid to the parent process

}



// 今のコンテキストを *save_to に保存し、next に切り替える。
// next が次に選ばれて戻ってきたとき (= 誰かが save_to に切り替えたとき) にこの関数から戻る。
void arch_switch(ProcessContext **save_to, Process *next)
{
    if (next->pml4)
    {
        vmm::vmm_ptr->switch_address_space(next->pml4); // step1: CR3を子の空間へ
    }

    // step2: TSS.rsp0と syscall_kernel_rsp
    // set TSS's RSP0 to the top of the current process's kernel stack, so that when an interrupt occurs, the
    // CPU will switch to this stack.
    gdt::set_kernel_stack(reinterpret_cast<uint64_t>(next->kernel_stack.top()));

    // step3: rspを子のフレームへ
    switch_context(save_to, next->context);
}

void arch_switch_to_scheduler(ProcessContext **save_to, ProcessContext *scheduler_context)
{
    switch_context(save_to, scheduler_context);
}
