#pragma once
#include <cstdint>
#include <optional>
#include "pmm.hpp"
#include "console.hpp"

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


// 物理アドレス。
// 仮想アドレスや普通の整数と取り違えないよう、型で区別する。
// 物理 0 も実在するページなので、「無効」は 0 ではなく nullopt で表す。
struct PhysicalAddress
{
    std::optional<uint64_t> address;

    // if (!phys) で「無効 (nullopt)」を判定できるようにする。
    explicit operator bool() const
    {
        return address.has_value();
    }
};

// マップ対象の仮想アドレス (ユーザー空間やヒープなど任意の場所)。
// VirtualAddress (direct map 上のページテーブルを指すポインタ) とは用途が違うので、
// 中身を読み書きするポインタではなく、ただのアドレス値として持つ。
struct PageVirtualAddress
{
    uint64_t address;
};

// direct map 上の仮想アドレス。
// 物理アドレス (uint64_t) と取り違えないよう、型で区別する。
struct VirtualAddress
{
    uint64_t *ptr;

    // ページテーブルのエントリを table[i] の形で読み書きできるようにする
    uint64_t &operator[](uint64_t index) const
    {
        return ptr[index];
    }

    // if (!table) で「テーブルが無い (nullptr)」を判定できるようにする。
    // explicit なので整数などへ暗黙に変換されることはない。
    explicit operator bool() const
    {
        return ptr != nullptr;
    }
};

class VirtualMemoryManager final
{
  public:
    // console は初期化時の情報 (PML4 のアドレスなど) の出力先。
    // 出力先を注入で受け取るので、VMM は VGA / シリアルのどちらに出るかを知らない。
    // nullptr を渡した場合は何も出力しない。
    VirtualMemoryManager(pmm::PhysicalMemoryManager *pmm_ptr, IConsole *console);
    // physical_address が無効 (nullopt) なら何もせず false を返す。
    bool map_page(PageVirtualAddress virtual_address, PhysicalAddress physical_address, uint64_t flags);
    // map解除とTLBフラッシュ
    bool unmap_page(uint64_t virtual_address);
    // マップされていなければ nullopt を返す。
    // (物理 0 は「未マップ」ではなく実在するページなので、番兵値には使えない)
    PhysicalAddress virtual_to_physical(uint64_t virtual_address) const;

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
    VirtualAddress get_or_create_table(VirtualAddress parent_table, uint64_t index, uint64_t flags);

    PhysicalAddress pml4_phys_;
    pmm::PhysicalMemoryManager *pmm_ptr_ = nullptr;
};

inline VirtualMemoryManager *vmm_ptr = nullptr;

} // namespace vmm
