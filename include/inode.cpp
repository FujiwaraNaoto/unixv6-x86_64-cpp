#include "inode.hpp"
#include "file_system.hpp"
#include <cstdint>
#include <optional>

namespace FileSystem::Inode{

    void copy_from_disk(Inode &inode, const DiskInode &disk){
        inode.type = disk.type;
        inode.major = disk.major;
        inode.minor = disk.minor;
        inode.nlink = disk.nlink;
        inode.size = disk.size;
        inode.addrs = disk.addrs;   
    }


    DiskInode to_disk(const Inode &inode)
    {
        DiskInode disk{};
        disk.type  = inode.type;
        disk.major = inode.major;
        disk.minor = inode.minor;
        disk.nlink = inode.nlink;
        disk.size  = inode.size;
        disk.addrs = inode.addrs;
        return disk;
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

std::optional<uint32_t> write_data(Inode &inode,const uint8_t *source, uint32_t offset, uint32_t count)
{
    return nullopt;
}

}
