#pragma once
#include <cstdint>

namespace keyboard {
    void initialize();

    // get one character from keyboard buffer. If no character is available, return 0.
    char getchar();

    // check if there is input in the buffer
    bool has_input();

    // IRQ1 handler
    void handle_irq();
}
