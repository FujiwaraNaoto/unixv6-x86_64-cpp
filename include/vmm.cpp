
#include <cstdint>
#include "vmm.hpp"
#include <cstring>

namespace
{
uint64_t pml4_index(uint64_t va)
{
    return (va >> 39) & 0x1FF;
}
uint64_t pdpt_index(uint64_t va)
{
    return (va >> 30) & 0x1FF;
}
uint64_t pd_index(uint64_t va)
{
    return (va >> 21) & 0x1FF;
}
uint64_t pt_index(uint64_t va)
{
    return (va >> 12) & 0x1FF;
}

uint64_t entry_to_phys(uint64_t entry)
{
    return entry & 0x000FFFFFFFFFF000ULL;
}

uint64_t *physical_to_virtual(uint64_t phys)
{
    return reinterpret_cast<uint64_t *>(phys);
}

extern "C" void load_cr3(uint64_t value);
extern "C" void asm_flush_tlb();

} // namespace

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
    if (!pml4)
    {
        return false; // PML4が存在しない場合はマッピングできない
    }
    // 中間テーブル(PDPT/PD/PT)にも User ビットを伝播させる必要がある。
    // 最終 PTE だけ User にしても、上位エントリのどれか一つでも User=0 なら
    // CPL=3 からのアクセスは拒否される (Intel SDM Vol.3A 4.6 "Access Rights")。
    const uint64_t table_flags = PageFlag::Present | PageFlag::Writable | (flags & PageFlag::User);
    uint64_t *pdpt             = get_or_create_table(pml4, pml4_index(virtual_address), table_flags);
    if (!pdpt)
    {
        return false; // PDPTが存在しない場合はマッピングできない
    }
    uint64_t *pd = get_or_create_table(pdpt, pdpt_index(virtual_address), table_flags);
    if (!pd)
    {
        return false; // PDが存在しない場合はマッピングできない
    }
    uint64_t *pt = get_or_create_table(pd, pd_index(virtual_address), table_flags);
    if (!pt)
    {
        return false; // PTが存在しない場合はマッピングできない
    }
    pt[pt_index(virtual_address)] = (physical_address & PAGE_MASK) | flags | PageFlag::Present;

    asm volatile("invlpg (%0)" ::"r"(virtual_address) : "memory"); // TLBフラッシュ
    return true;
}

