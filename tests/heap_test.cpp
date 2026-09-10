#include <cstdint>
#include "tests.hpp"
#include "heap.hpp"
#include "vmm.hpp"

namespace tests
{

void heap_sbrk_alloc(IConsole *console)
{
    auto *heap = heap::heap_ptr;

    void *brk0 = heap->sbrk(0);         // 現在の brk
    void *brk1 = heap->sbrk(PAGE_SIZE); // 1ページ伸ばす
    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[SBRK] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("brk before=0x%016lx  returned=0x%016lx  now=0x%016lx\n",
                    reinterpret_cast<uintptr_t>(brk0),
                    reinterpret_cast<uintptr_t>(brk1),
                    reinterpret_cast<uintptr_t>(heap->sbrk(0)));

    // alloc/free テスト (morecore が自動で呼ばれる)
    void *p1 = heap->alloc(64);
    void *p2 = heap->alloc(128);
    void *p3 = heap->alloc(32);
    heap->free(p2);
    void *p4 = heap->alloc(64); // p2 の領域が再利用されるはず
    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[HEAP] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("p1=0x%016lx p2=0x%016lx p3=0x%016lx p4=0x%016lx reuse=%s\n",
                    reinterpret_cast<uintptr_t>(p1),
                    reinterpret_cast<uintptr_t>(p2),
                    reinterpret_cast<uintptr_t>(p3),
                    reinterpret_cast<uintptr_t>(p4),
                    p4 == p2 ? "OK" : "MISMATCH");
}

} // namespace tests
