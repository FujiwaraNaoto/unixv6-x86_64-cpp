#ifndef ASM_USER_ENTRY_HPP
#define ASM_USER_ENTRY_HPP
struct Process;

namespace usermode
{
bool arch_enter_user(Process *self, void (*entry)());
}

#endif // ASM_USER_ENTRY_HPP
