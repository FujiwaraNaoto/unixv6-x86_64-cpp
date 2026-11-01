#include "usermode.hpp"
#include "vga.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

extern "C" [[noreturn]] void enter_usermode(uint64_t entry, uint64_t user_stack);

// .user セクションの範囲 (kernel.ld が定義する)。この中だけを User 許可でマップする。
extern "C" char __user_start[];
extern "C" char __user_end[];

namespace usermode
{

bool map_user_space(PhysicalAddress pml4)
{
    // リング3で実行するコード。書き込みは許可しない。
    for (uint64_t va = reinterpret_cast<uint64_t>(__user_start) & PAGE_MASK;
         va < reinterpret_cast<uint64_t>(__user_end);
         va += PAGE_SIZE)
    {
        const PhysicalAddress phys = vmm::vmm_ptr->virtual_to_physical(PageVirtualAddress{va});
        if (!phys)
        {
            return false;
        }
        if (!vmm::vmm_ptr->map_page_in(
                pml4, PageVirtualAddress{va}, phys, vmm::PageFlag::Present | vmm::PageFlag::User))
        {
            return false;
        }
    }

    const PhysicalAddress stack_phys = pmm::pmm_ptr->allocate();
    if (!stack_phys)
    {
        return false;
    }
    if (!vmm::vmm_ptr->map_page_in(
            pml4, kUserStack, stack_phys, vmm::PageFlag::Present | vmm::PageFlag::Writable | vmm::PageFlag::User))
    {
        pmm::pmm_ptr->free(stack_phys);
        return false;
    }
    return true;
}

void enter(uint64_t entry, uint64_t user_stack)
{
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[USER] ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->puts("entering ring 3...\n");
    enter_usermode(entry, user_stack);
}

} // namespace usermode
