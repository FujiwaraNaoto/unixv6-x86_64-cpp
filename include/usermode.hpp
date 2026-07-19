#pragma once
#include <cstdint>

namespace usermode
{
[[noreturn]] void enter(uint64_t entry_point, uint64_t stack_pointer);

}
