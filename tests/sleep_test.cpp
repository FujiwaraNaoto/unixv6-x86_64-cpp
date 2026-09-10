#include "tests.hpp"
#include "process.hpp"

namespace tests
{
namespace
{

// sleep/wakeup デモ用の待機理由 (任意のポインタ値)
int sleep_channel;
volatile bool sleeper_woke = false;

// スレッド関数は EntryPoint (void (*)()) として create_process に渡すので引数を
// 取れない。出力先はテスト関数がプロセスを作る前にここへ設定する。
IConsole *thread_console = nullptr;

void sleeper_thread()
{
    thread_console->set_color(Color::LightCyan, Color::Black);
    thread_console->puts("[SLEEP] sleeper going to sleep...\n");
    thread_console->set_color(Color::LightGrey, Color::Black);

    process::sleep(&sleep_channel); // ここで寝る

    // 起こされたら再開
    sleeper_woke = true;
    thread_console->set_color(Color::LightCyan, Color::Black);
    thread_console->puts("[SLEEP] sleeper woke up!\n");
    thread_console->set_color(Color::LightGrey, Color::Black);
}

void waker_thread()
{
    // 少し他の処理を挟んでから起こす
    for (int i = 0; i < 3; i++)
        process::yield();

    thread_console->set_color(Color::LightMagenta, Color::Black);
    thread_console->puts("[WAKE]  waking sleeper...\n");
    thread_console->set_color(Color::LightGrey, Color::Black);
    process::wakeup(&sleep_channel);
}

} // namespace

void sleep_wakeup(IConsole *console)
{
    thread_console = console;

    process::create_process(sleeper_thread, "sleeper");
    process::create_process(waker_thread, "waker");
    process::yield();

    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[SLEEP] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("result: sleeper %s\n", sleeper_woke ? "WOKE-OK" : "STILL-SLEEPING-FAIL");
}

} // namespace tests
