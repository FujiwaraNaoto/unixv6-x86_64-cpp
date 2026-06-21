#pragma once
#include <cstdint>
#include <cstddef>
#include "pmm.hpp"
#include "vmm.hpp"
namespace heap
{
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
    Heap(uint64_t heap_start,
         uint64_t heap_size,
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
