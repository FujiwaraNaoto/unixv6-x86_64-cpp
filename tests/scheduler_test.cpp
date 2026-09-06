#include "tests.hpp"
#include "process.hpp"
#include "vga.hpp"

namespace tests
{
namespace
{

void thread_A()
{
    for (int i = 0; i < 3; ++i)
    {
        vga::vga->set_color(Color::LightCyan, Color::Black);
        vga::vga->printf("[THREAD-A] iteration %u\n", static_cast<unsigned>(i));
        vga::vga->set_color(Color::LightGrey, Color::Black);
        process::yield();
    }
}

void thread_B()
{
    for (int i = 0; i < 3; ++i)
    {
        vga::vga->set_color(Color::LightCyan, Color::Black);
        vga::vga->printf("[THREAD-B] iteration %u\n", static_cast<unsigned>(i));
        vga::vga->set_color(Color::LightGrey, Color::Black);
        process::yield();
    }
}

} // namespace

void scheduler_yield()
{
    Process *procA = process::create_process(thread_A, "Thread A");
    Process *procB = process::create_process(thread_B, "Thread B");
    vga::vga->printf("[DBG] procA=0x%x stateA=%d procB=0x%x stateB=%d\n",
                     static_cast<unsigned>(reinterpret_cast<uintptr_t>(procA)),
                     procA ? static_cast<int>(procA->state) : -1,
                     static_cast<unsigned>(reinterpret_cast<uintptr_t>(procB)),
                     procB ? static_cast<int>(procB->state) : -1);

    process::yield(); // 最初のプロセスに切り替える
}

} // namespace tests
