
#include "idt.hpp"


namespace
{
constexpr int IDT_INTERRUPT_GATE = 0x8E; // P=1, DPL=0, S=0, Type=14 (32-bit interrupt gate)
constexpr int IDT_TRAP_GATE      = 0x8F; // P=1, DPL=0, S=0, Type=15 (32-bit trap gate)   
}

namespace idt
{
    InterruptDescriptorTable::InterruptDescriptorTable()
    {
        for(uint8_t i=0;i<32;i++){
            set_idt(i, IDT_INTERRUPT_GATE, 0);
        }

        for(uint8_t i=0;i<16;i++){
            set_idt(0x20 + i, IDT_INTERRUPT_GATE, 0);
        }
        
        // IDTのベースアドレスとサイズを設定
        idt_ptr.limit = sizeof(idt_entry_t) * entries.size() - 1;
        idt_ptr.base  = reinterpret_cast<uint64_t>(entries.data());
    }

    
    void InterruptDescriptorTable::set_idt(uint8_t idx, uint8_t type_attr, uint64_t handler)
    {
        entries[idx].offset_low   = handler & 0xFFFF;
        entries[idx].selector    = 0x08; // コードセグメントのセレクタ
        entries[idx].ist         = 0;    // ISTは使用しない
        entries[idx].type_attribute   = type_attr;
        entries[idx].offset_middle  = (handler >> 16) & 0xFFFF;
        entries[idx].offset_high   = (handler >> 32) & 0xFFFFFFFF;
        entries[idx].zero        = 0;
    }
}
