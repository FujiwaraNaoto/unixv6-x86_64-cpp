#include "inode.hpp"
#include "file_system.hpp"
#include "file_system_internal.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

namespace FileSystem
{
namespace
{
// ヘッダに置くと include した翻訳単位ごとに実体ができるので、ここに置く
std::array<Inode, NUM_INODE> inode_table{};
} // namespace

void copy_from_disk(Inode &inode, const DiskInode &disk)
{
    inode.disk = disk;
}


DiskInode to_disk(const Inode &inode)
{
    return inode.disk;
}

InodeRef get_inode(InodeNumber inode_number)
{
    Inode *empty = nullptr;
    for (Inode &inode : inode_table)
    {
        if (inode.refcnt > 0 and inode.inode_number == inode_number)
        {
            inode.refcnt++;
            return InodeRef{&inode};
        }
        if (empty == nullptr and inode.refcnt == 0)
        {
            empty = &inode;
        }
    }

    if (empty == nullptr)
    {
        return InodeRef{};
    }
    auto disk = internal::read_inode(inode_number);
    if (!disk)
        return InodeRef{};
    empty->inode_number = inode_number;
    empty->refcnt       = 1;
    empty->valid        = true;
    copy_from_disk(*empty, disk.value());
    return InodeRef{empty};
}

InodeRef allocate_inode(InodeType type)
{
    if (type == InodeType::kUnused)
        return InodeRef{};
    const SuperBlock &superblock = FileSystem::superblock();

    const uint32_t num_super_block_inodes = superblock.ninodes;
    for (InodeNumber inode_number{1}; inode_number.value < num_super_block_inodes; inode_number.value++)
    {
        auto disk = internal::read_inode(inode_number);
        if (!disk)
        {
            return InodeRef{};
        }
        if (disk->type != InodeType::kUnused)
            continue;

        DiskInode fresh{};
        fresh.type = type;
        if (!internal::write_inode(inode_number, fresh))
            return InodeRef{};
        return get_inode(inode_number);
    }

    return InodeRef{};
}

void put_inode(Inode *inode)
{
    if (inode == nullptr)
        return;
    if (inode->refcnt > 0)
        inode->refcnt--;
}

// ファイル内の logical 番目のブロックが、ディスクの何番ブロックかを返す (論理ブロック番号 → 物理ブロック番号)。
// 未割当なら失敗する。ブロックは確保しない。
std::optional<uint32_t> find_block(const Inode &inode, uint32_t logical)
{
    const auto &addrs = inode.disk.addrs;

    if (logical < NDIRECT)
    {
        if (addrs[logical] == 0)
            return std::nullopt;
        return addrs[logical];
    }

    logical -= NDIRECT;
    if (logical >= NINDIRECT)
        return std::nullopt; // MAXFILE 超え

    if (addrs[INDIRECT_INDEX] == 0)
        return std::nullopt;

    auto block = internal::fs.block_store->acquire(addrs[INDIRECT_INDEX]);
    if (!block)
        return std::nullopt;
    const auto *table = reinterpret_cast<const uint32_t *>(block.data());
    if (table[logical] == 0)
        return std::nullopt;
    return table[logical];
}

// find_block と同じだが、未割当なら確保する。
// addrs を書き換えるので、呼び出し側で update_inode すること。
std::optional<uint32_t> allocate_or_find_block(Inode &inode, uint32_t logical)
{
    auto &addrs = inode.disk.addrs;

    if (logical < NDIRECT)
    {
        if (addrs[logical] == 0)
        {
            auto b = FileSystem::allocate_block();
            if (!b)
                return std::nullopt;
            addrs[logical] = *b;
        }
        return addrs[logical];
    }

    logical -= NDIRECT;
    if (logical >= NINDIRECT)
        return std::nullopt; // MAXFILE 超え

    if (addrs[INDIRECT_INDEX] == 0)
    {
        auto b = FileSystem::allocate_block(); // 0 埋めされて返るので中身は全部「未割当」
        if (!b)
            return std::nullopt;
        addrs[INDIRECT_INDEX] = *b;
    }

    auto block = internal::fs.block_store->acquire(addrs[INDIRECT_INDEX]);
    if (!block)
        return std::nullopt;
    auto *table = reinterpret_cast<uint32_t *>(block.data());
    if (table[logical] == 0)
    {
        auto b = FileSystem::allocate_block();
        if (!b)
            return std::nullopt;
        table[logical] = *b;
        if (!block.write_back())
            return std::nullopt;
    }
    return table[logical];
}

// offset から最大 count バイトを dest に読む。読んだバイト数を返す。
// ファイル末尾 (disk.size) より先は読まない。offset が末尾より先なら失敗。
// 読み込みではブロックを確保しない。write_data は穴を作らないので、size 以内で未割当なら失敗する。
std::optional<uint32_t> read_data(const Inode &inode, uint8_t *dest, uint32_t offset, uint32_t count)
{
    const uint32_t size = inode.disk.size;
    if (offset > size)
        return std::nullopt;
    if (count > size - offset)
        count = size - offset;

    uint32_t done = 0;
    while (done < count)
    {
        const uint32_t pos = offset + done;
        auto blockno       = find_block(inode, pos / FSBLOCK_SIZE);
        if (!blockno)
            return std::nullopt;
        auto block = internal::fs.block_store->acquire(*blockno);
        if (!block)
            return std::nullopt;

        // このブロックの中で読める分だけ (ブロック境界か残りの少ない方)
        const uint32_t in_block = pos % FSBLOCK_SIZE;
        const uint32_t left     = count - done;
        const uint32_t chunk    = std::min(left, FSBLOCK_SIZE - in_block);
        std::memcpy(dest + done, block.data() + in_block, chunk);
        done += chunk;
    }
    return done;
}

// source の count バイトを offset に書く。書けたバイト数を返す。
// 穴を作らないよう、offset が末尾より先なら失敗。MAXFILE を超える書き込みも失敗。
// 途中でブロックが尽きたら、そこまで書いた分を返す (size もそこまで伸ばす)。
std::optional<uint32_t> write_data(Inode &inode, const uint8_t *source, uint32_t offset, uint32_t count)
{
    constexpr uint32_t max_bytes = MAXFILE * FSBLOCK_SIZE;
    if (offset > inode.disk.size)
        return std::nullopt;
    if (offset > max_bytes or count > max_bytes - offset)
        return std::nullopt;

    uint32_t done = 0;
    while (done < count)
    {
        const uint32_t pos = offset + done;
        auto blockno       = allocate_or_find_block(inode, pos / FSBLOCK_SIZE);
        if (!blockno)
            break;
        auto block = internal::fs.block_store->acquire(*blockno);
        if (!block)
            break;

        const uint32_t in_block = pos % FSBLOCK_SIZE;
        const uint32_t left     = count - done;
        const uint32_t chunk    = std::min(left, FSBLOCK_SIZE - in_block);
        std::memcpy(block.data() + in_block, source + done, chunk);
        if (!block.write_back())
            break;
        done += chunk;
    }

    if (offset + done > inode.disk.size)
        inode.disk.size = offset + done;
    // allocate_or_find_block が addrs を書き換えている可能性があるので、size が変わらなくても書き戻す
    if (!update_inode(inode))
        return std::nullopt;
    return done;
}


bool update_inode(const Inode &inode)
{
    return internal::write_inode(inode.inode_number, inode.disk);
}

} // namespace FileSystem
