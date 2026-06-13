#pragma once
#include <cstdint>


constexpr uint32_t MULTIBOOT2_TAG_END             = 0;
constexpr uint32_t MULTIBOOT2_TAG_TYPE_MEMORY_MAP = 6;
constexpr uint32_t MULTIBOOT2_MEMORY_AVAILABLE    = 1;


struct [[gnu::packed]] Multiboot2Tag
{
    uint32_t type;
    uint32_t size;
};

struct [[gnu::packed]] Multiboot2MemoryMapEntry
{
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct [[gnu::packed]] Multiboot2MemoryMapTag
{
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    Multiboot2MemoryMapEntry entries[];
};

Multiboot2Tag *find_next_tag(Multiboot2Tag *current_tag);

Multiboot2MemoryMapTag *find_mmap(uint32_t multiboot_address);
