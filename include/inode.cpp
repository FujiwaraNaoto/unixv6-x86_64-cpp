#include "inode.hpp"
#include "file_system_internal.hpp"
#include <array>
#include <cstring>

namespace FileSystem
{
extern IBlockStore *block_store;
} // namespace FileSystem

namespace
{
using FileSystem::Inode;
using FileSystem::internal::read_disk_inode;
using FileSystem::internal::write_disk_inode;

// メモリ上の inode テーブル。
// 既定値を持たない POD なので .bss に置かれ、ゼロ初期化される
// (= ref 0 / valid false / inum 0 で始まる)。実行時のコンストラクタは要らない。
std::array<Inode, FileSystem::NUM_OPEN_INODES> inode_table;

// 1 ブロックに入る間接ブロックのエントリ数分の添字が範囲内か
bool valid_inum(uint32_t inum)
{
    // inum 0 は「エントリが未使用」を表す番兵なので、有効な inode には使わない
    return inum != 0 && inum < FileSystem::superblock().ninodes;
}

// 間接ブロックを uint32_t の配列として見る
uint32_t *indirect_entries(uint8_t *data)
{
    return reinterpret_cast<uint32_t *>(data);
}

} // namespace

namespace FileSystem
{

Inode *get_inode(uint32_t inum)
{
    if (!valid_inum(inum))
    {
        return nullptr;
    }

    Inode *empty = nullptr;
    for (auto &entry : inode_table)
    {
        if (entry.ref > 0 && entry.inum == inum)
        {
            entry.ref++; // 既に載っている: 同じ実体を共有する
            return &entry;
        }
        if (empty == nullptr && entry.ref == 0)
        {
            empty = &entry;
        }
    }
    if (empty == nullptr)
    {
        return nullptr; // テーブルが一杯
    }

    const auto disk = read_disk_inode(inum);
    if (!disk)
    {
        return nullptr;
    }

    empty->inum  = inum;
    empty->ref   = 1;
    empty->disk  = *disk;
    empty->valid = true;
    return empty;
}

void put_inode(Inode *ip)
{
    if (ip == nullptr || ip->ref <= 0)
    {
        return;
    }
    ip->ref--;
    if (ip->ref > 0)
    {
        return;
    }

    // 誰も参照しておらず、ディレクトリからのリンクも無い = このファイルは消えた。
    // 中身のブロックを返し、inode を未使用に戻す。
    if (ip->valid && ip->disk.nlink == 0)
    {
        truncate_inode(ip);
        ip->disk.type = InodeType::kUnused;
        update_inode(ip);
    }
    ip->valid = false;
}

bool update_inode(Inode *ip)
{
    if (ip == nullptr || !ip->valid)
    {
        return false;
    }
    return write_disk_inode(ip->inum, ip->disk);
}

std::optional<uint32_t> allocate_inode(InodeType type)
{
    for (uint32_t inum = 1; inum < superblock().ninodes; inum++)
    {
        const auto disk = read_disk_inode(inum);
        if (!disk)
        {
            return std::nullopt;
        }
        if (disk->type != InodeType::kUnused)
        {
            continue;
        }

        // nlink は 0 で作る。ディレクトリに登録した時点で link_directory() が数える
        // (名前の数と nlink が必ず一致するように)。
        DiskInode allocated{};
        allocated.type  = type;
        allocated.nlink = 0;
        allocated.size  = 0;
        if (!write_disk_inode(inum, allocated))
        {
            return std::nullopt;
        }
        return inum;
    }
    return std::nullopt; // 空き inode が無い
}

std::optional<uint32_t> block_map(Inode *ip, uint32_t block_index, bool allocate)
{
    if (ip == nullptr || !ip->valid)
    {
        return std::nullopt;
    }

    // 直接ブロック: inode がブロック番号をそのまま持っている
    if (block_index < NDIRECT)
    {
        uint32_t blockno = ip->disk.addrs[block_index];
        if (blockno == 0)
        {
            if (!allocate)
            {
                return std::nullopt;
            }
            const auto allocated = allocate_block();
            if (!allocated)
            {
                return std::nullopt;
            }
            blockno                     = *allocated;
            ip->disk.addrs[block_index] = blockno;
        }
        return blockno;
    }

    // 間接ブロック: addrs[NDIRECT] が「ブロック番号の表」を指す
    const uint32_t indirect_index = block_index - NDIRECT;
    if (indirect_index >= NINDIRECT)
    {
        return std::nullopt; // ファイルの最大サイズを超えている
    }

    uint32_t table_blockno = ip->disk.addrs[NDIRECT];
    if (table_blockno == 0)
    {
        if (!allocate)
        {
            return std::nullopt;
        }
        const auto allocated = allocate_block();
        if (!allocated)
        {
            return std::nullopt;
        }
        table_blockno            = *allocated;
        ip->disk.addrs[NDIRECT] = table_blockno;
    }

    auto table = block_store->acquire(table_blockno);
    if (!table)
    {
        return std::nullopt;
    }
    auto *entries    = indirect_entries(table.data());
    uint32_t blockno = entries[indirect_index];
    if (blockno == 0)
    {
        if (!allocate)
        {
            return std::nullopt;
        }
        const auto allocated = allocate_block();
        if (!allocated)
        {
            return std::nullopt;
        }
        blockno                  = *allocated;
        entries[indirect_index] = blockno;
        if (!table.write_back())
        {
            return std::nullopt;
        }
    }
    return blockno;
}

void truncate_inode(Inode *ip)
{
    if (ip == nullptr || !ip->valid)
    {
        return;
    }

    for (int i = 0; i < NDIRECT; i++)
    {
        if (ip->disk.addrs[i] != 0)
        {
            free_block(ip->disk.addrs[i]);
            ip->disk.addrs[i] = 0;
        }
    }

    if (ip->disk.addrs[NDIRECT] != 0)
    {
        {
            auto table = block_store->acquire(ip->disk.addrs[NDIRECT]);
            if (!table)
            {
                // 表を読めない場合は中身を辿れないので、表そのものだけ解放する
            }
            else
            {
                auto *entries = indirect_entries(table.data());
                for (int i = 0; i < NINDIRECT; i++)
                {
                    if (entries[i] != 0)
                    {
                        free_block(entries[i]);
                    }
                }
            }
        } // ここで BlockRef を手放してから、表そのものを解放する
        free_block(ip->disk.addrs[NDIRECT]);
        ip->disk.addrs[NDIRECT] = 0;
    }

    ip->disk.size = 0;
    update_inode(ip);
}

std::optional<uint32_t> read_inode(Inode *ip, void *dst, uint32_t offset, uint32_t n)
{
    if (ip == nullptr || !ip->valid || dst == nullptr)
    {
        return std::nullopt;
    }
    if (offset > ip->disk.size)
    {
        return std::nullopt;
    }
    // ファイル末尾を超える分は読まない
    if (offset + n > ip->disk.size)
    {
        n = ip->disk.size - offset;
    }

    auto *out = static_cast<uint8_t *>(dst);
    uint32_t done = 0;
    while (done < n)
    {
        const uint32_t block_index  = (offset + done) / FSBLOCK_SIZE;
        const uint32_t block_offset = (offset + done) % FSBLOCK_SIZE;
        uint32_t count              = FSBLOCK_SIZE - block_offset;
        if (count > n - done)
        {
            count = n - done;
        }

        const auto blockno = block_map(ip, block_index, false);
        if (!blockno)
        {
            // 未割り当て (穴) はゼロとして読む。読み出しでブロックは確保しない。
            std::memset(out + done, 0, count);
            done += count;
            continue;
        }

        auto block = block_store->acquire(*blockno);
        if (!block)
        {
            return std::nullopt;
        }
        std::memcpy(out + done, block.data() + block_offset, count);
        done += count;
    }
    return done;
}

std::optional<uint32_t> write_inode(Inode *ip, const void *src, uint32_t offset, uint32_t n)
{
    if (ip == nullptr || !ip->valid || src == nullptr)
    {
        return std::nullopt;
    }
    // 穴を作らないよう、書き始めは末尾まで
    if (offset > ip->disk.size)
    {
        return std::nullopt;
    }
    if (offset + n > static_cast<uint32_t>(MAXFILE) * FSBLOCK_SIZE)
    {
        return std::nullopt; // ファイルの最大サイズを超える
    }

    const auto *in = static_cast<const uint8_t *>(src);
    uint32_t done  = 0;
    while (done < n)
    {
        const uint32_t block_index  = (offset + done) / FSBLOCK_SIZE;
        const uint32_t block_offset = (offset + done) % FSBLOCK_SIZE;
        uint32_t count              = FSBLOCK_SIZE - block_offset;
        if (count > n - done)
        {
            count = n - done;
        }

        const auto blockno = block_map(ip, block_index, true);
        if (!blockno)
        {
            break; // ブロックを確保できない: ここまでを書き込み済みとして返す
        }

        auto block = block_store->acquire(*blockno);
        if (!block)
        {
            break;
        }
        std::memcpy(block.data() + block_offset, in + done, count);
        if (!block.write_back())
        {
            break;
        }
        done += count;
    }

    if (offset + done > ip->disk.size)
    {
        ip->disk.size = offset + done;
    }
    // block_map() が addrs を書き換えている場合があるので、必ず書き戻す
    if (!update_inode(ip))
    {
        return std::nullopt;
    }
    return done;
}

} // namespace FileSystem
