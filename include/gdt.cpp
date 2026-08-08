#include "gdt.hpp"
#include <cstring>
#include <array>
#include "vga.hpp"

namespace
{
extern "C" void LoadTR(uint16_t sel); // Load Task Register with the given selector
// GDTR を差し替え、ds/es/ss/fs/gs と cs を新しい GDT の内容で再ロードする (gdt_helper.asm)
extern "C" void LoadGDT(const gdt::GlobalDescriptorTablePointer *ptr);
} // namespace

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
    using namespace SegmentSelector;

    // エントリ位置はセレクタ定数から導く。こうしておけば gdt.hpp のセレクタを変えたとき
    // 「定数は変えたが GDT の並びは古いまま」というズレが起きない。
    // User Data(index3) が User Code(index4) より先に来ているのは sysret が要求する順序。
    set_entry(0, 0x00, 0x00);                     // Null descriptor
    set_entry(index_of(kKernelCode), 0x9A, 0x20); // Kernel code segment P,S,E,RW+L=1
    set_entry(index_of(kKernelData), 0x92, 0x00); // Kernel data segment P,S,E,RW+L=0
    set_entry(index_of(kUserData), 0xF2, 0x00);   // User data segment P,S,E,RW+L=0
    set_entry(index_of(kUserCode), 0xFA, 0x20);   // User code segment P,S,E,RW+L=1


    memset(&tss, 0, sizeof(TaskStateSegment));

    tss.rsp[0]              = 0;
    tss.io_map_base_address = sizeof(TaskStateSegment);

    // TSS ディスクリプタは 16 バイトなので、index_of(kTSS)=5 と 6 の 2 エントリ分を占める
    set_tss_entry(index_of(kTSS), reinterpret_cast<uint64_t>(&tss), sizeof(TaskStateSegment) - 1);
    gdt_ptr.limit = sizeof(GlobalDescriptorTableEntry) * gdt_entries.size() - 1;
    gdt_ptr.base  = reinterpret_cast<uint64_t>(&gdt_entries[0]);

    // GDTR の差し替えと、ds/es/ss/fs/gs・cs の再ロード
    LoadGDT(&gdt_ptr);


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
