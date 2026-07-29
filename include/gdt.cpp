#include "gdt.hpp"
#include <cstring>
#include <array>
#include "vga.hpp"

namespace
{
extern "C" void LoadTR(uint16_t sel); // Load Task Register with the given selector
}

namespace
{
static std::array<gdt::GlobalDescriptorTableEntry, 7> gdt_entries;
static gdt::TaskStateSegment tss;
static gdt::GlobalDescriptorTablePointer gdt_ptr;
} // namespace

namespace
{
void set_entry(int index, uint8_t access, uint8_t granularity, uint32_t base = 0, uint32_t limit = 0)
{
    gdt::GlobalDescriptorTableEntry *entry = &gdt_entries[index];
    entry->limit_low                       = limit & 0xFFFF;
    entry->base_low                        = base & 0xFFFF;
    entry->base_middle                     = (base >> 16) & 0xFF;
    entry->access                          = access;
    entry->granularity                     = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    entry->base_high                       = (base >> 24) & 0xFF;
}

void set_tss_entry(int index, uint64_t base, uint32_t limit)
{
    auto entry         = reinterpret_cast<gdt::TaskStateSegmentDescriptor *>(&gdt_entries[index]);
    entry->limit_low   = limit & 0xFFFF;
    entry->base_low    = base & 0xFFFF;
    entry->base_middle = (base >> 16) & 0xFF;
    entry->access      = 0x89; // Present, DPL=0, Type=9 (Available 32-bit TSS)
    entry->granularity = ((limit >> 16) & 0x0F);
    entry->base_high   = (base >> 24) & 0xFF;
    entry->base_upper  = (base >> 32) & 0xFFFFFFFF;
    entry->reserved    = 0;
}
} // namespace

namespace gdt
{


void initialize_gdt()
{
    set_entry(0, 0x00, 0x00); // Null descriptor
    set_entry(1, 0x9A, 0x20); // Kernel code segment P,S,E,RW+L=1
    set_entry(2, 0x92, 0x00); // Kernel data segment P,S,E,RW+L=0
    // セレクタ番号と一致させる: 0x1B(index3)=User Data, 0x23(index4)=User Code
    set_entry(3, 0xF2, 0x00); // User data segment P,S,E,RW+L=0
    set_entry(4, 0xFA, 0x20); // User code segment P,S,E,RW+L=1


    memset(&tss, 0, sizeof(TaskStateSegment));

    tss.rsp[0]              = 0;
    tss.io_map_base_address = sizeof(TaskStateSegment);

    set_tss_entry(5, reinterpret_cast<uint64_t>(&tss), sizeof(TaskStateSegment) - 1);
    gdt_ptr.limit = sizeof(GlobalDescriptorTableEntry) * gdt_entries.size() - 1;
    gdt_ptr.base  = reinterpret_cast<uint64_t>(&gdt_entries[0]);

    asm volatile("lgdt %0" : : "m"(gdt_ptr));


    // セグメントレジスタを再ロード
    asm volatile("mov $0x10, %%ax\n"
                 "mov %%ax, %%ds\n"
                 "mov %%ax, %%es\n"
                 "mov %%ax, %%ss\n"
                 "mov %%ax, %%fs\n"
                 "mov %%ax, %%gs\n"
                 :
                 :
                 : "ax");

    // CS は far return で再ロード
    asm volatile("pushq $0x08\n"
                 "leaq 1f(%%rip), %%rax\n"
                 "pushq %%rax\n"
                 "lretq\n"
                 "1:\n"
                 :
                 :
                 : "rax", "memory");


    // TSS を Task Register にロード
    LoadTR(gdt::SegmentSelector::kTSS);

    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[GDT]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->puts("rebuilt with user segments + TSS\n");
}

void set_kernel_stack(uint64_t rsp0)
{
    tss.rsp[0] = rsp0;
}
} // namespace gdt
