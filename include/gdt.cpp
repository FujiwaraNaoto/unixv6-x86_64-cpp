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
struct Access
{
    uint8_t present : 1                     = 1; // P: Present (1=有効, 0=無効)
    uint8_t descriptor_privilege_level : 2  = 0; // DPL: Descriptor Privilege Level (0=Ring0, 3=Ring3)
    uint8_t descriptor_type : 1             = 1; // S: Descriptor Type (1=Code/Data, 0=System (TSS, LDT, ゲートなど))
    uint8_t executable : 1                  = 0; // E: Executable (1=Code, 0=Data)
    uint8_t direction_conforming : 1        = 0; // DC: Direction/Conforming (Code セグメントの場合: 1=Conforming, Data セグメントの場合: 1=Down)
    uint8_t readable_writable : 1           = 1; // RW: Readable/Writable (Code セグメントの場合: 1=Readable, Data セグメントの場合: 1=Writable)
    uint8_t accessed : 1                    = 0; // A: Accessed (CPU がアクセスしたときに自動で 1 になる)

    // access フィールドのビット構成
    // bit:  7    6-5    4    3    2     1    0
    //       P    DPL    S    E    DC    RW   A
    operator uint8_t() const
    {
    // ユーザとカーネルの場合は異なるのがDPLがuser(3)かkernel(0)かの違いだけで、他のビットは同じ。
    // データかコードセグメントかの違いは E ビットだけ(コード: E=1, データ: E=0)
    // DC と RW は E の値によって意味が変わる
    //        P DPL S E DC RW A
    // 0x9A = 1 00  1 1 0  1  0  → P=1, DPL=0, S=1, E=1, RW=1 : カーネル コード
    // 0x92 = 1 00  1 0 0  1  0  → P=1, DPL=0, S=1, E=0, RW=1 : カーネル データ
    // 0xF2 = 1 11  1 0 0  1  0  → P=1, DPL=3, S=1, E=0, RW=1 : ユーザー データ
    // 0xFA = 1 11  1 1 0  1  0  → P=1, DPL=3, S=1, E=1, RW=1 : ユーザー コード

    // NOTE: デフォルト値が P=1, DPL=0, S=1, E=0, RW=1 なので、カーネル データ セグメントの値と同じ。
        return (present << 7) | (descriptor_privilege_level << 5) | (descriptor_type << 4) |
               (executable << 3) | (direction_conforming << 2) | (readable_writable << 1) | accessed;
    }
};

// 下位 4bit の limit[19:16] は set_entry で limit から埋めるので、ここでは上位 4bit だけを持つ
struct Granularity
{
    // granularity フィールドのビット構成
    // bit:  7    6     5    4     3-0
    //       G    D/B   L    AVL   limit[19:16]
    // 0x20(=0b0010 0000)はbit5でLbit(long mode)=1。このコードセグメントは64bitモードで実行されることを示す。
    // L=1の時D/Bビットは必ず0にすること(L=1 & D=1は予約済みの組み合わせ)

    uint8_t granularity : 1          = 0; // G : 1=4KiB単位, 0=バイト単位
    uint8_t default_operand_size : 1 = 0; // D/B : 1=32bit, 0=16bit
    uint8_t long_mode : 1            = 0; // L : 1=64bitコード, 0=互換モード(D/Bで16/32bitが決まる)。コードセグメントのみ有効
    uint8_t available : 1            = 0; // AVL : Available for system software use

    operator uint8_t() const
    {
        return (granularity << 7) | (default_operand_size << 6) | (long_mode << 5) | (available << 4);
    }
};


// システムディスクリプタ (TSS など) の種類。S=0 のとき access の下位 4bit がこの値になる。
// (Intel SDM Vol.3A Table 3-2)
// 64bit モードで TSS を表すのは 9 と 11 だけ。32bit モードで 32-bit TSS だった値が、
// そのまま 64-bit TSS になっている。16-bit TSS を表していた 1 と 3 は予約済みになり、
// 0 は「16 バイトディスクリプタの上位 8 バイト」を表す値に変わった。
enum class SystemDescriptorType : uint8_t
{
    AvailableTss = 0x9, // まだ使われていない TSS (access は 0x89)
    BusyTss      = 0xB, // ltr でロードされると CPU が自動でこちらに書き換える (access は 0x8B)。
                        // 既に Busy の TSS を ltr すると #GP になるので、初期値は Available にする
};

