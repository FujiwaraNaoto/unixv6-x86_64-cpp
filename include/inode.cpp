#include "inode.hpp"
#include "file_system_internal.hpp"
#include "buffer_cache.hpp"

#include <cstring>

namespace
{

constexpr int NINODE = 50; // メモリ上に同時に保持する inode 数

FileSystem::Inode inode_table[NINODE];

// ─── bmap ───────────────────────────────────────────────────────
// 論理ブロック番号 n (ファイルの先頭から数えたブロック) を
// 物理ブロック番号に変換する。alloc が true なら未割り当てのときに確保する。
// 0 を返したら失敗、または alloc=false で未割り当て。
//
// この関数を公開しないのは、上位層に「論理ブロック番号」という概念を
// 漏らさないため。あとで二重間接ブロックを足しても影響がここに閉じる。
uint32_t bmap(FileSystem::Inode &node, uint32_t n, bool alloc)
{
    if (n < static_cast<uint32_t>(NDIRECT))
    {
        if (node.disk.addrs[n] == 0 && alloc)
        {
            node.disk.addrs[n] = FileSystem::allocate_block();
        }
        return node.disk.addrs[n];
    }

    n -= static_cast<uint32_t>(NDIRECT);
    if (n >= static_cast<uint32_t>(NINDIRECT))
    {
        return 0; // MAXFILE 超過
    }

    // 間接ブロック本体がまだ無ければ確保する
    if (node.disk.addrs[NDIRECT] == 0)
    {
        if (!alloc)
        {
            return 0;
        }
        uint32_t indirect = FileSystem::allocate_block();
        if (indirect == 0)
        {
            return 0;
        }
        node.disk.addrs[NDIRECT] = indirect;
    }

    // 一度目の read: 既に割り当て済みならそのまま返す
    {
        auto block = BufferCache::acquire(node.disk.addrs[NDIRECT]);
        if (!block)
        {
            return 0;
        }
        uint32_t existing = reinterpret_cast<const uint32_t *>(block->data)[n];
        if (existing != 0 || !alloc)
        {
            return existing;
        }
    }
    // ここでバッファは解放済み。
    // allocate_block() は内部で zero_block() → acquire() を呼ぶので、
    // 間接ブロックを握ったまま呼ぶとバッファを同時に2枚押さえることになる。

    uint32_t newblock = FileSystem::allocate_block();
    if (newblock == 0)
    {
        return 0;
    }

    auto block = BufferCache::acquire(node.disk.addrs[NDIRECT]);
    if (!block)
    {
        FileSystem::free_block(newblock);
        return 0;
    }
    reinterpret_cast<uint32_t *>(block->data)[n] = newblock;
    if (!block.write())
    {
        return 0;
    }
    return newblock;
}

// 中身をすべて解放する。
// 間接ブロックは「中の128個 → 間接ブロック自身」の順に解放すること。
// 逆にすると解放済みブロックを読むことになる。
void truncate_node(FileSystem::Inode &node)
{
    for (int i = 0; i < NDIRECT; i++)
    {
        if (node.disk.addrs[i] != 0)
        {
            FileSystem::free_block(node.disk.addrs[i]);
            node.disk.addrs[i] = 0;
        }
    }

    if (node.disk.addrs[NDIRECT] != 0)
    {
        {
            auto block = BufferCache::acquire(node.disk.addrs[NDIRECT]);
            if (block)
            {
                const auto *table = reinterpret_cast<const uint32_t *>(block->data);
                for (int i = 0; i < NINDIRECT; i++)
                {
                    if (table[i] != 0)
                    {
                        FileSystem::free_block(table[i]);
                    }
                }
            }
        }
        FileSystem::free_block(node.disk.addrs[NDIRECT]);
        node.disk.addrs[NDIRECT] = 0;
    }

    node.disk.size = 0;
}

} // namespace

