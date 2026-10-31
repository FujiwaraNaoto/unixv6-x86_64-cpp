#include "file_system.hpp"
#include "file_system_internal.hpp"
#include <cstdint>
#include <cstring>

namespace
{
using FileSystem::internal::bitmap_block;
using FileSystem::internal::mark_block_used;
using FileSystem::internal::write_inode;
using FileSystem::internal::zero_block;


bool add_root_entry(DiskInode &root, InodeNumber inode_number, const char *name)
{
    BlockNumber root_address = root.addrs[0];
    if (root_address.value == 0)
    {
        auto block_number = FileSystem::allocate_block();
        if (!block_number)
        {
            return false;
        }
        root_address  = block_number.value();
        root.addrs[0] = root_address;
    }

    auto block = FileSystem::internal::fs.block_store->acquire(root_address);
    if (!block)
        return false;

    auto *entries = reinterpret_cast<DirectoryEntry *>(block.data());
    int capacity  = FSBLOCK_SIZE / sizeof(DirectoryEntry);

    for (int i = 0; i < capacity; i++)
    {
        if (entries[i].inode_number != 0)
            continue;

        entries[i].inode_number = static_cast<uint16_t>(inode_number.value);
        std::strncpy(entries[i].name, name, DIRECTORY_NAME_SIZE);
        if (!block.write_back())
        {
            return false;
        }
        root.size += sizeof(DirectoryEntry);
        return true;
    }

    return false;
}


bool format(uint32_t total_blocks)
{
    SuperBlock &superblock_state  = FileSystem::internal::mutable_superblock();
    constexpr uint32_t NUM_INODES = 200; // 適当な値。xv6 は 200 で固定している。
    uint32_t inode_blocks         = (NUM_INODES + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
    uint32_t bitmap_blocks        = (total_blocks + BLOCKS_PER_BITMAP_BLOCK - 1) / BLOCKS_PER_BITMAP_BLOCK;

    superblock_state.magic        = FS_MAGIC;
    superblock_state.size         = total_blocks;
    superblock_state.ninodes      = NUM_INODES;
    superblock_state.inode_start  = BlockNumber{2}; // 0=boot, 1=super
    superblock_state.bitmap_start = BlockNumber{superblock_state.inode_start.value + inode_blocks};


    uint32_t data_start      = superblock_state.bitmap_start.value + bitmap_blocks;
    superblock_state.nblocks = total_blocks - data_start;

    for (BlockNumber b = superblock_state.inode_start; b.value < data_start; b.value++)
    {
        if (!zero_block(b))
        {
            return false;
        }
    }

    // make meta blocks used (0〜data_start-1)
    for (BlockNumber b{0}; b.value < data_start; b.value++)
    {
        if (!mark_block_used(b))
        {
            return false;
        }
    }

    // write superblock to block 1. 0 is boot block, so skip it.
    {
        auto block = FileSystem::internal::fs.block_store->acquire(BlockNumber{1}); // superblock
        if (!block)
            return false;
        std::memset(block.data(), 0, FSBLOCK_SIZE);

        *reinterpret_cast<SuperBlock *>(block.data()) = superblock_state;
        if (!block.write_back())
            return false;
    }

    DiskInode root_inode{
        .type  = InodeType::kDirectory,
        .major = 0,
        .minor = 0,
        .nlink = 1,
        .size  = 0,
        .addrs = {},
    };

    if (!add_root_entry(root_inode, ROOT_INODE, "."))
    {
        return false;
    }
    if (!add_root_entry(root_inode, ROOT_INODE, ".."))
    {
        return false;
    }
    if (!write_inode(ROOT_INODE, root_inode))
    {
        return false;
    }


    return true;
}


} // namespace

namespace FileSystem
{

bool initialize(uint32_t total_blocks, IBlockStore *block_store, IConsole *console)
{
    FileSystem::internal::fs.block_store = block_store ? block_store : null_block_store;
    FileSystem::internal::fs.console     = console ? console : null_console;

    if (internal::load_superblock())
    {
        return true;
    }

    // if the magic number is not matched, the disk is not formatted yet. format it.
    FileSystem::internal::fs.console->puts("[FS]   not formatted, creating filesystem...\n");

    if (!format(total_blocks))
    {
        return false;
    }
    return internal::load_superblock();
}

std::optional<BlockNumber> allocate_block()
{
    const SuperBlock &superblock = FileSystem::superblock();

    for (BlockNumber base{0}; base.value < superblock.size; base.value += BLOCKS_PER_BITMAP_BLOCK)
    {
        auto block = FileSystem::internal::fs.block_store->acquire(bitmap_block(base));
        if (!block)
            return std::nullopt;
        auto *bitmap = block.data();
        for (uint32_t offset = 0; offset < BLOCKS_PER_BITMAP_BLOCK && base.value + offset < superblock.size; offset++)
        {
            uint32_t byte_index = offset / BITS_PER_BYTE;
            uint8_t bit_mask    = 1 << (offset % BITS_PER_BYTE);
            if ((bitmap[byte_index] & bit_mask) == 0)
            {
                // mark this block as used
                bitmap[byte_index] |= bit_mask;
                if (!block.write_back())
                {
                    return std::nullopt;
                }
                block.reset();
                BlockNumber block_number{base.value + offset};
                if (!zero_block(block_number))
                {
                    return std::nullopt;
                }
                return block_number;
            }
        }
    }
    return std::nullopt;
}

void free_block(BlockNumber block_number)
{
    auto block = FileSystem::internal::fs.block_store->acquire(bitmap_block(block_number));
    if (!block)
    {
        return;
    }
    uint32_t bit_index = block_number.value % BLOCKS_PER_BITMAP_BLOCK;
    block.data()[bit_index / BITS_PER_BYTE] &= static_cast<uint8_t>(~(1u << (bit_index % BITS_PER_BYTE)));
    block.write_back();
}

} // namespace FileSystem
