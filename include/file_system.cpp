#include "file_system.hpp"
#include <cstdint>
#include <cstring>

namespace
{

    SuperBlock superblock_state;

    bool zero_block(uint32_t blockno)
    {
        auto block = FileSystem::block_store->acquire(blockno);
        if (!block) return false;
        std::memset(block.data(), 0, FSBLOCK_SIZE);
        return block.write_back();
    }

    bool mark_block_used(uint32_t blockno)
    {
        auto block = FileSystem::block_store.acquire(superblock_state.bmapstart + blockno / BPB);
        if (!block) return false;
        uint32_t bit_index = blockno % BPB;
        uint32_t byte_index = bit_index / 8;
        uint8_t bit_mask = 1 << (bit_index % 8);
        block.data()[byte_index] |= bit_mask;
        return block.write_back();
    }

    bool add_root_entry(DiskInode &root, uint32_t inum, const char* name){
        return true;
    }





bool format(uint32_t total_blocks, IConsole *console)
{
    uint32_t inode_blocks  = (NINODES + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
    uint32_t bitmap_blocks = (total_blocks + BLOCKS_PER_BITMAP_BLOCK - 1) / BLOCKS_PER_BITMAP_BLOCK;

    superblock_state.magic      = FS_MAGIC;
    superblock_state.size       = total_blocks;
    superblock_state.ninodes    = NINODES;
    superblock_state.inodestart = 2; // 0=boot, 1=super
    superblock_state.bmapstart  = superblock_state.inodestart + inode_blocks;



    uint32_t data_start      = superblock_state.bmapstart + bitmap_blocks;
    superblock_state.nblocks = total_blocks - data_start;

    for(uint32_t b=superblock_state.inodestart; b<data_start; ++b)
    {
        if(!zero_block(b))
        {
            return false;
        }
    }

    // make meta blocks used (0〜data_start-1)
    for (uint32_t b = 0; b < data_start; b++)
    {
        if (!mark_block_used(b))
        {
            return false;
        }
    }

    // write superblock to block 1. 0 is boot block, so skip it.
    {
        auto block = FileSystem::block_store->acquire(1); // superblock
        if (!block) return false;
        std::memset(block.data(), 0, FSBLOCK_SIZE);

        *reinterpret_cast<SuperBlock *>(block.data()) = superblock_state;
        if (!block.write_back()) return false;

    }

    DiskInode root_inode{
        .type  = InodeType::kDirectory,
        .nlink = 1,
        .size  = 0,
    };

    if (!add_root_entry(root_inode, ROOTINO, "."))
    {
        return false;
    }
    if (!add_root_entry(root_inode, ROOTINO, ".."))
    {
        return false;
    }
    if (!write_inode(ROOTINO, root_inode))
    {
        return false;
    }


    return true;
}


}// namespace
