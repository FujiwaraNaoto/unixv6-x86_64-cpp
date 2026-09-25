#pragma once
#include <cstdint>
#include <cstddef>
#include "address.hpp"
#include "kstring.hpp"

struct [[gnu::packed]] ProcessContext
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;
};

enum class ProcessState
{
    Unused,
    Embryo,
    Runnable,
    Running,
    Sleeping,
    Zombie,
};

// ProcessManager はポインタとして受け取るだけなので、前方宣言で済ませる。
namespace heap
{
class Heap;
}

constexpr size_t MAX_PROCESSES     = 64;
constexpr size_t KERNEL_STACK_SIZE = 0x4000; // 16KB

// プロセスのエントリポイント (引数なし・戻り値なしの関数)。
//
// std::function ではなく関数ポインタにしている理由:
//   1. std::function はキャプチャが 16 バイト (libstdc++ の内部バッファ) を超えると
//      operator new でヒープから確保するが、このカーネルには operator new が無く
//      リンクエラーになる (operator delete も空実装なので、足してもリークする)。
//      fork() で entry をコピーするときにも同じ確保が発生する。
//   2. entry は create_process() が戻った後に、別のカーネルスタック上で実行される。
//      参照キャプチャ ([&]) した呼び出し元のローカル変数は、その時点で寿命が切れている。
//      関数ポインタは何もキャプチャできないので、この問題が構造上起きない。
//
// NOTE: 標準ライブラリ自体が使えないわけではない (std::array などは使っている)。
//       また entry はアセンブリから直接ジャンプされる先ではなく trampoline() から
//       呼ばれるので、呼び出し可能オブジェクトにすること自体は技術的に可能。
using EntryPoint = void (*)();

struct Process
{
    uint64_t pid;
    ProcessState state;
    ProcessContext *context; // カーネルスタック上の保存コンテキストを指す
    uint64_t kernel_stack;
    EntryPoint entry;
    kstring<16> name; // NOTE: kstring は固定容量の文字列で、容量超過分は切り捨てられる.
                      // C++の標準ライブラリは使えないので、std::string は使えない
    PhysicalAddress pml4; // プロセスのページテーブルの物理アドレス(Page Map Level 4)。未割り当てなら nullopt
    void *sleep_channel; // プロセスが sleep している場合のチャネル (待機理由) 0=起きている
    Process *parent;     // 親プロセスへのポインタ (fork などで使う)
    int exit_status;     // プロセスの終了ステータス (exit() で設定される)
};

namespace process
{
// プロセス管理の初期化を担うクラス。
// コンストラクタが従来の initialize() 相当 (プロセステーブルのクリアと
// heap の登録) を行う。状態自体は process.cpp のモジュール内 static が持ち、
// create_process()/yield() はフリー関数としてそれを操作する。
class ProcessManager
{
  public:
    explicit ProcessManager(heap::Heap *heap_ptr);
};

Process *create_process(EntryPoint entry, const char *name);
void yield();
Process *current_process();

// 現在プロセスをchannelを理由にしてSleeping状態にしスケジューラへ制御を渡す
// wakeup()でchannelを理由にしてSleeping状態のプロセスをReady状態にする
void sleep(void *channel);

// channelを理由にしてSleeping状態のプロセスをReady状態にする
void wakeup(void *channel);


// terminate the current process and set its exit status. This function does not return.
[[noreturn]] void exit(int status);

// wait for child process to exit.
// If a child process has exited, return its pid and write its exit status to *exit_code_out.
// If there are no child processes, return -1.
int wait(int *exit_code_out); // 子プロセスの終了を待つ。終了した子プロセスの exit_status を status に書き込む

// fork the current process. Return the pid of the child process to the parent, and 0 to the child.
// If fork fails, return -1.
int fork();

} // namespace process
