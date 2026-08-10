#include "multiboot2.hpp"


Multiboot2Tag *find_next_tag(Multiboot2Tag *current_tag)
{
    return reinterpret_cast<Multiboot2Tag *>(reinterpret_cast<uint8_t *>(current_tag) + ((current_tag->size + 7) & ~7));
}

Multiboot2MemoryMapTag *find_mmap(uint32_t multiboot_address)
{
    constexpr uint64_t DIRECT_MAP_BASE = 0xFFFF800000000000ULL;
    // multiboot_address は multiboot2 の情報構造体の物理アドレス
    uint64_t virt = static_cast<uint64_t>(multiboot_address) + DIRECT_MAP_BASE;
    auto *tag     = reinterpret_cast<Multiboot2Tag *>(virt + 8); //

    while (tag->type != MULTIBOOT2_TAG_END)
    {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MEMORY_MAP)
        {
            return reinterpret_cast<Multiboot2MemoryMapTag *>(tag);
        }
        tag = find_next_tag(tag);
    }
    return nullptr; // メモリマップタグが見つからなかった場合
}
