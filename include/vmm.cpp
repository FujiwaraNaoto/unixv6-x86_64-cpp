
#include <cstdint>
#include "vmm.hpp"
#include <cstring>

//  63      48 47    39 38    30 29    21 20    12 11         0
// ┌──────────┬────────┬────────┬────────┬────────┬────────────┐
// │  符号拡張 │  PML4  │  PDPT  │   PD   │   PT   │  ページ内   │
// │          │  9bit  │  9bit  │  9bit  │  9bit  │ 12bit      │
// └──────────┴────────┴────────┴────────┴────────┴────────────┘
//    (va >> 39) & 0x1FF ─┘        ...              4096 バイト内の位置

namespace
{
// 表 1 枚のエントリ数 (インデックスが 9bit なので 2^9)。
// 8 バイト × 512 = 4096 バイトで、表 1 枚がちょうど 1 ページに収まる。
// PDPT / PD / PT の各表も同じく 2^9 エントリで、1 ページに収まる。
constexpr int ENTRIES_PER_TABLE = 1<<9;

uint64_t pml4_index(PageVirtualAddress va)
{
    return (va.address >> 39) & 0x1FF;
}
uint64_t pdpt_index(PageVirtualAddress va)
{
    return (va.address >> 30) & 0x1FF;
}
uint64_t pd_index(PageVirtualAddress va)
{
    return (va.address >> 21) & 0x1FF;
}
uint64_t pt_index(PageVirtualAddress va)
{
    return (va.address >> 12) & 0x1FF;
}
// ページテーブルのエントリからフラグを取り除いて物理アドレスだけを取り出す関数
PhysicalAddress entry_to_phys(uint64_t entry)
{
    return PhysicalAddress{entry & 0x000FFFFFFFFFF000ULL};
}
// direct map経由: 物理アドレス + DIRECT_MAP_BASE = 仮想アドレス
// 無効な物理アドレス (nullopt) は nullptr の仮想アドレスになる。
VirtualAddress physical_to_virtual(PhysicalAddress phys)
{
    if (!phys.address)
    {
        return VirtualAddress{nullptr};
    }
    return VirtualAddress{reinterpret_cast<uint64_t *>(*phys.address + DIRECT_MAP_BASE)};
}

extern "C" void load_cr3(uint64_t value);
extern "C" uint64_t read_cr3();
extern "C" void asm_flush_tlb();

} // namespace

