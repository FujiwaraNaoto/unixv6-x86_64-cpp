
#include "idt.hpp"


namespace
{
// attribute は IST バイト(下位8bit) + type/DPL/P バイト(上位8bit) の 16bit。
// 旧 type_attribute (0x8E/0x8F) は上位バイトに置く必要があるため << 8 する。
// union の bits メンバを designated initializer で初期化する (C++20)。
// designator は宣言順・1段のみ。匿名ビットフィールドや reserved0 は 0 で省略。
constexpr idt::InterruptDescriptorAttribute IDT_INTERRUPT_GATE = {.bits = {
                                                                      .interrupt_stack_table = 0,
                                                                      .type = idt::DescriptorType::kInterruptGate,
                                                                      .descriptor_privilege_level = 0,
                                                                      .present                    = 1,
                                                                  }};
constexpr idt::InterruptDescriptorAttribute IDT_TRAP_GATE      = {.bits = {
                                                                      .interrupt_stack_table = 0,
                                                                      .type = idt::DescriptorType::kTrapGate,
                                                                      .descriptor_privilege_level = 0,
                                                                      .present                    = 1,
                                                             }};

extern "C" uint64_t isr_stubs[]; // isr.asm で定義される ISR スタブ関数のテーブル
extern "C" uint64_t irq_stubs[]; // isr.asm で定義される IRQ スタブ関数のテーブル
} // namespace

namespace idt
{
InterruptDescriptorTable::InterruptDescriptorTable()
{
    for (uint8_t i = 0; i < 32; i++)
    {
        set_idt(i, IDT_INTERRUPT_GATE, isr_stubs[i]);
    }

    for (uint8_t i = 0; i < 16; i++)
    {
        set_idt(0x20 + i, IDT_INTERRUPT_GATE, irq_stubs[i]);
    }

    // IDTのベースアドレスとサイズを設定
    idt_ptr.limit = sizeof(InterruptDescriptorTableEntry) * entries.size() - 1;
    idt_ptr.base  = reinterpret_cast<uint64_t>(entries.data());
    asm volatile("lidt %0" : : "m"(idt_ptr));
    asm volatile("sti"); // 割り込みを有効化
}


void InterruptDescriptorTable::set_idt(uint8_t idx, InterruptDescriptorAttribute attribute, uint64_t handler)
{
    entries[idx].offset_low       = handler & 0xFFFF;
    entries[idx].segment_selector = 0x08; // コードセグメントのセレクタ

    entries[idx].attribute = attribute;

    entries[idx].offset_middle = (handler >> 16) & 0xFFFF;
    entries[idx].offset_high   = (handler >> 32) & 0xFFFFFFFF;
    entries[idx].reserved      = 0;
}
} // namespace idt
