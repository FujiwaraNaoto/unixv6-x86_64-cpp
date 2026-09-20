#ifndef FILE_SYSTEM_INTERNAL_HPP
#define FILE_SYSTEM_INTERNAL_HPP

#include "file_system.hpp"
#include <optional>

// file_system.cpp / fs_mount.cpp / inode.cpp の間でのみ共有する。
// 外部からは file_system.hpp の const アクセサだけを見せる。
namespace FileSystem::internal
{
SuperBlock &mutable_superblock();
bool load_superblock();
// ビットマップのうち、blockno のビットが入っているブロック番号
uint32_t bitmap_block(uint32_t blockno);
bool zero_block(uint32_t blockno);
bool mark_block_used(uint32_t blockno);
bool write_disk_inode(uint32_t inum, const DiskInode &inode);
std::optional<DiskInode> read_disk_inode(uint32_t inum);
// inum が入っている inode ブロックの番号
uint32_t inode_block(uint32_t inum);
} // namespace FileSystem::internal


#endif // FILE_SYSTEM_INTERNAL_HPP
