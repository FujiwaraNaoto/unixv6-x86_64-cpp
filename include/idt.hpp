#pragma once
#include "types.hpp"

#include <array>


namespace idt
{

enum class DescriptorType : uint16_t
{
    // system segment & gate descriptor types
    kUpper8Bytes   = 0,
    kLDT           = 2,
    kTSSAvailable  = 9,
    kTSSBusy       = 11,
    kCallGate      = 12,
    kInterruptGate = 14,
    kTrapGate      = 15,

    // code & data segment types
    kReadWrite   = 2,
    kExecuteRead = 10,
};

union InterruptDescriptorAttribute
{
    uint16_t data;
    struct
    {
        uint16_t interrupt_stack_table : 3; // IST (Interrupt Stack Table) インデックス
        uint16_t reserved0 : 5;
        DescriptorType type : 4; // タイプ (例: 0
        uint16_t : 1;
        uint16_t descriptor_privilege_level : 2; // DPL (Descriptor Privilege Level)
        uint16_t present : 1;                    // P (Present) ビット
    } __attribute__((packed)) bits;
} __attribute__((packed));


struct InterruptDescriptorTableEntry
{
    uint16_t offset_low;
    uint16_t segment_selector;
    InterruptDescriptorAttribute attribute;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr_t
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));


/* 割り込み時にスタックに積まれるフレーム */
struct int_frame_t
{
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));


class InterruptDescriptorTable
{
  public:
    InterruptDescriptorTable();

  private:
    void set_idt(uint8_t idx, InterruptDescriptorAttribute attribute, uint64_t handler);
    std::array<InterruptDescriptorTableEntry, 256> entries;
    idt_ptr_t idt_ptr;
};

} // namespace idt
