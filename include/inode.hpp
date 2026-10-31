#ifndef INODE_HPP
#define INODE_HPP

#include "file_system.hpp"
#include <array>
#include <optional>

struct Inode
{
    uint32_t inode_number;
    int refcnt;     // 0なら空きスロット
    bool valid;     //　ディスクから読み込み済みか?
    DiskInode disk; // ディスク上の内容そのもの
};


class InodeRef;

namespace FileSystem
{

constexpr int NUM_INODE = 10;

// inode番号inode_numのinodeを取得する
// テーブルに載っていればそれを共有してrefcntを増やす
InodeRef get_inode(uint32_t inode_number);

//ディスクの空きinode(typeがkUnused)を確保してtypeを設定する。
InodeRef allocate_inode(InodeType type);

// refcnt を減らす。InodeRef のデストラクタから呼ばれる。
void put_inode(Inode *inode);

[[nodiscard]] bool update_inode(const Inode &);

// offset から最大 count バイトを dest に読む。読んだバイト数を返す (末尾で切り詰める)。
std::optional<uint32_t> read_data(const Inode &inode, uint8_t *dest, uint32_t offset, uint32_t count);

// source の count バイトを offset に書き、size と inode をディスクに反映する。書けたバイト数を返す。
std::optional<uint32_t> write_data(Inode &inode, const uint8_t *source, uint32_t offset, uint32_t count);
} // namespace FileSystem


class InodeRef final
{
  public:
    InodeRef() = default;
    explicit InodeRef(Inode *inode) : inode_(inode){};

    ~InodeRef()
    {
        reset();
    }

    InodeRef(const InodeRef &)            = delete; // copyさせない
    InodeRef &operator=(const InodeRef &) = delete;

    InodeRef(InodeRef &&other) noexcept : inode_(other.inode_)
    {
        other.inode_ = nullptr;
    }

    InodeRef &operator=(InodeRef &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            inode_       = other.inode_;
            other.inode_ = nullptr;
        }
        return *this;
    }


    void reset()
    {
        if (inode_ != nullptr)
        {
            FileSystem::put_inode(inode_);
            inode_ = nullptr;
        }
    }

    //取得に成功したかどうか
    explicit operator bool() const
    {
        return inode_ != nullptr;
    }

    Inode *operator->() const
    {
        return inode_;
    }

    Inode *get() const
    {
        return inode_;
    }

  private:
    Inode *inode_ = nullptr;
};


#endif // INODE_HPP
