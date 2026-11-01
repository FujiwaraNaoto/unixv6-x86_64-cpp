#include "tests.hpp"
#include "process.hpp"

namespace tests
{
namespace
{

// スレッド関数の引数 (void *) は使わず、出力先はテスト関数がプロセスを作る前にここへ設定する。
IConsole *thread_console = nullptr;

// 各スレッドが回った回数 (yield で切り替わらないと最後まで回らない)
constexpr int kIterations = 3;
volatile int iterations_a = 0;
volatile int iterations_b = 0;

void thread_A(void *)
{
    for (int i = 0; i < kIterations; ++i)
    {
        thread_console->set_color(Color::LightCyan, Color::Black);
        thread_console->printf("[THREAD-A] iteration %u\n", static_cast<unsigned>(i));
        thread_console->set_color(Color::LightGrey, Color::Black);
        iterations_a = i + 1;
        process::yield();
    }
}

void thread_B(void *)
{
    for (int i = 0; i < kIterations; ++i)
    {
        thread_console->set_color(Color::LightCyan, Color::Black);
        thread_console->printf("[THREAD-B] iteration %u\n", static_cast<unsigned>(i));
        thread_console->set_color(Color::LightGrey, Color::Black);
        iterations_b = i + 1;
        process::yield();
    }
}

} // namespace

bool scheduler_yield(IConsole *console)
{
    thread_console = console;

    Process *procA = process::create_process(thread_A, "Thread A");
    Process *procB = process::create_process(thread_B, "Thread B");
    console->printf("[DBG] procA=0x%x stateA=%d procB=0x%x stateB=%d\n",
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(procA)),
                    procA ? static_cast<int>(procA->state) : -1,
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(procB)),
                    procB ? static_cast<int>(procB->state) : -1);

    if (procA == nullptr || procB == nullptr)
    {
        return false;
    }

    process::yield(); // 最初のプロセスに切り替える

    return iterations_a == kIterations && iterations_b == kIterations;
}

} // namespace tests
