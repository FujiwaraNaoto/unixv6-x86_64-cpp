#pragma once
#include <cstdint>
#include "multiboot2.hpp"

#include "vga.hpp"

namespace pmm
{
constexpr uint64_t PAGE_SIZE  = 0x1000; // 4KB
constexpr uint32_t PAGE_SHIFT = 12;     // 2^12 = 4096
struct PhysicalMemoryManagerState
{
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t base;
    uint64_t top;
    explicit PhysicalMemoryManagerState(uint64_t total_pages, uint64_t free_pages, uint64_t base, uint64_t top)
        : total_pages(total_pages), free_pages(free_pages), base(base), top(top)
    {
    }
};

static constexpr uint32_t MAX_PAGES   = 65536;
static constexpr uint32_t BITMAP_SIZE = MAX_PAGES / 8;

// TODO:vgaと依存するので剥がしたい
static void print_mm_state([[maybe_unused]] const PhysicalMemoryManagerState &state)
{
    // 自前 printf は 'l' 長さ修飾子を解釈しない (%llu と書くと "llu" がそのまま出る)。
    // %u / %x が既に unsigned long long を取り出すので、これで 64bit 表示になる。
    vga::vga->printf("Physical Memory Manager State:\n");
    vga::vga->printf("  Total Pages: %u\n", state.total_pages);
    vga::vga->printf("  Free Pages: %u\n", state.free_pages);
    vga::vga->printf("  Base Address: 0x%016x\n", state.base);
    vga::vga->printf("  Top Address: 0x%016x\n", state.top);
}
class PhysicalMemoryManager
{
  public:
    // memory_map / multiboot_address はどちらも GRUB が渡してくるブート情報。
    // memory_map から使用可能な物理メモリを拾い、カーネル本体 ([base, kernel_end))
    // と multiboot2 情報構造体そのものを「使用中」にして構築する。
    // 情報構造体を予約しないと、カーネル終端より後ろに置かれているぶんが
    // 普通の空きページとして配られてしまう。
    PhysicalMemoryManager(Multiboot2MemoryMapTag *memory_map, uint64_t kernel_end, uint32_t multiboot_address);
    uint64_t allocate();
    void free(uint64_t page_address);
    PhysicalMemoryManagerState get_state() const;


  private:
    // [start, end) を「使用中」にして allocate() の対象から外す。
    // ページ境界に丸めて (start は切り下げ / end は切り上げ) 予約するので、
    // 範囲にかかるページは必ず保護される。
    void reserve_region(uint64_t start, uint64_t end);

    // 物理メモリ管理の実装をここに記述 --- IGNORE ---
    uint8_t bitmap_[BITMAP_SIZE];
    uint64_t base_{0};
    uint64_t pages_{0};
    uint64_t free_pages_{0};

    void set_bit(uint64_t page_index)
    {
        bitmap_[page_index / 8] |= (1 << (page_index % 8));
    }


    void clear_bit(uint64_t page_index)
    {
        bitmap_[page_index / 8] &= ~(1 << (page_index % 8));
    }

    bool test_bit(uint64_t page_index) const
    {
        return bitmap_[page_index / 8] & (1 << (page_index % 8));
    }

    uint64_t address_to_page_index(uint64_t address) const
    {
        return (address - base_) / PAGE_SIZE;
    }

    uint64_t page_index_to_address(uint64_t page_index) const
    {
        return base_ + page_index * PAGE_SIZE;
    }
};

inline PhysicalMemoryManager *pmm_ptr = nullptr;

} // namespace pmm