// システムディスクリプタ用の access バイト。
// Code/Data 用の Access とはビットの意味が違い、S=0 (システム) のとき
// 下位 4bit は E/DC/RW/A ではなく Type になる。
//
// NOTE: 64bit モードのシステムディスクリプタは 16 バイトあり、GDT の 2 エントリ分を占める。
//       2 エントリ目にベースアドレスの上位 32bit が入る (TaskStateSegmentDescriptor::base_upper)。
//       Code/Data 用の set_entry() で書くと上位 8 バイトを書き忘れるので、set_tss_entry() を使う。
struct SystemAccess
{
    // access フィールドのビット構成
    // bit:  7    6-5    4    3-0
    //       P    DPL    S    Type
    // 0x89 = 1 00  0 1001 → P=1, DPL=0, S=0, Type=9 : 使用前の TSS
    uint8_t present : 1                    = 1; // P: Present
    uint8_t descriptor_privilege_level : 2 = 0; // DPL: 慣例的に 0。本来はハードウェアタスクスイッチを
                                                //      低い特権レベルから使わせないためのものだが、64bit モードには
                                                //      その仕組みが無く、ltr も CPL=0 でしか実行できないので実質効かない
    uint8_t descriptor_type : 1            = 0; // S: システムディスクリプタは 0 固定
    SystemDescriptorType type              = SystemDescriptorType::AvailableTss;

    operator uint8_t() const
    {
        return (present << 7) | (descriptor_privilege_level << 5) | (descriptor_type << 4) |
               (static_cast<uint8_t>(type) & 0x0F);
    }
};


void set_entry(int index, Access access, Granularity granularity, uint32_t base = 0, uint32_t limit = 0)
{
    gdt::GlobalDescriptorTableEntry *entry = &gdt_entries[index];
    entry->limit_low                       = limit & 0xFFFF;
    entry->base_low                        = base & 0xFFFF;
    entry->base_middle                     = (base >> 16) & 0xFF;
    entry->access                          = static_cast<uint8_t>(access);
    entry->granularity                     = ((limit >> 16) & 0x0F) | static_cast<uint8_t>(granularity);
    entry->base_high                       = (base >> 24) & 0xFF;
}

void set_tss_entry(int index, uint64_t base, uint32_t limit)
{
    auto entry         = reinterpret_cast<gdt::TaskStateSegmentDescriptor *>(&gdt_entries[index]);
    entry->limit_low   = limit & 0xFFFF;
    entry->base_low    = base & 0xFFFF;
    entry->base_middle = (base >> 16) & 0xFF;
    entry->access      = static_cast<uint8_t>(SystemAccess{.type = SystemDescriptorType::AvailableTss});
    // TSS では G / D/B / L / AVL はどれも 0 にする (L=1 と D/B=1 はコードセグメント用で、
    // ここでは予約扱い)。Granularity の既定値がすべて 0 なので、そのまま使う。
    entry->granularity = ((limit >> 16) & 0x0F) | static_cast<uint8_t>(Granularity{});
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
    set_entry(0, Access{0,0,0,0,0,0,0}, Granularity{});                     // Null descriptor
    set_entry(index_of(kKernelCode), Access{.descriptor_privilege_level = 0b00, .executable = 1}, Granularity{.long_mode = 1}); // Kernel code segment P,S,E,RW+L=1
    set_entry(index_of(kKernelData), Access{.descriptor_privilege_level = 0b00, .executable = 0}, Granularity{.long_mode = 0}); // Kernel data segment P,S,E,RW+L=0
    set_entry(index_of(kUserData), Access{.descriptor_privilege_level = 0b11, .executable = 0}, Granularity{.long_mode = 0});   // User data segment P,S,E,RW+L=0
    set_entry(index_of(kUserCode), Access{.descriptor_privilege_level = 0b11, .executable = 1}, Granularity{.long_mode = 1});   // User code segment P,S,E,RW+L=1


    std::memset(&tss, 0, sizeof(TaskStateSegment));

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
