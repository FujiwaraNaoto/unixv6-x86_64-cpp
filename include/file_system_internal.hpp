#ifndef FILE_SYSTEM_INTERNAL_HPP
#define FILE_SYSTEM_INTERNAL_HPP

#include "file_system.hpp"
#include <optional>

// file_system.cpp / fs_mount.cpp / inode.cpp の間でのみ共有する。
// 外部からは file_system.hpp の const アクセサだけを見せる。
namespace FileSystem::internal
{

struct State
{
    // 下のブロック記憶域。initialize() が設定する差し替えの境目。
    // nullptr のまま使われないよう、既定は何もしない null_block_store。
    IBlockStore *block_store = nullptr;
    IConsole *console        = nullptr; // ログ出力先
    SuperBlock superblock{};            // メモリ上のスーパーブロック
};

// 実体は file_system_internal.cpp。すべてゼロなので .bss に置かれる。
extern State fs;

SuperBlock &mutable_superblock();
bool load_superblock();
// ビットマップのうち、blockno のビットが入っているブロック番号
uint32_t bitmap_block(uint32_t blockno);
bool zero_block(uint32_t blockno);
bool mark_block_used(uint32_t blockno);
bool write_inode(InodeNumber inode_number, const DiskInode &inode);
std::optional<DiskInode> read_inode(InodeNumber inode_number);
} // namespace FileSystem::internal


#endif // FILE_SYSTEM_INTERNAL_HPP
