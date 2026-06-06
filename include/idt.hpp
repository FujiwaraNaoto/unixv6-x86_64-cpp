#pragma once
#include "types.hpp"

#include <array>


namespace idt
{

typedef struct __attribute__((packed))
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attribute;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;


/* 割り込み時にスタックに積まれるフレーム */
typedef struct __attribute__((packed))
{
    uint64_t rip, cs, rflags, rsp, ss;
} int_frame_t;


class InterruptDescriptorTable
{
  public:
    InterruptDescriptorTable();

  private:
    void set_idt(uint8_t idx, uint8_t type_attr, uint64_t handler);
    std::array<idt_entry_t, 256> entries;
    idt_ptr_t idt_ptr;
};

} // namespace idt
