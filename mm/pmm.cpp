#include "pmm.hpp"
#include "multiboot2.hpp"
#include <cstdint>
#include "address.hpp"


namespace pmm
{

PhysicalMemoryManager::PhysicalMemoryManager(const MemoryRegion *usable,
                                             size_t usable_count,
                                             const MemoryRegion *reserved,
                                             size_t reserved_count)
{
    // initialize bitmap (all pages used)
    for (auto &b : bitmap_)
    {
        b = 0xFF;
    }

    uint64_t total = 0, base_found = 0;
    for (size_t i = 0; i < usable_count; i++)
    {
        // ~(PAGE_SIZE - 1) は PAGE_SIZE でアラインメントするためのマスク
        uint64_t start = (usable[i].start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); //切り上げ
        uint64_t end   = usable[i].end & ~(PAGE_SIZE - 1);                     //切り捨て
        if (start >= end)
            continue;
        if (total == 0)
        {
            base_found = start;
        }
        total += (end - start) / PAGE_SIZE;
    }
    if (total == 0)
        return;
    base_  = base_found;
    pages_ = (total > MAX_PAGES) ? MAX_PAGES : total;

    // usable なページを解放
    for (size_t i = 0; i < usable_count; i++)
    {
        uint64_t start = (usable[i].start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t end   = usable[i].end & ~(PAGE_SIZE - 1);
        if (start >= end)
            continue;
        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE)
        {
            free(PhysicalAddress{addr}); // release the page to the free list
        }
    }

    // NULL ページを使用中にする
    // 物理アドレス0は通常使用されないが、誤ってアクセスされたときにすぐにわかるようにするため
    // 0x0000 ~ 0x03FF
    // はリアルモード割り込みベクタテーブルやBIOSデータエリアなどで使用されることがあるため、これらも使用中にする
    if (base_ == 0)
    {
        reserve_region(0, PAGE_SIZE);
    }

    // カーネル本体やブート情報など、usable の中でも配ってはいけない範囲を使用中にする
    for (size_t i = 0; i < reserved_count; i++)
    {
        reserve_region(reserved[i].start, reserved[i].end);
    }
}

void PhysicalMemoryManager::free(PhysicalAddress page_address)
{
    if (!page_address || *page_address.address < base_)
        return;
    uint64_t page_index = address_to_page_index(*page_address.address);
    if (page_index >= MAX_PAGES)
        return;
    if (test_bit(page_index))
    {
        clear_bit(page_index);
        free_pages_++;
    }
}

void PhysicalMemoryManager::reserve_region(uint64_t start, uint64_t end)
{
    if (end <= start)
        return;

    // start は切り下げ、end は切り上げ。範囲に少しでもかかるページを全部押さえる。
    uint64_t first_page = start & ~(PAGE_SIZE - 1);
    uint64_t last_page  = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t addr = first_page; addr < last_page; addr += PAGE_SIZE)
    {
        if (addr < base_)
            continue;
        uint64_t page_index = address_to_page_index(addr);
        if (page_index >= pages_)
            continue;
        if (!test_bit(page_index)) // 既に使用中なら二重に減らさない
        {
            set_bit(page_index);
            if (free_pages_ > 0)
                free_pages_--;
        }
    }
}

// bitが0のページを先頭から探し、未使用ページが見つかったらそのページを使用中にして物理アドレスを返す。
PhysicalAddress PhysicalMemoryManager::allocate()
{
    for (uint64_t i = 0; i < pages_; i++)
    {
        if (!test_bit(i))
        {
            set_bit(i);
            free_pages_--;
            return PhysicalAddress{base_ + i * PAGE_SIZE};
        }
    }
    return PhysicalAddress{}; // メモリ不足
}

PhysicalMemoryManagerState PhysicalMemoryManager::get_state() const
{
    return PhysicalMemoryManagerState(pages_, free_pages_, base_, base_ + pages_ * PAGE_SIZE);
}
} // namespace pmm
