#pragma once
#include <cstdint>
#include <cstddef>
#include "heap.hpp"
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
    Ready,
    Running,
    Sleeping,
    Zombie,
};

constexpr size_t MAX_PROCESSES     = 64;
constexpr size_t KERNEL_STACK_SIZE = 0x4000; // 16KB

// プロセスのエントリポイント (引数なし・戻り値なしの関数)。
// NOTE: std::function などの C++ 標準ライブラリは使えないので、関数ポインタで表現する
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
} // namespace process
