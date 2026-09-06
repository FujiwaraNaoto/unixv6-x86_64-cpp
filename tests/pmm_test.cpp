#include <cstdint>
#include "tests.hpp"
#include "pmm.hpp"
#include "vga.hpp"

namespace tests
{

void pmm_alloc_free()
{
    auto *pmm = pmm::pmm_ptr;

    uint64_t p1 = pmm->allocate();
    uint64_t p2 = pmm->allocate();
    uint64_t p3 = pmm->allocate();
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[PMM]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("alloc test: 0x%x  0x%x  0x%x\n",
                     static_cast<unsigned>(p1),
                     static_cast<unsigned>(p2),
                     static_cast<unsigned>(p3));

    // 解放したページが次の allocate で再利用されるか
    pmm->free(p2);
    uint64_t p4 = pmm->allocate();
    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[PMM]  ");
    vga::vga->set_color(Color::LightGrey, Color::Black);
    vga::vga->printf("free+realloc: freed=0x%x  got=0x%x  %s\n",
                     static_cast<unsigned>(p2),
                     static_cast<unsigned>(p4),
                     p4 == p2 ? "OK" : "MISMATCH");
}

} // namespace tests
