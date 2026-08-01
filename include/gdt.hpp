#pragma once
#include <cstdint>

namespace gdt
{
namespace SegmentSelector
{
constexpr uint16_t kKernelCode = 1<<3; // 0x08
constexpr uint16_t kKernelData = 2<<3; // 0x10
constexpr uint16_t kUserData   = 3<<3 | 0x03; // 0x1B(RPL=3)
constexpr uint16_t kUserCode   = 4<<3 | 0x03; // 0x23(RPL=3)
constexpr uint16_t kTSS        = 5<<3; // 0x28(RPL=0)

// セレクタから GDT のエントリ番号を取り出す (下位3bitは RPL と TI なので落とす)。
// これを使って GDT を組めば、セレクタ定数とエントリ位置が食い違うことがなくなる。
constexpr int index_of(uint16_t selector)
{
    return selector >> 3;
}
} // namespace SegmentSelector

struct [[gnu::packed]] GlobalDescriptorTableEntry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

struct [[gnu::packed]] TaskStateSegmentDescriptor
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper; // bit 63:32
    uint32_t reserved;
};

struct [[gnu::packed]] GlobalDescriptorTablePointer
{
    uint16_t limit;
    uint64_t base;
};

struct [[gnu::packed]] TaskStateSegment
{
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base_address;
};

void initialize_gdt();
/**
 * @brief Update TSS's RSP0 field to the given value, which is used when switching from user mode to kernel mode.
 */
void set_kernel_stack(uint64_t rsp0);
} // namespace gdt
