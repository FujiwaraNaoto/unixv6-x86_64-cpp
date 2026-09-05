#ifndef INODE_HPP
#define INODE_HPP
#include <cstdint>
#include "file_system.hpp"

// ─── inode 層 ────────────────────────────────────────────────────
// ディスク上の DiskInode をメモリ上でキャッシュし、参照カウントで管理する。
//
// 位置づけ:
//   ディレクトリ層 / パス解決  ←→  inode 層 (ここ)  ←→  ブロック割り当て
//
// この層が隠すのは「ファイルの N バイト目がどのブロックか」という対応づけ。
// 上位層はバイトオフセットだけを見ればよく、直接ブロックと間接ブロックの
// 区別を意識しない (bmap は inode.cpp の内部に閉じている)。

namespace FileSystem
{

// メモリ上の inode。
//
// ref と disk.nlink は別物なので混同しないこと。
//   ref        … このメモリ上のスロットを握っている InodeRef の数
//   disk.nlink … ディスク上でこの inode を指しているディレクトリエントリの数
// ref が 0 になった時点で nlink も 0 なら、初めて実データを解放してよい。
struct Inode
{
    uint32_t inum = 0;     // inode 番号 (0 なら未使用スロット)
    int ref       = 0;     // 参照カウント
    bool valid    = false; // disk がディスクから読み込み済みか
    DiskInode disk{};
};

namespace detail
{
// InodeRef のデストラクタから呼ぶ。参照を1つ減らし、必要なら実体を解放する。
void iput(Inode *node);
} // namespace detail

// inode への参照。スコープを抜けると自動で iput() する。
// BufferCache::BufferRef と同じ形に揃えてあるので、使い勝手は同じ。
//
//   if (auto node = FileSystem::iget(ROOTINO))
//   {
//       node->size;          // DiskInode のメンバに直接アクセス
//       node.update();       // 変更をディスクへ書き戻す
//   }
class InodeRef final
{
  public:
    InodeRef() = default;
    explicit InodeRef(Inode *node) : node_(node) { }
    ~InodeRef()
    {
        reset();
    }

    InodeRef(const InodeRef &)            = delete;
    InodeRef &operator=(const InodeRef &) = delete;

    InodeRef(InodeRef &&other) noexcept : node_(other.node_)
    {
        other.node_ = nullptr;
    }
    InodeRef &operator=(InodeRef &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            node_       = other.node_;
            other.node_ = nullptr;
        }
        return *this;
    }

    explicit operator bool() const
    {
        return node_ != nullptr;
    }

    Inode *get() const
    {
        return node_;
    }
    // DiskInode のメンバへ直接アクセスする。node->size のように書ける。
    DiskInode *operator->() const
    {
        return &node_->disk;
    }
    DiskInode &operator*() const
    {
        return node_->disk;
    }

    uint32_t inum() const
    {
        return node_ != nullptr ? node_->inum : 0;
    }

    // メモリ上の変更をディスクへ書き戻す。
    // disk のフィールドを書き換えたら必ず呼ぶこと。
    bool update() const;

    void reset()
    {
        if (node_ != nullptr)
        {
            detail::iput(node_);
            node_ = nullptr;
        }
    }

  private:
    Inode *node_ = nullptr;
};

// inum の inode への参照を得る。中身はここで読み込む。
// 存在しない / 読めない場合は空の InodeRef を返す。
InodeRef iget(uint32_t inum);

// 空き inode を1つ確保して type を設定し、参照を返す。
// nlink は 0 のままなので、呼び出し側が dirlink() したあとに 1 にすること。
InodeRef ialloc(InodeType type);

// off から n バイト読む。実際に読めたバイト数を返す。
// ファイル末尾を越える分は切り詰める。
uint32_t readi(const InodeRef &node, uint8_t *dst, uint32_t off, uint32_t n);

// off へ n バイト書く。実際に書けたバイト数を返す。
// 必要なブロックは自動で確保し、size が伸びれば inode も書き戻す。
uint32_t writei(const InodeRef &node, const uint8_t *src, uint32_t off, uint32_t n);

// ファイルの中身をすべて解放し、size を 0 にする。
// inode 自体は残るので、削除するときは nlink を 0 にしてから参照を手放す。
void itrunc(const InodeRef &node);

} // namespace FileSystem

#endif // INODE_HPP
