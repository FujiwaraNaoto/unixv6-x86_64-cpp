#pragma once
#include <cstdint>
#include <cstddef>
#include "pmm.hpp"
#include "vmm.hpp"
namespace heap
{
// ─── カーネルヒープの仮想アドレス配置 ─────────────────────────────
// 0xFFFFFFFF80000000 ┌─ カーネルコード/データ (PML4[511], PDPT[510], PD[0..])
// 0xFFFFFFFF90000000 ├─ カーネルヒープ         (PML4[511], PDPT[510], PD[128..])
//
// カーネルコードと同じ PDPT エントリ (1GiB: 0xFFFFFFFF80000000〜
// 0xFFFFFFFFC0000000) の内側に置いてあるので、ブート時に張った
// pml4_table[511] → pdpt_high[510] → pd_high をそのまま使える。
// map_page() は PD 配下の PT だけを新規に作る。
//
// PML4[511] は create_address_space() が全アドレス空間へコピーするため、
// ヒープはどのプロセスのアドレス空間からも同じ仮想アドレスで見える。
constexpr uint64_t KERNEL_HEAP_BASE = 0xFFFFFFFF90000000ULL;
constexpr uint64_t KERNEL_HEAP_SIZE = 16ULL * 1024 * 1024; // 16MiB (実ページは sbrk で遅延割り当て)
constexpr uint64_t KERNEL_HEAP_END  = KERNEL_HEAP_BASE + KERNEL_HEAP_SIZE;

static_assert(KERNEL_HEAP_END <= 0xFFFFFFFFC0000000ULL,
              "kernel heap must stay inside the PDPT[510] 1GiB region mapped at boot");

struct Block
{
    size_t size; // ブロックのサイズ (ヘッダを含む)
    bool free;   // ブロックが空いているか
    Block *next; // 次のブロックへのポインタ
    Block *prev; // 前のブロックへのポインタ
};

class Heap final
{
  public:
    Heap(uint64_t virt_start,
         uint64_t virt_end,
         pmm::PhysicalMemoryManager *pmm_ptr,
         vmm::VirtualMemoryManager *vmm_ptr);
    void *alloc(size_t size);
    void free(void *ptr);

    // error時には(void*)-1を返す
    void *sbrk(intptr_t increment);
    void *morecore(size_t pages);

  private:
    pmm::PhysicalMemoryManager *pmm_ptr_ = nullptr; // PMMのポインタ
    vmm::VirtualMemoryManager *vmm_ptr_  = nullptr; // VMMのポインタ
};

inline Heap *heap_ptr;
} // namespace heap
