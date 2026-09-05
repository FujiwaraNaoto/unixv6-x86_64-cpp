#pragma once
#include <cstdint>
#include "pmm.hpp"

constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t PAGE_MASK = ~(PAGE_SIZE - 1);

// direct map: 物理メモリ全体を高位にマップした領域のベース
constexpr uint64_t DIRECT_MAP_BASE = 0xFFFF800000000000ULL;

// 物理 → direct map 上の仮想アドレス
inline uint64_t phys_to_virt(uint64_t phys)
{
    return phys + DIRECT_MAP_BASE;
}
// direct map 上の仮想 → 物理
inline uint64_t virt_to_phys_direct(uint64_t virt)
{
    return virt - DIRECT_MAP_BASE;
}


namespace vmm
{

// Intel SDM Volume 3A, Chapter 4 "Paging"
namespace PageFlag
{
constexpr uint64_t Present   = 1ULL << 0;
constexpr uint64_t Writable  = 1ULL << 1; // R/Wbit
constexpr uint64_t User      = 1ULL << 2; // リング3からアクセス可
constexpr uint64_t Accessed  = 1ULL << 5;
constexpr uint64_t Dirty     = 1ULL << 6;
constexpr uint64_t HugePage  = 1ULL << 7;
constexpr uint64_t NoExecute = 1ULL << 63;
} // namespace PageFlag


class VirtualMemoryManager final
{
  public:
    VirtualMemoryManager(pmm::PhysicalMemoryManager *pmm_ptr);
    bool map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
    // map解除とTLBフラッシュ
    bool unmap_page(uint64_t virtual_address);
    uint64_t virtual_to_physical(uint64_t virtual_address) const;

    void flush_tlb();


    // 新しいアドレス空間(PML4)を作成し，その物理アドレスを返す
    // カーネル領域のエントリは現在の PML4からコピーされる
    uint64_t create_address_space();

    // CR3を指定PML4に切り替える。
    void switch_address_space(uint64_t pml4_phys);

    // 指定PML4に対して、指定仮想アドレスを指定物理アドレスにマッピングする。(プロセスにアドレス空間構築用)
    bool map_page_in(uint64_t pml4_phys, uint64_t virtual_address, uint64_t physical_address, uint64_t flags);

    void copy_user_pages(uint64_t src_pml4_phys, uint64_t dst_pml4_phys);

  private:
    uint64_t *get_or_create_table(uint64_t *parent_table, uint64_t index, uint64_t flags);

    uint64_t pml4_phys_                  = 0;
    pmm::PhysicalMemoryManager *pmm_ptr_ = nullptr;
};

inline VirtualMemoryManager *vmm_ptr = nullptr;

} // namespace vmm
