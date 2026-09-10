#include "tests.hpp"
#include "process.hpp"

namespace tests
{
namespace
{

// スレッド関数は EntryPoint (void (*)()) として create_process に渡すので引数を
// 取れない。出力先はテスト関数がプロセスを作る前にここへ設定する。
// (fork した子もカーネル空間を共有しているので、同じポインタをそのまま使える)
IConsole *thread_console = nullptr;

// fork/exit/wait デモ用スレッド
void parent_thread()
{
    thread_console->set_color(Color::LightCyan, Color::Black);
    thread_console->printf("[FORK] parent: calling fork()\n");
    thread_console->set_color(Color::LightGrey, Color::Black);

    int pid = process::fork();
    if (pid == 0)
    {
        // 子
        thread_console->set_color(Color::LightGreen, Color::Black);
        thread_console->printf("[FORK] child: I am the child, exiting with 42\n");
        thread_console->set_color(Color::LightGrey, Color::Black);
        process::exit(42);
    }
    else
    {
        // 親
        thread_console->set_color(Color::Yellow, Color::Black);
        thread_console->printf("[FORK] parent: forked child pid=%u, waiting...\n", (unsigned)pid);
        thread_console->set_color(Color::LightGrey, Color::Black);

        int code;
        int wpid = process::wait(&code);
        thread_console->set_color(Color::Yellow, Color::Black);
        thread_console->printf("[FORK] parent: child %u exited with code %u\n", (unsigned)wpid, (unsigned)code);
        thread_console->set_color(Color::LightGrey, Color::Black);
    }
}

} // namespace

void fork_wait(IConsole *console)
{
    thread_console = console;

    process::create_process(parent_thread, "parent");
    process::yield();
}

} // namespace tests
