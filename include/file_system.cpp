#include "file_system.hpp"
#include "file_system_internal.hpp"
#include "directory.hpp"
#include <cstdint>
#include <cstring>

// file_system.hpp で extern 宣言している変数の実体。
// Manager のコンストラクタが差し替え、各関数はここから読む。
namespace FileSystem
{
IBlockStore *block_store = nullptr;
IConsole *console        = nullptr;
} // namespace FileSystem

namespace
{
using FileSystem::internal::bitmap_block;
using FileSystem::internal::mark_block_used;
using FileSystem::internal::write_disk_inode;
using FileSystem::internal::zero_block;


bool format(uint32_t total_blocks, IConsole *console)
{
    SuperBlock &superblock_state = FileSystem::internal::mutable_superblock();
    constexpr uint32_t NUM_INODES = 200; // 適当な値。xv6 は 200 で固定している。
    uint32_t inode_blocks  = (NUM_INODES + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
    uint32_t bitmap_blocks = (total_blocks + BLOCKS_PER_BITMAP_BLOCK - 1) / BLOCKS_PER_BITMAP_BLOCK;

    superblock_state.magic      = FS_MAGIC;
    superblock_state.size       = total_blocks;
    superblock_state.ninodes    = NUM_INODES;
    superblock_state.inode_start = 2; // 0=boot, 1=super
    superblock_state.bitmap_start  = superblock_state.inode_start + inode_blocks;


    uint32_t data_start      = superblock_state.bitmap_start + bitmap_blocks;
    superblock_state.nblocks = total_blocks - data_start;

    for(uint32_t b=superblock_state.inode_start; b<data_start; b++)
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

    // ルートディレクトリ。まず空の inode を書き、"." と ".." は inode 層経由で追加する。
    // nlink は link_directory() が数えるので 0 から始める (最終的に "." と ".." の 2)。
    DiskInode root_inode{};
    root_inode.type = InodeType::kDirectory;
    if (!write_disk_inode(ROOT_INODE, root_inode))
    {
        return false;
    }

    FileSystem::Inode *root = FileSystem::get_inode(ROOT_INODE);
    if (root == nullptr)
    {
        return false;
    }
    const bool initialized = FileSystem::initialize_directory(root, ROOT_INODE); // ルートの親は自分自身
    FileSystem::put_inode(root);
    return initialized;
}


}// namespace

namespace FileSystem
{

Manager::Manager(uint32_t total_blocks, IBlockStore *block_store, IConsole *console)
{
    FileSystem::block_store = block_store ? block_store : null_block_store;
    FileSystem::console     = console ? console : null_console;

    if(internal::load_superblock())
    {
        valid_ = true;
        return;
    }

    // if the magic number is not matched, the disk is not formatted yet. format it.
    FileSystem::console->puts("[FS]   not formatted, creating filesystem...\n");
    
    if(!format(total_blocks, console))
    {
        valid_ = false;
        return;
    }
    valid_ = internal::load_superblock();
}

std::optional<uint32_t> allocate_block(){
    const SuperBlock &superblock = FileSystem::superblock();

    for(uint32_t base = 0; base < superblock.size; base += BLOCKS_PER_BITMAP_BLOCK){
        auto block = FileSystem::block_store->acquire(bitmap_block(base));
        if(!block) return std::nullopt;
        auto *bitmap = block.data();
        for(uint32_t offset=0; offset<BLOCKS_PER_BITMAP_BLOCK && base+offset<superblock.size; offset++){
            uint32_t byte_index = offset / BITS_PER_BYTE;
            uint8_t bit_mask = 1 << (offset % BITS_PER_BYTE);
            if((bitmap[byte_index] & bit_mask) == 0){
                // mark this block as used
                bitmap[byte_index] |= bit_mask;
                if(!block.write_back()){
                    return std::nullopt;
                }
                block.reset();
                uint32_t block_number = base + offset;
                if(!zero_block(block_number)){
                    return std::nullopt;
                }
                return block_number;
            }
        }
    }
    return std::nullopt;
}

void free_block(uint32_t blockno)
{
    auto block = block_store->acquire(bitmap_block(blockno));
    if (!block)
    {
        return;
    }
    uint32_t bit_index = blockno % BLOCKS_PER_BITMAP_BLOCK;
    block.data()[bit_index / BITS_PER_BYTE] &= static_cast<uint8_t>(~(1u << (bit_index % BITS_PER_BYTE)));
    block.write_back();
}

} // namespace FileSystem
