#include "scheduler.hpp"
#include "process.hpp"

namespace scheduler
{
void tick()
{
    process::yield();
}
} // namespace scheduler
