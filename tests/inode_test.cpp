#include "inode.hpp"
#include "file_system.hpp"
#include "file_system_internal.hpp"

namespace tests
{

bool first_inode_is_root_directory(IConsole *console){
    InodeRef root = FileSystem::get_inode(ROOT_INODE);
    const bool ok = root && root->disk.type == InodeType::kDirectory;
    
    console->printf("[INODE]:first_inode_is_root_directory");

    return ok;
}

bool inode_refcount(IConsole *console){
    Inode *slot = nullptr;
    
    bool ok=false;
    {
        InodeRef a = FileSystem::get_inode(ROOT_INODE);
        InodeRef b = FileSystem::get_inode(ROOT_INODE);
        ok   = a && b && a.get() == b.get() && a->refcnt == 2;
        slot = a.get();
    }   // b → a の順にデストラクタ → put_inode が 2 回
    ok = ok && slot->refcnt == 0;

    console->printf("[INODE]:inode_refcount");
    return ok;
}

bool inode_write_disk(IConsole *console){
    InodeRef file = FileSystem::allocate_inode(InodeType::kFile);
    bool ok = file && file->inode_num >= 2;      // 0 は未使用、1 はルート
    auto disk = FileSystem::internal::read_inode(file->inode_num);
    ok = ok && disk && disk->type == InodeType::kFile;
    int inum = file->inode_num;
    FileSystem::internal::write_inode(inum, DiskInode{});   // 後片付け

    console->printf("[INODE]:inode_write_disk");
    return ok;
}
    

}
