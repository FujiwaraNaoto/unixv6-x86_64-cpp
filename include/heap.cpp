#include <cstdint>
#include "heap.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

namespace heap
{
static constexpr size_t BLOCK_HEADER   = sizeof(Block);
static uint64_t ALIGN                  = 8;  // 8バイトアラインメント
static constexpr size_t MIN_SPLIT_SIZE = 32; // ブロックを分割する最小サイズ
static uint64_t virt_start_            = 0;
static uint64_t virt_end_              = 0;
static uint64_t brk_                   = 0;
static Block *head_                    = nullptr; // ブロックの先頭を指すポインタ


void *Heap::sbrk(intptr_t increment)
{
    // ここではpmmを直接呼び出して物理ページを割り当てる
    if (increment < 0)
        return (void *)-1; // 負の増分はサポートしない
    if (increment == 0)
        return reinterpret_cast<void *>(brk_); // 現在のブレークポイントを返す

    uint64_t new_brk = brk_ + increment;
    if (new_brk > virt_end_)
    {
        return (void *)-1; // 仮想アドレスの範囲
    }

    uint64_t map_start = (brk_ + pmm::PAGE_SIZE - 1) & ~(pmm::PAGE_SIZE - 1); // 次のページ境界に切り上げ

    for (uint64_t va = map_start; va < new_brk; va += pmm::PAGE_SIZE)
    {
        uint64_t pa = pmm_ptr_->allocate();
        if (pa == 0)
        {
            return (void *)-1; // 物理ページの割り当てに失敗
        }
        if (!vmm_ptr_->map_page(va, pa, vmm::PageFlag::Present | vmm::PageFlag::Writable))
        {
            return (void *)-1;
        }
    }

    void *old_brk = reinterpret_cast<void *>(brk_);
    brk_          = new_brk; // ブレークポイントを更新
    return old_brk;          // 古いブレークポイントを返す
}
void *Heap::morecore(size_t pages)
{
    if (pages == 0)
        return nullptr;
    intptr_t bytes = static_cast<intptr_t>(pages * pmm::PAGE_SIZE);
    void *ptr      = sbrk(bytes);
    if (ptr == (void *)-1)
        return nullptr;

    return ptr;
}

Heap::Heap(uint64_t virt_start,
           uint64_t virt_end,
           pmm::PhysicalMemoryManager *pmm_ptr,
           vmm::VirtualMemoryManager *vmm_ptr)
{
    virt_start_ = virt_start;
    virt_end_   = virt_end;
    brk_        = virt_start_; // ブレークポイントを初期化
    pmm_ptr_    = pmm_ptr;
    vmm_ptr_    = vmm_ptr;

    void *p = morecore(1); // 最初のページを確保
    if (!p)                // morecore は失敗時に nullptr を返す
    {
        head_ = nullptr;
        return;
    }

    head_       = reinterpret_cast<Block *>(p);
    head_->size = pmm::PAGE_SIZE - BLOCK_HEADER; // ブロックのサイズをページ
    head_->free = true;
    head_->next = nullptr;
    head_->prev = nullptr;
}

void *Heap::alloc(size_t size)
{
    if (size == 0 || head_ == nullptr)
        return nullptr;


    // サイズをブロックサイズに合わせて調整
    size_t total_size = (size + ALIGN - 1) & ~(ALIGN - 1); // アラインメントを考慮してサイズを調整

    for (Block *block = head_; block != nullptr; block = block->next)
    {
        if (block->free && block->size >= total_size)
        {
            // ブロックが十分なサイズを持っている場合
            if (block->size >= total_size + BLOCK_HEADER + MIN_SPLIT_SIZE)
            {
                // ブロックを分割
                Block *new_block = reinterpret_cast<Block *>(reinterpret_cast<uint8_t *>(block) + BLOCK_HEADER +
                                                             total_size);
                new_block->size  = block->size - total_size - BLOCK_HEADER;
                new_block->free  = true;
                new_block->next  = block->next;
                new_block->prev  = block;

                if (block->next)
                {
                    block->next->prev = new_block;
                }
                block->next = new_block;
                block->size = total_size; // ブロックのサイズを更新
            }
            block->free = false;                        // ブロックを使用中にする
            return reinterpret_cast<void *>(block + 1); // ヘッダの後ろがユーザーデータ領域
        }
    } // for

    size_t pages_needed = (total_size + BLOCK_HEADER + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE; // 必要なページ数を計算
    void *new_mem       = morecore(pages_needed);
    if (!new_mem)
    {
        return nullptr; // メモリの確保に失敗
    }

    Block *last_block = head_;
    while (last_block->next)
    {
        last_block = last_block->next;
    }

    if (last_block->free)
    {
        // 最後のブロックが空いている場合は結合
        last_block->size += pages_needed * pmm::PAGE_SIZE;
    }
    else
    {
        // 新しいブロックを作成
        Block *new_block = reinterpret_cast<Block *>(new_mem);
        new_block->size  = pages_needed * pmm::PAGE_SIZE - BLOCK_HEADER;
        new_block->free  = true;
        new_block->next  = nullptr;
        new_block->prev  = last_block;
        last_block->next = new_block;
    }

    return alloc(size); // 再度allocを呼び出してブロックを取得. 今度は必ず空きがある
}

void Heap::free(void *ptr)
{
    if (!ptr)
        return;
    auto *block = reinterpret_cast<Block *>(ptr) - 1; // ブロックのヘッダを取得
    block->free = true;                               // ブロックを解放

    // 次のブロックも空いていれば結合
    if (block->next && block->next->free)
    {
        block->size += BLOCK_HEADER + block->next->size; // 次のブロックと結合
        block->next = block->next->next;
        if (block->next)
        {
            block->next->prev = block;
        }
    }

    // 前のブロックも空いていれば結合
    if (block->prev && block->prev->free)
    {
        block->prev->size += BLOCK_HEADER + block->size; // 前のブロックと結合
        block->prev->next = block->next;
        if (block->next)
        {
            block->next->prev = block->prev;
        }
    }
}
} // namespace heap
