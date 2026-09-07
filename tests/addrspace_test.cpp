#include <cstdint>
#include "tests.hpp"
#include "process.hpp"
#include "pmm.hpp"
#include "vmm.hpp"
#include "vga.hpp"

namespace tests
{
namespace
{

// PML4分離テスト用スレッド。
// 同じ仮想アドレスに別の値を書いて確認する。
// 低位 identity map 撤去後は PML4[0] もプロセスごとに独立しているが、
// このテストは従来どおり PML4 スロット1 (512GiB〜) を使う。
constexpr uint64_t kAddrTestVirt = 0x8000000000; // PML4 index 1
volatile uint64_t test_result_a  = 0;
volatile uint64_t test_result_b  = 0;

void addrspace_thread_a()
{
    volatile uint64_t *p = reinterpret_cast<volatile uint64_t *>(kAddrTestVirt);
    *p                   = 0xAAAA;
    process::yield();   // Bに切り替わる
    test_result_a = *p; // 戻ってきて自分の値を再確認
}

void addrspace_thread_b()
{
    volatile uint64_t *p = reinterpret_cast<volatile uint64_t *>(kAddrTestVirt);
    *p                   = 0xBBBB;
    process::yield(); // Aに切り替わる
    test_result_b = *p;
}

} // namespace

void addrspace_separation()
{
    Process *pa = process::create_process(addrspace_thread_a, "addr-a");
    Process *pb = process::create_process(addrspace_thread_b, "addr-b");
    if (pa == nullptr || pb == nullptr)
    {
        vga::vga->set_color(Color::LightRed, Color::Black);
        vga::vga->puts("[ADDR] failed to create test processes\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        return;
    }

    // 各プロセスの同一仮想アドレスに物理ページを別々にマップ
    uint64_t phys_a = pmm::pmm_ptr->allocate();
    uint64_t phys_b = pmm::pmm_ptr->allocate();
    vmm::vmm_ptr->map_page_in(pa->pml4, kAddrTestVirt, phys_a, vmm::PageFlag::Present | vmm::PageFlag::Writable);
    vmm::vmm_ptr->map_page_in(pb->pml4, kAddrTestVirt, phys_b, vmm::PageFlag::Present | vmm::PageFlag::Writable);

    process::yield(); // スケジューラ起動

    // 両方のスレッド終了後に結果を確認
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[ADDR] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("A wrote 0xAAAA read 0x%x / B wrote 0xBBBB read 0x%x  %s\n",
                     (unsigned)test_result_a,
                     (unsigned)test_result_b,
                     (test_result_a == 0xAAAA && test_result_b == 0xBBBB) ? "SEPARATED-OK" : "SHARED-FAIL");
}

} // namespace tests
