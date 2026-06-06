
#pragma once
#include "types.hpp"



namespace pic
{

    void InitializePIC(uint8_t offset1, uint8_t offset2);
    void mask_irq(uint8_t irq);
    void unmask_irq(uint8_t irq);
    void send_eoi(uint8_t irq);

}
