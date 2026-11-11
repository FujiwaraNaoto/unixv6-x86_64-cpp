#ifndef MM_MEMORY_REGION_HPP
#define MM_MEMORY_REGION_HPP
#include <array>
#include <cstddef>
#include <cstdint>

// 物理アドレス [start, end)
struct MemoryRegion
{
    uint64_t start;
    uint64_t end;
};

// ブート時に arch 側が集めて PMM に渡す、物理メモリの情報。
// usable: 使ってよい物理メモリ。reserved: usable の中にあるが配ってはいけない範囲。
struct BootMemoryMap
{
    static constexpr size_t MAX_USABLE   = 32;
    static constexpr size_t MAX_RESERVED = 4;

    std::array<MemoryRegion, MAX_USABLE> usable{};
    size_t usable_count = 0;
    std::array<MemoryRegion, MAX_RESERVED> reserved{};
    size_t reserved_count = 0;
};

#endif // MM_MEMORY_REGION_HPP
