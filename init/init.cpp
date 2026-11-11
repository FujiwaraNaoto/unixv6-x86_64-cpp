#include <cstdint>
#include "init.hpp"
#include "process.hpp"
#include "user_init.hpp"
#include "asm/usermode.hpp"
namespace InitProcess
{
void init_func(void * /*arg*/)
{
    Process *self = process::current_process();
    usermode::arch_enter_user(self,init_main);
}

} // namespace InitProcess
