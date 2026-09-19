#include <cstdint>
#include "tests.hpp"
#include "pmm.hpp"

namespace tests
{

void pmm_alloc_free(IConsole *console)
{
    auto *pmm = pmm::pmm_ptr;

    const auto a1 = pmm->allocate();
    const auto a2 = pmm->allocate();
    const auto a3 = pmm->allocate();
    if (!a1 || !a2 || !a3)
    {
        console->puts("[PMM]  alloc test: out of memory\n");
        return;
    }
    const uint64_t p1 = *a1.address;
    const uint64_t p2 = *a2.address;
    const uint64_t p3 = *a3.address;
    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[PMM]  ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("alloc test: 0x%x  0x%x  0x%x\n",
                    static_cast<unsigned>(p1),
                    static_cast<unsigned>(p2),
                    static_cast<unsigned>(p3));

    // 解放したページが次の allocate で再利用されるか
    pmm->free(a2);
    const auto a4 = pmm->allocate();
    if (!a4)
    {
        console->puts("[PMM]  free+realloc: out of memory\n");
        return;
    }
    const uint64_t p4 = *a4.address;
    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[PMM]  ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("free+realloc: freed=0x%x  got=0x%x  %s\n",
                    static_cast<unsigned>(p2),
                    static_cast<unsigned>(p4),
                    p4 == p2 ? "OK" : "MISMATCH");
}

} // namespace tests
