#include "file_system_internal.hpp"
#include <cstring>

namespace FileSystem::internal
{
State fs;
} // namespace FileSystem::internal

namespace
{
uint32_t inode_block(uint32_t inum)
{
    return FileSystem::internal::fs.superblock.inode_start + inum / INODES_PER_BLOCK;
}


} // namespace

// file_system.cpp / fs_mount.cpp / inode.cpp の間でのみ共有する。
// 外部からは file_system.hpp の const アクセサだけを見せる。
namespace FileSystem
{
// 読み取り専用の公開アクセサ。書き換えは internal::mutable_superblock() 経由で行う。
const SuperBlock &superblock()
{
    return internal::mutable_superblock();
}
} // namespace FileSystem

namespace FileSystem::internal
{
uint32_t bitmap_block(uint32_t blockno)
{
    return fs.superblock.bitmap_start + blockno / BLOCKS_PER_BITMAP_BLOCK;
}

SuperBlock &mutable_superblock()
{
    return fs.superblock;
}
bool load_superblock()
{
    auto block = fs.block_store->acquire(1); // superblock
    if (!block)
        return false;
    fs.superblock = *reinterpret_cast<const SuperBlock *>(block.data());
    return fs.superblock.magic == FS_MAGIC;
}
bool zero_block(uint32_t blockno)
{
    auto block = fs.block_store->acquire(blockno);
    if (!block)
        return false;
    std::memset(block.data(), 0, FSBLOCK_SIZE);
    return block.write_back();
}
bool mark_block_used(uint32_t blockno)
{
    auto block = fs.block_store->acquire(bitmap_block(blockno));
    if (!block)
        return false;
    uint32_t bit_index  = blockno % BLOCKS_PER_BITMAP_BLOCK;
    uint32_t byte_index = bit_index / BITS_PER_BYTE;
    uint8_t bit_mask    = 1 << (bit_index % BITS_PER_BYTE);
    block.data()[byte_index] |= bit_mask;
    return block.write_back();
}
bool write_inode(uint32_t inum, const DiskInode &inode)
{
    auto block = fs.block_store->acquire(inode_block(inum));
    if (!block)
        return false;
    auto *entries                    = reinterpret_cast<DiskInode *>(block.data());
    entries[inum % INODES_PER_BLOCK] = inode;
    return block.write_back();
}

std::optional<DiskInode> read_inode(uint32_t inum)
{
    auto block = fs.block_store->acquire(inode_block(inum));
    if (!block)
        return std::nullopt;
    auto *entries = reinterpret_cast<DiskInode *>(block.data());
    return entries[inum % INODES_PER_BLOCK];
}


} // namespace FileSystem::internal
