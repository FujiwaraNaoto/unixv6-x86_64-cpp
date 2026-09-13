#ifndef FILE_SYSTEM_INTERNAL_HPP
#define FILE_SYSTEM_INTERNAL_HPP

#include "file_system.hpp"

// file_system.cpp / fs_mount.cpp / inode.cpp の間でのみ共有する。
// 外部からは file_system.hpp の const アクセサだけを見せる。
namespace FileSystem::internal
{
SuperBlock &mutable_superblock();
bool load_superblock();
bool zero_block(uint32_t blockno);
bool mark_block_used(uint32_t blockno);
bool write_inode(uint32_t inum, const DiskInode &inode);
DiskInode* read_inode(uint32_t inum);
} // namespace FileSystem::internal


#endif // FILE_SYSTEM_INTERNAL_HPP
