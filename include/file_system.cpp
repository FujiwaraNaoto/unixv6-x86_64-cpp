#include "file_system.hpp"
#include "file_system_internal.hpp"
#include <cstdint>
#include <cstring>

// file_system.hpp で extern 宣言している変数の実体。
// Manager のコンストラクタが差し替え、各関数はここから読む。
namespace FileSystem
{
IBlockStore *block_store = nullptr;
IConsole *console        = nullptr;
} // namespace FileSystem

namespace {
    uint32_t bitmap_block(uint32_t blockno)
    {
        return blockno / BLOCKS_PER_BITMAP_BLOCK + FileSystem::superblock().bitmap_start;
    }
}

namespace
{
using FileSystem::internal::mark_block_used;
using FileSystem::internal::write_inode;
using FileSystem::internal::zero_block;


    bool add_root_entry(DiskInode &root, uint32_t inum, const char* name){
        int root_address = root.addrs[0];
        if(root_address == 0){
            auto block_number = FileSystem::allocate_block();
            if(!block_number){
                return false;
            }
            root_address = block_number.value();
            root.addrs[0] = root_address;
        }

        auto block = FileSystem::block_store->acquire(root_address);
        if(!block) return false;

        auto *entries = reinterpret_cast<DirectoryEntry*>(block.data());
        int capacity = FSBLOCK_SIZE / sizeof(DirectoryEntry);

        for(int i=0; i<capacity; i++){
            if(entries[i].inum !=0) continue;

            entries[i].inum = inum;
            std::strncpy(entries[i].name, name, DIRSIZ);
            if(!block.write_back()){
                return false;
            }
            root.size += sizeof(DirectoryEntry);
            return true;
        }

        return false;
    }


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

    DiskInode root_inode{
        .type  = InodeType::kDirectory,
        .nlink = 1,
        .size  = 0,
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
    console->puts("[FS]   not formatted, creating filesystem...\n");
    
    if(!format(total_blocks, console))
    {
        valid_ = false;
        return;
    }
    valid_ = internal::load_superblock();
}

std::optional<uint32_t> allocate_block(){

    const SuperBlock &superblock = FileSystem::internal::mutable_superblock();

    for(uint32_t base = 0; base < superblock.nblocks; base += BLOCKS_PER_BITMAP_BLOCK){
        auto block = FileSystem::block_store->acquire(bitmap_block(base));
        if(!block) return std::nullopt;
        auto *bitmap = block.data();
        for(uint32_t offset=0; offset<BLOCKS_PER_BITMAP_BLOCK && base+offset<superblock.nblocks; offset++){
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


} // namespace FileSystem
