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
using EntryPoint = void (*)();

struct Process
{
    uint64_t pid;
    ProcessState state;
    ProcessContext *context; // カーネルスタック上の保存コンテキストを指す
    uint64_t kernel_stack;
    EntryPoint entry;
    kstring<16> name;
};

namespace process
{
void initialize(heap::Heap *heap_ptr);
Process *create_process(EntryPoint entry, const char *name);
void yield();
Process *current_process();
} // namespace process
