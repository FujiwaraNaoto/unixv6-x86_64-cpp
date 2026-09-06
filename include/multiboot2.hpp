#pragma once
#include <cstdint>

// see 4.4.1 multiboot2.h
constexpr uint32_t MULTIBOOT2_TAG_END             = 0;
constexpr uint32_t MULTIBOOT2_TAG_TYPE_MEMORY_MAP = 6; // 3.6.8 Memory Map
constexpr uint32_t MULTIBOOT2_MEMORY_AVAILABLE    = 1;

// https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
// chapter3.1.3 General tag structure
struct [[gnu::packed]] Multiboot2Tag
{
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

// chapter3.6.8 Memory Map
struct [[gnu::packed]] Multiboot2MemoryMapEntry
{
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

// chapter3.6.8 Memory Map
struct [[gnu::packed]] Multiboot2MemoryMapTag
{
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    Multiboot2MemoryMapEntry entries[];
};

// chapter3.1 Boot information format
// 情報構造体の先頭 8 バイト。この total_size にタグ全体のバイト数が入っている。
struct [[gnu::packed]] Multiboot2InfoHeader
{
    uint32_t total_size;
    uint32_t reserved;
};

Multiboot2Tag *find_next_tag(Multiboot2Tag *current_tag);

Multiboot2MemoryMapTag *find_mmap(uint32_t multiboot_address);

// GRUB が置いた情報構造体全体のバイト数を返す。
// PMM に予約させて、後から物理ページとして配られるのを防ぐために使う。
uint32_t multiboot_info_size(uint32_t multiboot_address);
