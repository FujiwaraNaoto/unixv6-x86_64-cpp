#ifndef INODE_HPP
#define INODE_HPP

#include "file_system.hpp"
#include <array>

struct [[gnu::packed]] Inode
{
    uint32_t inode_num;
    int refcnt; //0なら空きスロット
    bool valid; //　ディスクから読み込み済みか?
    DiskInode disk; // ディスク上の内容そのもの
};


class InodeRef final
{
public:
    InodeRef() = default;
    explicit InodeRef(Inode *inode): inode_(inode){};
    
    ~InodeRef(){reset();}

    InodeRef(const InodeRef&)=delete;// copyさせない
    InodeRef &operator=(const InodeRef &)=delete;

    InodeRef(InodeRef &&other) noexcept: inode_(other.inode_)
    {
        other.inode_ = nullptr;
    }

    InodeRef &operator=(InodeRef &&other) noexcept
    {
        if(this!=&other)
        {
            reset();
            inode_ = other.inode_;
            other.inode_=nullptr;
        }
        return *this;
    }


    void reset()
    {
        if(inode_!=nullptr){
            put_inode(inode_);
            inode_ = nullptr;
        }
    }

private:
    Inode *inode_ = nullptr;
};


namespace FileSystem::Inode{

    const int NUM_INODE = 10;
    std::array<Inode,NUM_INODE> inode_table;

    void copy_from_disk(Inode &inode, const DiskInode &disk);

    DiskInode to_disk(const Inode &inode);

    // inode番号inode_numのinodeを取得する
    // テーブルに載っていればそれを共有してrefcntを増やす
    InodeRef get_inode(uint32_t inode_num);

    //ディスクの空きinode(typeがkUnused)を確保してtypeを設定する。
    InodeRef allocate_inode(InodeType type);
}


#endif //INODE_HPP
