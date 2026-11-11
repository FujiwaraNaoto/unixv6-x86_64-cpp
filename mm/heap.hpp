#ifndef MM_HEAP_HPP
#define MM_HEAP_HPP
#include <cstdint>
#include "units.hpp"
#include <cstddef>
#include "asm/memory_layout.hpp"

// Heap はポインタとして持つだけなので、ヘッダは読み込まず前方宣言で済ませる。
// (pmm.hpp / vmm.hpp を読み込むと、heap.hpp を使う側にまで依存が広がる)
namespace pmm
{
class PhysicalMemoryManager;
}
namespace vmm
{
class VirtualMemoryManager;
}

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
    Heap(uint64_t virt_start,
         uint64_t virt_end,
         pmm::PhysicalMemoryManager *pmm_ptr,
         vmm::VirtualMemoryManager *vmm_ptr);
    void *allocate(size_t size);
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

#endif // MM_HEAP_HPP
