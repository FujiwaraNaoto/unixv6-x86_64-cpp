
#include <cstdint>
#include "vmm.hpp"

namespace
{
    uint64_t pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
    uint64_t pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
    uint64_t pd_index  (uint64_t va) { return (va >> 21) & 0x1FF; }
    uint64_t pt_index  (uint64_t va) { return (va >> 12) & 0x1FF; }

    uint64_t entry_to_phys(uint64_t entry) {
        return entry & 0x000FFFFFFFFFF000ULL;
    }

    uint64_t *physical_to_virtual(uint64_t phys) {
        return reinterpret_cast<uint64_t *>(phys);
    }

    
}

namespace vmm
{

    VirtualMemoryManager::VirtualMemoryManager(pmm::PhysicalMemoryManager *pmm_ptr)
    {
        this->pmm_ptr_ = pmm_ptr;
        asm volatile("mov %%cr3, %0" : "=r"(pml4_phys_));
    }

    bool VirtualMemoryManager::map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags)
    {
        uint64_t *pml4 = physical_to_virtual(pml4_phys_);
        if(!pml4){
            return false; // PML4が存在しない場合はマッピングできない
        }
        uint64_t *pdpt = get_or_create_table(pml4, pml4_index(virtual_address),PageFlag::Present|PageFlag::Writable);
        if(!pdpt){
            return false; // PDPTが存在しない場合はマッピングできない
        }
        uint64_t *pd   = get_or_create_table(pdpt, pdpt_index(virtual_address),PageFlag::Present|PageFlag::Writable);
        if(!pd){
            return false; // PDが存在しない場合はマッピングできない
        }
        uint64_t *pt   = get_or_create_table(pd, pd_index(virtual_address),PageFlag::Present|PageFlag::Writable);
        if(!pt){
            return false; // PTが存在しない場合はマッピングできない
        }
        pt[pt_index(virtual_address)] = (physical_address & PAGE_MASK) | flags | PageFlag::Present;
        
        asm volatile("invlpg (%0)" ::"r"(virtual_address) : "memory"); // TLBフラッシュ
        return true;
    }

    bool VirtualMemoryManager::unmap_page(uint64_t virtual_address)
    {
        uint64_t *pml4 = physical_to_virtual(pml4_phys_);
        
        if(!(pml4[pml4_index(virtual_address)] & PageFlag::Present)){
            return false; // PML4エントリが存在しない場合はアンマッピングできない
        }

        uint64_t *pdpt = physical_to_virtual(entry_to_phys(pml4[pml4_index(virtual_address)]));
        
        if(!(pdpt[pdpt_index(virtual_address)] & PageFlag::Present)){
            return false; // PDPTエントリが存在しない場合はアンマッピングできない
        }

        uint64_t *pd   = physical_to_virtual(entry_to_phys(pdpt[pdpt_index(virtual_address)]));
        if(!(pd[pd_index(virtual_address)] & PageFlag::Present)){
            return false; // PDエントリが存在しない場合はアンマッピングできない
        }
        uint64_t *pt   = physical_to_virtual(entry_to_phys(pd[pd_index(virtual_address)]));
        if(!(pt[pt_index(virtual_address)] & PageFlag::Present)){
            return false; // PTエントリが存在しない場合はアンマッピングできない
        }
        pt[pt_index(virtual_address)] = 0; // エントリをクリア
        
        asm volatile("invlpg (%0)" ::"r"(virtual_address) : "memory"); // TLBフラッシュ
        return true;
    }

    uint64_t VirtualMemoryManager::virtual_to_physical(uint64_t virtual_address) const{
        uint64_t *pml4 = physical_to_virtual(pml4_phys_);
        if(!(pml4[pml4_index(virtual_address)] & PageFlag::Present)){
            return 0; // PML4エントリが存在しない場合は物理アドレスを返せない
        }
        uint64_t *pdpt = physical_to_virtual(entry_to_phys(pml4[pml4_index(virtual_address)]));
        if(!(pdpt[pdpt_index(virtual_address)] & PageFlag::Present)){
            return 0; // PDPTエントリが存在しない場合は物理アドレスを返せない
        }
        uint64_t *pd   = physical_to_virtual(entry_to_phys(pdpt[pdpt_index(virtual_address)]));
        if(!(pd[pd_index(virtual_address)] & PageFlag::Present)){
            return 0; // PDエントリが存在しない場合は物理アドレスを返せない
        }
        uint64_t *pt   = physical_to_virtual(entry_to_phys(pd[pd_index(virtual_address)]));
        if(!(pt[pt_index(virtual_address)] & PageFlag::Present)){
            return 0; // PTエントリが存在しない場合は物理アドレスを返せない
        }
        return entry_to_phys(pt[pt_index(virtual_address)]) | (virtual_address & ~PAGE_MASK);
    }

    void VirtualMemoryManager::flush_tlb(){
        uint64_t cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory"); // CR3を再ロードしてTLBをフラッシュ
    }


    uint64_t *VirtualMemoryManager::get_or_create_table(uint64_t *parent_table, uint64_t index, uint64_t flags) {
        if (!(parent_table[index] & PageFlag::Present)) {
            // 新しいテーブルを割り当てる
            uint64_t new_table_phys = pmm_ptr_->allocate(); // 物理ページの割り当て関数
            parent_table[index] = new_table_phys | flags;
        }
        return physical_to_virtual(entry_to_phys(parent_table[index]));
    }

}