namespace vmm
{

VirtualMemoryManager::VirtualMemoryManager(pmm::PhysicalMemoryManager *pmm_ptr, IConsole *console)
{
    this->pmm_ptr_ = pmm_ptr;
    pml4_phys_     = PhysicalAddress{read_cr3()}; // CR3の値を読み込む
    if (console != nullptr)
    {
        console->printf("PML4 physical address: 0x%016lx, direct map at 0x%016lx\n", *pml4_phys_.address, DIRECT_MAP_BASE);
    }
}

bool VirtualMemoryManager::map_page(PageVirtualAddress virtual_address, PhysicalAddress physical_address, PageFlag flags)
{
    if (!physical_address)
    {
        return false; // 無効な物理アドレスはマップできない
    }
    VirtualAddress pml4 = physical_to_virtual(pml4_phys_);
    if (!pml4)
    {
        return false; // PML4が存在しない場合はマッピングできない
    }
    // 中間テーブル(PDPT/PD/PT)にも User ビットを伝播させる必要がある。
    // 最終 PTE だけ User にしても、上位エントリのどれか一つでも User=0 なら
    // CPL=3 からのアクセスは拒否される (Intel SDM Vol.3A 4.6 "Access Rights")。
    const PageFlag table_flags = PageFlag::Present | PageFlag::Writable | (flags & PageFlag::User);
    VirtualAddress pdpt        = get_or_create_table(pml4, pml4_index(virtual_address), table_flags);
    if (!pdpt)
    {
        return false; // PDPTが存在しない場合はマッピングできない
    }
    VirtualAddress pd = get_or_create_table(pdpt, pdpt_index(virtual_address), table_flags);
    if (!pd)
    {
        return false; // PDが存在しない場合はマッピングできない
    }
    VirtualAddress pt = get_or_create_table(pd, pd_index(virtual_address), table_flags);
    if (!pt)
    {
        return false; // PTが存在しない場合はマッピングできない
    }
    pt[pt_index(virtual_address)] = (*physical_address.address & PAGE_MASK) | flags | PageFlag::Present;

    asm volatile("invlpg (%0)" ::"r"(virtual_address.address) : "memory"); // TLBフラッシュ
    return true;
}

bool VirtualMemoryManager::unmap_page(PageVirtualAddress virtual_address)
{
    VirtualAddress pml4 = physical_to_virtual(pml4_phys_);

    if (!(pml4[pml4_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PML4エントリが存在しない場合はアンマッピングできない
    }

    VirtualAddress pdpt = physical_to_virtual(entry_to_phys(pml4[pml4_index(virtual_address)]));

    if (!(pdpt[pdpt_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PDPTエントリが存在しない場合はアンマッピングできない
    }

    VirtualAddress pd = physical_to_virtual(entry_to_phys(pdpt[pdpt_index(virtual_address)]));
    if (!(pd[pd_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PDエントリが存在しない場合はアンマッピングできない
    }
    VirtualAddress pt = physical_to_virtual(entry_to_phys(pd[pd_index(virtual_address)]));
    if (!(pt[pt_index(virtual_address)] & PageFlag::Present))
    {
        return false; // PTエントリが存在しない場合はアンマッピングできない
    }
    pt[pt_index(virtual_address)] = 0; // エントリをクリア

    asm volatile("invlpg (%0)" ::"r"(virtual_address.address) : "memory"); // TLBフラッシュ
    return true;
}

PhysicalAddress VirtualMemoryManager::virtual_to_physical(PageVirtualAddress virtual_address) const
{
    VirtualAddress pml4 = physical_to_virtual(pml4_phys_);
    if (!(pml4[pml4_index(virtual_address)] & PageFlag::Present))
    {
        return PhysicalAddress{}; // PML4エントリが存在しない場合は物理アドレスを返せない
    }
    VirtualAddress pdpt = physical_to_virtual(entry_to_phys(pml4[pml4_index(virtual_address)]));
    if (!(pdpt[pdpt_index(virtual_address)] & PageFlag::Present))
    {
        return PhysicalAddress{}; // PDPTエントリが存在しない場合は物理アドレスを返せない
    }
    VirtualAddress pd = physical_to_virtual(entry_to_phys(pdpt[pdpt_index(virtual_address)]));
    if (!(pd[pd_index(virtual_address)] & PageFlag::Present))
    {
        return PhysicalAddress{}; // PDエントリが存在しない場合は物理アドレスを返せない
    }
    VirtualAddress pt = physical_to_virtual(entry_to_phys(pd[pd_index(virtual_address)]));
    if (!(pt[pt_index(virtual_address)] & PageFlag::Present))
    {
        return PhysicalAddress{}; // PTエントリが存在しない場合は物理アドレスを返せない
    }
    return PhysicalAddress{*entry_to_phys(pt[pt_index(virtual_address)]).address | (virtual_address.address & ~PAGE_MASK)};
}

void VirtualMemoryManager::flush_tlb()
{
    asm_flush_tlb(); // CR3を再ロードしてTLBをフラッシュする
}


VirtualAddress VirtualMemoryManager::get_or_create_table(VirtualAddress parent_table, uint64_t index, PageFlag flags)
{
    if (!(parent_table[index] & PageFlag::Present))
    {
        // 新しいテーブルを割り当てる
        const PhysicalAddress new_table_phys = pmm_ptr_->allocate(); // 物理ページの割り当て関数
        if (!new_table_phys)
        {
            return VirtualAddress{nullptr}; // 物理メモリ不足。確保できないまま Present なエントリを作らないこと
        }

        // PMM が配るページには前の用途のゴミが残っている (カーネル直後の領域には
        // GRUB が置いたデータなどが入っている)。ゼロクリアせずに配下のテーブルとして
        // 使うと、ゴミのエントリの Present ビットが偶然立っているところを
        // 「既存のテーブル」とみなして追いかけ、RAM の外を指すアドレスに書きに行く。
        std::memset(physical_to_virtual(new_table_phys).ptr, 0, PAGE_SIZE);

        parent_table[index] = *new_table_phys.address | flags;
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

PhysicalAddress VirtualMemoryManager::create_address_space()
{
    const PhysicalAddress new_pml4_phys = pmm_ptr_->allocate();
    if (!new_pml4_phys)
    {
        return PhysicalAddress{}; // メモリ不足
    }
    VirtualAddress new_pml4 = physical_to_virtual(new_pml4_phys);
    VirtualAddress current_pml4 = physical_to_virtual(pml4_phys_);

    std::memset(new_pml4.ptr, 0, PAGE_SIZE); // 新しいPML4をゼロクリア (表 1 枚 = 1 ページ)


    // カーネル空間を共有:
    // PML4[256..511] : 上位半分 (direct map + 高位カーネル) を共有
    //
    // PML4[0] はコピーしない。低位 identity map はブート直後に撤去済みで、
    // このスロットはプロセスごとのユーザ空間専用になっている。
    // (コピーすると fork のように「親を current にしたまま呼ぶ」場合に
    //  親のユーザ用 PDPT を子が共有してしまい、以降の map_page_in が
    //  親のテーブルを書き換えることになる)
    memcpy(&new_pml4[256], &current_pml4[256], 256 * sizeof(uint64_t)); // カーネル空間のマッピングをコピー
    return new_pml4_phys;
}

// ─── アドレス空間の切り替え ──────────────────────────────────────
// CR3を切り替えることで、プロセスを切り替えることを実現する
// NOTE: 実行中のコードスタックは、切り替え後のアドレス空間にマップされている必要がある。
void VirtualMemoryManager::switch_address_space(PhysicalAddress pml4_phys)
{
    if (!pml4_phys)
    {
        return; // 無効なPML4物理アドレスは無視
    }
    pml4_phys_ = pml4_phys;
    load_cr3(*pml4_phys.address); // CR3を切り替えてTLB(=Translation Lookaside Buffer)をフラッシュ
}


bool VirtualMemoryManager::map_page_in(PhysicalAddress pml4_phys,
                                       PageVirtualAddress virtual_address,
                                       PhysicalAddress physical_address,
                                       PageFlag flags)
{
    if (!physical_address)
    {
        return false; // 無効な物理アドレスはマップできない
    }

    VirtualAddress original_pml4 = physical_to_virtual(pml4_phys);
    if (!original_pml4)
    {
        return false; // PML4が存在しない場合はマッピングできない
    }

    VirtualAddress pdpt = get_or_create_table(original_pml4,
                                     pml4_index(virtual_address),
                                     PageFlag::Present | PageFlag::Writable | PageFlag::User);

    if (!pdpt)
    {
        return false; // PDPTが存在しない場合はマッピングできない
    }
    VirtualAddress pd =
        get_or_create_table(pdpt, pdpt_index(virtual_address), PageFlag::Present | PageFlag::Writable | PageFlag::User);
    if (!pd)
    {
        return false; // PDが存在しない場合はマッピングできない
    }

    VirtualAddress pt =
        get_or_create_table(pd, pd_index(virtual_address), PageFlag::Present | PageFlag::Writable | PageFlag::User);
    if (!pt)
    {
        return false; // PTが存在しない場合はマッピングできない
    }

    pt[pt_index(virtual_address)] = (*physical_address.address & PAGE_MASK) | flags | PageFlag::Present;
    return true;
}

void VirtualMemoryManager::copy_user_pages(PhysicalAddress src_pml4_phys, PhysicalAddress dst_pml4_phys)
{
    VirtualAddress src = physical_to_virtual(src_pml4_phys);
    if (!src)
    {
        return; // PML4が存在しない場合はコピーできない
    }

    if (!(src[0] & PageFlag::Present))
    {
        return; // PML4エントリが存在しない場合はコピーできない
    }

    VirtualAddress src_pdpt = physical_to_virtual(entry_to_phys(src[0]));


    for (int i = 0; i < ENTRIES_PER_TABLE; i++)
    {
        if (!(src_pdpt[i] & PageFlag::Present))
        {
            continue; // PDPTエントリが存在しない場合はコピーできない
        }
        VirtualAddress src_pd = physical_to_virtual(entry_to_phys(src_pdpt[i]));

        for (int j = 0; j < ENTRIES_PER_TABLE; j++)
        {
            if (!(src_pd[j] & PageFlag::Present))
            {
                continue; // PDエントリが存在しない場合はコピーできない
            }
            VirtualAddress src_pt = physical_to_virtual(entry_to_phys(src_pd[j]));

            for (int k = 0; k < ENTRIES_PER_TABLE; k++)
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

                const PageVirtualAddress virtual_address{shift(i, 30) | shift(j, 21) |
                                                         shift(k, 12)}; // PDPTのインデックスを仮想アドレスに変換

                const PhysicalAddress new_phys = pmm::pmm_ptr->allocate(); // 新しい物理ページを割り当てて内容をコピーする
                if (!new_phys)
                {
                    return; // メモリ不足
                }

                VirtualAddress dist_page = physical_to_virtual(new_phys);
                VirtualAddress src_page  = physical_to_virtual(entry_to_phys(e));

                std::memcpy(dist_page.ptr, src_page.ptr, PAGE_SIZE);

                //子供のPML4に同じ仮想アドレスでマップ
                // フラグは親のエントリの下位 12 ビットをそのまま引き継ぐ
                const PageFlag flags = static_cast<PageFlag>(e & 0xFFF);
                vmm::vmm_ptr->map_page_in(dst_pml4_phys, virtual_address, new_phys, flags);
            }
        }
    }
}


} // namespace vmm