bool VirtualMemoryManager::unmap_page(uint64_t virtual_address)
{
    uint64_t *pml4 = physical_to_virtual(pml4_phys_);

    if (!(pml4[pml4_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PML4エントリが存在しない場合はアンマッピングできない
    }

    uint64_t *pdpt = physical_to_virtual(entry_to_phys(pml4[pml4_index(virtual_address)]));

    if (!(pdpt[pdpt_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PDPTエントリが存在しない場合はアンマッピングできない
    }

    uint64_t *pd = physical_to_virtual(entry_to_phys(pdpt[pdpt_index(virtual_address)]));
    if (!(pd[pd_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PDエントリが存在しない場合はアンマッピングできない
    }
    uint64_t *pt = physical_to_virtual(entry_to_phys(pd[pd_index(virtual_address)]));
    if (!(pt[pt_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PTエントリが存在しない場合はアンマッピングできない
    }
    pt[pt_index(virtual_address)] = 0; // エントリをクリア

    asm volatile("invlpg (%0)" ::"r"(virtual_address) : "memory"); // TLBフラッシュ
    return true;
}

uint64_t VirtualMemoryManager::virtual_to_physical(uint64_t virtual_address) const
{
    uint64_t *pml4 = physical_to_virtual(pml4_phys_);
    if (!(pml4[pml4_index(virtual_address)] & PageFlag::Present))
    {
        return 0; // PML4エントリが存在しない場合は物理アドレスを返せない
    }
    uint64_t *pdpt = physical_to_virtual(entry_to_phys(pml4[pml4_index(virtual_address)]));
    if (!(pdpt[pdpt_index(virtual_address)] & PageFlag::Present))
    {
        return 0; // PDPTエントリが存在しない場合は物理アドレスを返せない
    }
    uint64_t *pd = physical_to_virtual(entry_to_phys(pdpt[pdpt_index(virtual_address)]));
    if (!(pd[pd_index(virtual_address)] & PageFlag::Present))
    {
        return 0; // PDエントリが存在しない場合は物理アドレスを返せない
    }
    uint64_t *pt = physical_to_virtual(entry_to_phys(pd[pd_index(virtual_address)]));
    if (!(pt[pt_index(virtual_address)] & PageFlag::Present))
    {
        return 0; // PTエントリが存在しない場合は物理アドレスを返せない
    }
    return entry_to_phys(pt[pt_index(virtual_address)]) | (virtual_address & ~PAGE_MASK);
}

void VirtualMemoryManager::flush_tlb()
{
    asm_flush_tlb(); // CR3を再ロードしてTLBをフラッシュする
}


uint64_t *VirtualMemoryManager::get_or_create_table(uint64_t *parent_table, uint64_t index, uint64_t flags)
{
    if (!(parent_table[index] & PageFlag::Present))
    {
        // 新しいテーブルを割り当てる
        uint64_t new_table_phys = pmm_ptr_->allocate(); // 物理ページの割り当て関数
        parent_table[index]     = new_table_phys | flags;
    }
    else
    {
        // 既存エントリには User / Writable を追加で立てる。
        // ブートローダが作った中間テーブルは User=0 なので、これをしないと
        // 配下を User マップしても CPL=3 からアクセスできない。
        parent_table[index] |= (flags & (PageFlag::User | PageFlag::Writable));
    }
    return physical_to_virtual(entry_to_phys(parent_table[index]));
}

uint64_t VirtualMemoryManager::create_address_space()
{
    uint64_t new_pml4_phys = pmm_ptr_->allocate();
    if (new_pml4_phys == 0)
    {
        return 0; // メモリ不足
    }
    uint64_t *new_pml4 = physical_to_virtual(new_pml4_phys);
    auto *current_pml4 = physical_to_virtual(pml4_phys_);

    memset(new_pml4, 0, 512 * sizeof(uint64_t)); // 新しいPML4をゼロクリア


    // カーネル空間を共有:
    // PML4[0] : カーネルコード/データ(identity mapping)を共有
    // PML4[256..511] : 上位半分(将来のカーネル空間)を共有
    new_pml4[0] = current_pml4[0]; // カーネル空間のマッピングをコピー
    memcpy(&new_pml4[256], &current_pml4[256], 256 * sizeof(uint64_t)); // カーネル空間のマッピングをコピー
    return new_pml4_phys;
}

// ─── アドレス空間の切り替え ──────────────────────────────────────
void VirtualMemoryManager::switch_address_space(uint64_t pml4_phys)
{
    if (pml4_phys == 0)
    {
        return; // 無効なPML4物理アドレスは無視
    }
    pml4_phys_ = pml4_phys;
    load_cr3(pml4_phys); // CR3を切り替えてTLB(=Translation Lookaside Buffer)をフラッシュ
}


bool VirtualMemoryManager::map_page_in(uint64_t pml4_phys,
                                       uint64_t virtual_address,
                                       uint64_t physical_address,
                                       uint64_t flags)
{

    auto *original_pml4 = physical_to_virtual(pml4_phys);

    auto *pdpt = get_or_create_table(original_pml4,
                                     pml4_index(virtual_address),
                                     PageFlag::Present | PageFlag::Writable | PageFlag::User);

    if (!pdpt)
    {
        return false; // PDPTが存在しない場合はマッピングできない
    }
    auto *pd =
        get_or_create_table(pdpt, pdpt_index(virtual_address), PageFlag::Present | PageFlag::Writable | PageFlag::User);
    if (!pd)
    {
        return false; // PDが存在しない場合はマッピングできない
    }

    auto *pt =
        get_or_create_table(pd, pd_index(virtual_address), PageFlag::Present | PageFlag::Writable | PageFlag::User);
    if (!pt)
    {
        return false; // PTが存在しない場合はマッピングできない
    }

    pt[pt_index(virtual_address)] = (physical_address & PAGE_MASK) | flags | PageFlag::Present;
    return true;
}

void copy_user_pages(uint64_t src_pml4_phys, uint64_t dst_pml4_phys)
{
    auto *src = physical_to_virtual(src_pml4_phys);

    if (!(src[0] & PageFlag::Present))
    {
        return; // PML4エントリが存在しない場合はコピーできない
    }

    auto src_pdpt = physical_to_virtual(entry_to_phys(src[0]));


    for (int i = 0; i < 512; i++)
    {
        if (!(src_pdpt[i] & PageFlag::Present))
        {
            continue; // PDPTエントリが存在しない場合はコピーできない
        }
        auto *src_pd = physical_to_virtual(entry_to_phys(src_pdpt[i]));

        for (int j = 0; j < 512; j++)
        {
            if (!(src_pd[j] & PageFlag::Present))
            {
                continue; // PDエントリが存在しない場合はコピーできない
            }
            auto *src_pt = physical_to_virtual(entry_to_phys(src_pd[j]));

            for (int k = 0; k < 512; k++)
            {

                uint64_t e = src_pt[k];
                if (!(e & PageFlag::Present))
                {
                    continue; // PTエントリが存在しない場合はコピーできない
                }
                if (!(e & PageFlag::User))
                {
                    continue; // Userページでない場合はコピーしない
                }

                auto shift = [](uint64_t e, int shift) -> uint64_t { return e << shift; };

                uint64_t virtual_address = shift(i, 30) | shift(j, 21) |
                                           shift(k, 12); // PDPTのインデックスを仮想アドレスに変換

                uint64_t new_phys = vmm_ptr->allocate_page(); // 新しい物理ページを割り当てる
                if (new_phys == 0)
                {
                    return; // メモリ不足
                }

                auto *dist_page = physical_to_virtual(new_phys);
                auto *src_page  = physical_to_virtual(entry_to_phys(e));
                for (int l = 0; l < 512; l++)
                {
                    dist_page[l] = src_page[l]; // ページの内容をコピー
                }

                //子供のPML4に同じ仮想アドレスでマップ
                uint64_t flags = e & 0xFFF;
                map_page_in(dst_pml4_phys, virtual_address, new_phys, flags);
            }
        }
    }

    for (int i = 0; i < 512; i++)
    {
        uint64_t e = src_pdpt[i];
        if (!(e & PageFlag::Present))
        {
            continue; // PDPTエントリが存在しない場合はコピーできない
        }
        if (!(e & PageFlag::User))
        {
            continue; // Userページでない場合はコピーしない
        }

        auto shift = [](uint64_t e, int shift) -> uint64_t { return e << shift; };

        uint64_t virtual_address = shift(i, 30) | shift(j, 21) | shift(k, 12); // PDPTのインデックスを仮想アドレスに変換
    }
}


} // namespace vmm
