#include "inode.hpp"
#include "file_system.hpp"
#include "file_system_internal.hpp"
#include <cstdint>
#include <optional>

namespace FileSystem{

    // ヘッダに置くと include した翻訳単位ごとに実体ができるので、ここに置く
    std::array<Inode,NUM_INODE> inode_table;

    void copy_from_disk(Inode &inode, const DiskInode &disk){
        inode.disk = disk;
    }


    DiskInode to_disk(const Inode &inode)
    {
        return inode.disk;
    }


InodeRef get_inode(uint32_t inode_num)
{
    Inode* empty = nullptr;
    for(Inode &inode: inode_table){
        
    }

    if(empty==nullptr){
        return InodeRef{};
    }
    auto disk = internal::read_inode(inode_num);
    if(!disk) return InodeRef{};
    empty->inode_num = inode_num;
    empty->refcnt=1;
    empty->valid = true;
    copy_from_disk(*empty,disk.value());
    return InodeRef{empty};
}

InodeRef allocate_inode(InodeType type){
    if(type==InodeType::kUnused) return InodeRef{};
    const SuperBlock &superblock = FileSystem::superblock();

    const uint32_t num_super_block_inodes = superblock.ninodes;
    for(uint32_t idx=1;idx<num_super_block_inodes;idx++){
        auto disk = internal::read_inode(idx);
        if(!disk){
            return InodeRef{};
        }
        if(disk->type!=InodeType::kUnused)continue;

        DiskInode fresh{};
        fresh.type = type;
        if(!internal::write_inode(idx,fresh)) return InodeRef{};
        return get_inode(idx);

    }

    return InodeRef{};
}

void put_inode(Inode *inode)
{
    if(inode->refcnt>0) inode->refcnt--;
}

std::optional<uint32_t> write_data(Inode &inode,const uint8_t *source, uint32_t offset, uint32_t count)
{
    return std::nullopt;
}

}
