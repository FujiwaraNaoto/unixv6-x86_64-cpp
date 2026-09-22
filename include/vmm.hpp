#pragma once
#include <cstdint>
#include <optional>
#include "pmm.hpp"
#include "console.hpp"
#include "address.hpp"

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
enum class PageFlag : uint64_t
{
    None      = 0,
    Present   = 1ULL << 0,
    Writable  = 1ULL << 1, // R/Wbit
    User      = 1ULL << 2, // リング3からアクセス可
    Accessed  = 1ULL << 5,
    Dirty     = 1ULL << 6,
    HugePage  = 1ULL << 7,
    NoExecute = 1ULL << 63,
};

// フラグ同士を組み合わせる / 一部を取り出す
constexpr PageFlag operator|(PageFlag a, PageFlag b)
{
    return static_cast<PageFlag>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}
constexpr PageFlag operator&(PageFlag a, PageFlag b)
{
    return static_cast<PageFlag>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

// ページテーブルのエントリ (生の uint64_t) にフラグを立てる / フラグが立っているかを調べる。
// エントリは物理アドレスとフラグが混ざった値なので、ここだけは整数との演算を許す。
constexpr uint64_t operator|(uint64_t entry, PageFlag flag)
{
    return entry | static_cast<uint64_t>(flag);
}
constexpr uint64_t operator&(uint64_t entry, PageFlag flag)
{
    return entry & static_cast<uint64_t>(flag);
}
constexpr uint64_t &operator|=(uint64_t &entry, PageFlag flag)
{
    return entry = entry | flag;
}


class VirtualMemoryManager final
{
  public:
    // console は初期化時の情報 (PML4 のアドレスなど) の出力先。
    // 出力先を注入で受け取るので、VMM は VGA / シリアルのどちらに出るかを知らない。
    // nullptr を渡した場合は何も出力しない。
    VirtualMemoryManager(pmm::PhysicalMemoryManager *pmm_ptr, IConsole *console);
    // physical_address が無効 (nullopt) なら何もせず false を返す。
    bool map_page(PageVirtualAddress virtual_address, PhysicalAddress physical_address, PageFlag flags);
    // map解除とTLBフラッシュ
    bool unmap_page(PageVirtualAddress virtual_address);
    // マップされていなければ nullopt を返す。
    // (物理 0 は「未マップ」ではなく実在するページなので、番兵値には使えない)
    PhysicalAddress virtual_to_physical(PageVirtualAddress virtual_address) const;

    void flush_tlb();


    // 新しいアドレス空間(PML4)を作成し，その物理アドレスを返す
    // カーネル領域のエントリは現在の PML4からコピーされる
    // メモリ不足で作成できなければ無効 (nullopt) な PhysicalAddress を返す
    PhysicalAddress create_address_space();

    // CR3を指定PML4に切り替える。
    void switch_address_space(PhysicalAddress pml4_phys);

    // 指定PML4に対して、指定仮想アドレスを指定物理アドレスにマッピングする。(プロセスにアドレス空間構築用)
    bool map_page_in(PhysicalAddress pml4_phys, PageVirtualAddress virtual_address, PhysicalAddress physical_address, PageFlag flags);

    void copy_user_pages(PhysicalAddress src_pml4_phys, PhysicalAddress dst_pml4_phys);

  private:
    VirtualAddress get_or_create_table(VirtualAddress parent_table, uint64_t index, PageFlag flags);

    PhysicalAddress pml4_phys_;
    pmm::PhysicalMemoryManager *pmm_ptr_ = nullptr;
};

inline VirtualMemoryManager *vmm_ptr = nullptr;

} // namespace vmm
