#include "tests.hpp"
#include "process.hpp"
#include "vga.hpp"

namespace tests
{
namespace
{

// fork/exit/wait デモ用スレッド
void parent_thread()
{
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->printf("[FORK] parent: calling fork()\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);

    int pid = process::fork();
    if (pid == 0)
    {
        // 子
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->printf("[FORK] child: I am the child, exiting with 42\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        process::exit(42);
    }
    else
    {
        // 親
        vga::vga->set_color(Color::Yellow, Color::Black);
        vga::vga->printf("[FORK] parent: forked child pid=%u, waiting...\n", (unsigned)pid);
        vga::vga->set_color(Color::LightGrey, Color::Black);

        int code;
        int wpid = process::wait(&code);
        vga::vga->set_color(Color::Yellow, Color::Black);
        vga::vga->printf("[FORK] parent: child %u exited with code %u\n", (unsigned)wpid, (unsigned)code);
        vga::vga->set_color(Color::LightGrey, Color::Black);
    }
}

} // namespace

void fork_wait()
{
    process::create_process(parent_thread, "parent");
    process::yield();
}

} // namespace tests