namespace FileSystem
{

bool InodeRef::update() const
{
    if (node_ == nullptr)
    {
        return false;
    }
    return detail::write_inode(node_->inum, node_->disk);
}

namespace detail
{

void iput(Inode *node)
{
    if (node == nullptr || node->ref <= 0)
    {
        return;
    }

    node->ref--;
    if (node->ref > 0)
    {
        return;
    }

    // 誰も参照しておらず、ディレクトリからも指されていない → 実体を捨てる
    if (node->valid && node->disk.nlink == 0 && node->disk.type != InodeType::kUnused)
    {
        truncate_node(*node);
        node->disk.type = InodeType::kUnused;
        write_inode(node->inum, node->disk);
    }

    node->inum  = 0;
    node->valid = false;
}

} // namespace detail

InodeRef iget(uint32_t inum)
{
    if (inum == 0)
    {
        return {};
    }

    // 既にメモリ上にあれば同じスロットを共有する。
    // ここを外すと同じファイルの実体が2つできて整合性が壊れる。
    Inode *empty = nullptr;
    for (auto &slot : inode_table)
    {
        if (slot.ref > 0 && slot.inum == inum)
        {
            slot.ref++;
            return InodeRef{&slot};
        }
        if (empty == nullptr && slot.ref == 0)
        {
            empty = &slot;
        }
    }

    if (empty == nullptr)
    {
        return {}; // inode テーブルが枯渇
    }

    if (!detail::read_inode(inum, empty->disk))
    {
        return {};
    }
    empty->inum  = inum;
    empty->ref   = 1;
    empty->valid = true;
    return InodeRef{empty};
}

InodeRef ialloc(InodeType type)
{
    const uint32_t ninodes = superblock().ninodes;
    for (uint32_t inum = ROOTINO; inum < ninodes; inum++)
    {
        DiskInode candidate{};
        if (!detail::read_inode(inum, candidate))
        {
            return {};
        }
        if (candidate.type != InodeType::kUnused)
        {
            continue;
        }

        // 使い回しのゴミが残らないよう全体を作り直す
        DiskInode fresh{};
        fresh.type  = type;
        fresh.nlink = 0; // dirlink() 後に呼び出し側が 1 にする
        if (!detail::write_inode(inum, fresh))
        {
            return {};
        }
        return iget(inum);
    }
    return {}; // 空き inode なし
}

uint32_t readi(const InodeRef &node, uint8_t *dst, uint32_t off, uint32_t n)
{
    Inode *self = node.get();
    if (self == nullptr || dst == nullptr)
    {
        return 0;
    }
    if (off > self->disk.size)
    {
        return 0;
    }
    if (n > self->disk.size - off) // 引き算で書くことで off+n のオーバーフローを避ける
    {
        n = self->disk.size - off;
    }

    uint32_t done = 0;
    while (done < n)
    {
        uint32_t position = off + done;
        uint32_t blockno  = bmap(*self, position / BLOCK_SIZE, false);
        uint32_t inner    = position % BLOCK_SIZE;
        uint32_t chunk    = BLOCK_SIZE - inner;
        if (chunk > n - done)
        {
            chunk = n - done;
        }

        if (blockno == 0)
        {
            // 穴あきファイル。未割り当ての領域はゼロとして読ませる。
            std::memset(dst + done, 0, chunk);
            done += chunk;
            continue;
        }

        auto block = BufferCache::acquire(blockno);
        if (!block)
        {
            break;
        }
        std::memcpy(dst + done, block->data + inner, chunk);
        done += chunk;
    }
    return done;
}

uint32_t writei(const InodeRef &node, const uint8_t *src, uint32_t off, uint32_t n)
{
    Inode *self = node.get();
    if (self == nullptr || src == nullptr)
    {
        return 0;
    }
    if (off > MAX_FILE_BYTES)
    {
        return 0;
    }
    if (n > MAX_FILE_BYTES - off) // ここも引き算にしてオーバーフローを避ける
    {
        n = MAX_FILE_BYTES - off;
    }

    uint32_t done = 0;
    while (done < n)
    {
        uint32_t position = off + done;
        uint32_t blockno  = bmap(*self, position / BLOCK_SIZE, true);
        if (blockno == 0)
        {
            break; // 空きブロックなし
        }

        auto block = BufferCache::acquire(blockno);
        if (!block)
        {
            break;
        }
        uint32_t inner = position % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - inner;
        if (chunk > n - done)
        {
            chunk = n - done;
        }
        std::memcpy(block->data + inner, src + done, chunk);
        if (!block.write())
        {
            break;
        }
        done += chunk;
    }

    // bmap が addrs を書き換えている可能性があるので、
    // size が伸びなかった場合でも inode は書き戻す。
    if (done > 0)
    {
        if (off + done > self->disk.size)
        {
            self->disk.size = off + done;
        }
        detail::write_inode(self->inum, self->disk);
    }
    return done;
}

void itrunc(const InodeRef &node)
{
    Inode *self = node.get();
    if (self == nullptr)
    {
        return;
    }
    truncate_node(*self);
    detail::write_inode(self->inum, self->disk);
}

} // namespace FileSystem
