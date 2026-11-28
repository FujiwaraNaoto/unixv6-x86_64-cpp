#ifndef MM_MEMORY_REGION_HPP
#define MM_MEMORY_REGION_HPP
#include <array>
#include <cstddef>
#include <cstdint>

// PhysicalAddress [start, end)
struct MemoryRegion
{
    uint64_t start;
    uint64_t end;
};

// Physical memory information collected by the arch code at boot and passed to the PMM.
// usable: physical memory that may be used. reserved: ranges inside usable that must not be handed out.
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
