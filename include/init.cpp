
#include "init.hpp"
#include "process.hpp"
#include "file.hpp"
#include "console.hpp"
#include "usermode.hpp"
#include "user_init.hpp"

constexpr int BUF_SIZE = 1 << 10;
namespace InitProcess
{
void init_func(void *arg)
{
    Process *self = process::current_process();
    if (!usermode::map_user_space(self->pml4))
        return; // 失敗したら trampoline 経由で終わる
    usermode::enter(reinterpret_cast<uint64_t>(&init_main), usermode::kUserStackTop);
}

} // namespace InitProcess
