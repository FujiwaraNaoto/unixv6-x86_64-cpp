#include <cstdint>
#include "tests.hpp"
#include "process.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

namespace tests
{
namespace
{

// スレッド関数は引数を取れないので、出力先はプロセスを作る前にここへ設定する。
IConsole *free_console = nullptr;

// fork + wait を繰り返す回数。MAX_PROCESSES より多くして、
// 回収した枠 (ProcessState::Unused) が再利用されることも一緒に確かめる。
constexpr int kForkCount = static_cast<int>(MAX_PROCESSES) + 36;

uint64_t free_pages()
{
    return pmm::pmm_ptr->get_state().free_pages;
}

// before と after の空きページ数が同じなら OK、違えば漏れた (または余分に返した) ページ数を出す。
void report(const char *what, uint64_t before, uint64_t after)
{
    if (before == after)
    {
        free_console->set_color(Color::LightGreen, Color::Black);
        free_console->printf("[FREE] %s: OK (free pages %lu)\n", what, after);
    }
    else
    {
        free_console->set_color(Color::LightRed, Color::Black);
        free_console->printf("[FREE] %s: NG before=%lu after=%lu diff=%ld\n",
                             what,
                             before,
                             after,
                             static_cast<long>(before) - static_cast<long>(after));
    }
    free_console->set_color(Color::LightGrey, Color::Black);
}

// destroy_address_space() が、ユーザーページ・ページテーブル・PML4 をすべて返すことを確かめる。
// PT / PD / PDPT のそれぞれが複数になるようにマップして、どの段の解放漏れも数に出るようにする。
void destroy_returns_all_pages()
{
    // 0x600000 と 0x601000 : 同じ PT
    // 0x800000             : 同じ PD の別の PT
    // 0x40000000           : 別の PD (PDPT の別エントリ)
    constexpr PageVirtualAddress kPages[] = {{0x600000}, {0x601000}, {0x800000}, {0x40000000}};

    const uint64_t before = free_pages();

    const PhysicalAddress pml4 = vmm::vmm_ptr->create_address_space();
    if (!pml4)
    {
        report("destroy_address_space (create failed)", before, free_pages());
        return;
    }
    for (const PageVirtualAddress va : kPages)
    {
        vmm::vmm_ptr->map_page_in(pml4,
                                  va,
                                  pmm::pmm_ptr->allocate(),
                                  vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User);
    }
    // PML4 1 + PDPT 1 + PD 2 + PT 3 + ページ 4 = 11
    free_console->printf("[FREE] address space uses %lu pages\n", before - free_pages());

    vmm::vmm_ptr->destroy_address_space(pml4);

    report("destroy_address_space", before, free_pages());
}

// fork して子を終了させ、wait で回収する。回収した pid が子のものなら true。
bool fork_and_wait()
{
    const int pid = process::fork();
    if (pid == 0)
    {
        process::exit(0);
    }
    if (pid < 0)
    {
        return false; // プロセステーブルが埋まった = 回収した枠が再利用されていない
    }
    int code;
    return process::wait(&code) == pid;
}

// fork + wait を繰り返しても空きページが減らないことを確かめる (子の PML4 とカーネルスタック)。
void fork_wait_thread()
{
    // 1 回目はヒープが伸びる (カーネルスタックの確保で sbrk する) ので、慣らしてから測る
    fork_and_wait();

    const uint64_t before = free_pages();
    for (int i = 0; i < kForkCount; ++i)
    {
        if (!fork_and_wait())
        {
            free_console->set_color(Color::LightRed, Color::Black);
            free_console->printf("[FREE] fork+wait failed at %u\n", static_cast<unsigned>(i));
            free_console->set_color(Color::LightGrey, Color::Black);
            return;
        }
    }
    report("fork+wait loop", before, free_pages());
}

} // namespace

void process_free(IConsole *console)
{
    free_console = console;

    destroy_returns_all_pages();

    process::create_process(fork_wait_thread, "free");
    process::yield();
}

} // namespace tests
