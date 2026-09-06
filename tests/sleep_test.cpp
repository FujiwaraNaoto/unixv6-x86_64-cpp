#include "tests.hpp"
#include "process.hpp"
#include "vga.hpp"

namespace tests
{
namespace
{

// sleep/wakeup デモ用の待機理由 (任意のポインタ値)
int sleep_channel;
volatile bool sleeper_woke = false;

void sleeper_thread()
{
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->puts("[SLEEP] sleeper going to sleep...\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);

    process::sleep(&sleep_channel); // ここで寝る

    // 起こされたら再開
    sleeper_woke = true;
    vga::vga->set_color(Color::LightCyan, Color::Black);
    vga::vga->puts("[SLEEP] sleeper woke up!\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);
}

void waker_thread()
{
    // 少し他の処理を挟んでから起こす
    for (int i = 0; i < 3; i++)
        process::yield();

    vga::vga->set_color(Color::LightMagenta, Color::Black);
    vga::vga->puts("[WAKE]  waking sleeper...\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    process::wakeup(&sleep_channel);
}

} // namespace

void sleep_wakeup()
{
    process::create_process(sleeper_thread, "sleeper");
    process::create_process(waker_thread, "waker");
    process::yield();

    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[SLEEP] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("result: sleeper %s\n", sleeper_woke ? "WOKE-OK" : "STILL-SLEEPING-FAIL");
}

} // namespace tests
