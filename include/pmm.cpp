#include "pmm.hpp"

namespace 
{
    
}


namespace pmm
{
    PhysicalMemoryManager::PhysicalMemoryManager(Multiboot2MemoryMapTag *memory_map,uint64_t kernel_end)
    {
        for(uint32_t i=0;i<BITMAP_SIZE;i++){
            bitmap_[i] = 0xFF;
        }

        uint64_t total=0,base_found = 0;
        uint32_t num_page = (memory_map->size - sizeof(Multiboot2MemoryMapTag)) / memory_map->entry_size;
        for(uint32_t i=0;i<num_page;i++){
            auto entry =  reinterpret_cast<Multiboot2MemoryMapEntry *>(reinterpret_cast<uint8_t *>(memory_map->entries) + i * memory_map->entry_size);
            if(entry->type !=MULTIBOOT2_MEMORY_AVAILABLE ) continue;
            uint64_t start = (entry->base_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end = (entry->base_addr + entry->length) & ~(PAGE_SIZE - 1);
            if(start >= end) continue;
            if(total == 0){
                base_found = start;
            }
            total += (end - start) / PAGE_SIZE;

        }
        if(total==0) return;
        base_ = base_found;
        pages_ =(total>MAX_PAGES)? MAX_PAGES : total;

        //availableなページを解放
        for(uint32_t i=0;i<num_page;i++){
            auto entry =  reinterpret_cast<Multiboot2MemoryMapEntry *>(reinterpret_cast<uint8_t *>(memory_map->entries) + i * memory_map->entry_size);
            if(entry->type !=MULTIBOOT2_MEMORY_AVAILABLE ) continue;
            uint64_t start = (entry->base_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t end = (entry->base_addr + entry->length) & ~(PAGE_SIZE - 1);
            if(start >= end) continue;
            for(uint64_t addr=start;addr<end;addr+=PAGE_SIZE){
                if(addr < kernel_end) continue; // カーネル領域はスキップ
                free(addr);
            }
        }

        // NULL ページを使用中にする
        if(base_ == 0){
            set_bit(0); // ベースアドレスが0の場合、最初のページを使用中にする
            if(free_pages_ > 0) free_pages_--;
        }

        uint64_t kernel_end_page = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for(uint64_t addr=base_;addr<kernel_end_page;addr+=PAGE_SIZE){
            uint64_t page_idx = address_to_page_index(addr);
            if(page_idx < pages_){
                set_bit(page_idx); // カーネル領域を使用中にする
                if(free_pages_ > 0) free_pages_--;
            }
        }

        
    }

    void PhysicalMemoryManager::free(uint64_t page_address){
        if(page_address < base_) return;
        uint64_t page_index = address_to_page_index(page_address);
        if(page_index >= MAX_PAGES) return;
        if(test_bit(page_index)) {
            clear_bit(page_index);
            free_pages_++;
        }

    }

    uint64_t PhysicalMemoryManager::allocate()
    {
        for(uint64_t i=0;i<pages_;i++){
            if(!test_bit(i)){
                set_bit(i);
                free_pages_--;
                return base_ + i * PAGE_SIZE;
            }
        }
        return 0; // メモリ不足
    }

    PhysicalMemoryManagerState PhysicalMemoryManager::get_state() const
    {
        return PhysicalMemoryManagerState(pages_, pages_ - free_pages_, base_, base_ + pages_ * PAGE_SIZE);
    }
}
