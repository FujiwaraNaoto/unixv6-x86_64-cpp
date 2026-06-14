#pragma once
#include <cstdint>
#include "pmm.hpp"

namespace PageFlag {
    constexpr uint64_t Present   = 1ULL << 0;
    constexpr uint64_t Writable  = 1ULL << 1;
    constexpr uint64_t User      = 1ULL << 2;  // リング3からアクセス可
    constexpr uint64_t Accessed  = 1ULL << 5;
    constexpr uint64_t Dirty     = 1ULL << 6;
    constexpr uint64_t HugePage  = 1ULL << 7;
    constexpr uint64_t NoExecute = 1ULL << 63;
}

constexpr uint64_t PAGE_SIZE  = 4096;
constexpr uint64_t PAGE_MASK  = ~(PAGE_SIZE - 1);



namespace vmm
{
    

    class VirtualMemoryManager final
    {
      public:
        VirtualMemoryManager(pmm::PhysicalMemoryManager *pmm_ptr);
        bool map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
        // map解除とTLBフラッシュ
        bool unmap_page(uint64_t virtual_address);
        uint64_t virtual_to_physical(uint64_t virtual_address) const;

        void flush_tlb();

      private:
        uint64_t *get_or_create_table(uint64_t *parent_table, uint64_t index, uint64_t flags);

        uint64_t pml4_phys_ = 0;
        pmm::PhysicalMemoryManager *pmm_ptr_ = nullptr;

    };

    VirtualMemoryManager *vmm_ptr = nullptr;
    
}
