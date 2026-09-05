#include "file_system.hpp"
#include "file_system_internal.hpp"
#include "block_store.hpp"
#include "console.hpp"

#include <cstring>

namespace
{

SuperBlock superblock_state{};

// console が渡されなかったときの出力先 (何もしない)
NullConsole null_console;

IConsole *console_or_null_object(IConsole *console)
{
    return console != nullptr ? console : &null_console;
}

// store が渡されていないときのフォールバック。常に失敗するので、
// 各関数が「毎回 nullptr かどうか」を判定しなくて済む。
class NullBlockStore final : public IBlockStore
{
  public:
    BlockRef acquire(uint32_t) override
    {
        return BlockRef{};
    }
    bool write_back(uint32_t) override
    {
        return false;
    }
    void release(uint32_t) override { }
};

NullBlockStore null_block_store;

// Manager のコンストラクタで差し替わる。それまでは何もしない実装を指す。
IBlockStore *block_store = &null_block_store;

// ルートディレクトリにエントリを追加する。
// フォーマット直後専用の簡易版で、ディレクトリが1ブロックに収まることを前提とする。
bool add_root_entry(DiskInode &root, uint32_t inum, const FileName &name)
{
    if (root.addrs[0] == 0)
    {
        auto blockno = FileSystem::allocate_block();
        if (!blockno.has_value())
        {
            return false;
        }
        root.addrs[0] = *blockno;
    }

    auto block = block_store->acquire(root.addrs[0]);
    if (!block)
    {
        return false;
    }

    auto *entries = reinterpret_cast<DiskDirEntry *>(block.data());
    int capacity  = FSBLOCK_SIZE / sizeof(DiskDirEntry);

    for (int i = 0; i < capacity; i++)
    {
        if (entries[i].inum != 0)
        {
            continue; // 使用中
        }

        // ここはフォーマット時のブートストラップなので、上位のディレクトリ層
        // (to_disk) には依存せず、ディスク上の並びを直接組み立てる。
        DiskDirEntry fresh{};
        fresh.inum = static_cast<uint16_t>(inum);
        name.copy_to(fresh.name, DIRECTORY_NAME_SIZE); // 余りはゼロ埋めされる
        entries[i] = fresh;

        if (!block.write())
        {
            return false;
        }
        root.size += sizeof(DiskDirEntry);
        return true;
    }
    return false; // 空きエントリなし
}


// ディスクをフォーマットする
bool format(uint32_t total_blocks, IConsole *console)
{
    constexpr uint32_t NINODES = 200;

    uint32_t inode_blocks  = (NINODES + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
    uint32_t bitmap_blocks = (total_blocks + BLOCKS_PER_BITMAP_BLOCK - 1) / BLOCKS_PER_BITMAP_BLOCK;

    superblock_state.magic      = FS_MAGIC;
    superblock_state.size       = total_blocks;
    superblock_state.ninodes    = NINODES;
    superblock_state.inodestart = 2; // 0=boot, 1=super
    superblock_state.bmapstart  = superblock_state.inodestart + inode_blocks;

    uint32_t data_start      = superblock_state.bmapstart + bitmap_blocks;
    superblock_state.nblocks = total_blocks - data_start;

    console->puts("[FS]   ");
    console->printf("formatting: size=%u inodestart=%u bmapstart=%u datastart=%u\n",
                    superblock_state.size,
                    superblock_state.inodestart,
                    superblock_state.bmapstart,
                    data_start);

    // zero clear inode and bitmap blocks.
    for (uint32_t b = superblock_state.inodestart; b < data_start; b++)
    {
        if (!FileSystem::detail::zero_block(b))
        {
            return false;
        }
    }

    // make meta blocks used (0〜data_start-1)
    for (uint32_t b = 0; b < data_start; b++)
    {
        if (!FileSystem::detail::mark_block_used(b))
        {
            return false;
        }
    }

    // write superblock to block 1. 0 is boot block, so skip it.
    {
        auto block = block_store->acquire(1);
        if (!block)
        {
            return false;
        }
        // スーパーブロックは構造体より小さいので、残りにゴミが残らないよう
        // ブロック全体を消してから書く。
        std::memset(block.data(), 0, FSBLOCK_SIZE);
        *reinterpret_cast<SuperBlock *>(block.data()) = superblock_state;
        if (!block.write())
        {
            return false;
        }
    }

    // make root inode (inum=1) and add "." and ".." entries.
    DiskInode root{};
    root.type  = InodeType::kDirectory;
    root.nlink = 1;
    root.size  = 0;

    if (!add_root_entry(root, ROOTINO, "."))
    {
        return false;
    }
    if (!add_root_entry(root, ROOTINO, ".."))
    {
        return false;
    }
    if (!FileSystem::detail::write_inode(ROOTINO, root))
    {
        return false;
    }
    console->puts("[FS]   ");
    console->printf("format done, root inode created (size=%u)\n", root.size);
    return true;
}

} // namespace

namespace FileSystem::detail
{

SuperBlock &mutable_superblock()
{
    return superblock_state;
}

// ブロックをゼロで埋めて即座に書き戻す
bool zero_block(uint32_t blockno)
{
    auto block = block_store->acquire(blockno);
    if (!block)
    {
        return false;
    }
    std::memset(block.data(), 0, FSBLOCK_SIZE);
    return block.write();
}

// ビットマップ上で blockno を使用中にする
bool mark_block_used(uint32_t blockno)
{
    auto block = block_store->acquire(FileSystem::bitmap_block(blockno));
    if (!block)
    {
        return false;
    }
    uint32_t bit_index = blockno % BLOCKS_PER_BITMAP_BLOCK;
    block.data()[bit_index / 8] |= static_cast<uint8_t>(1u << (bit_index % 8));
    return block.write();
}

// inode を書き込む
bool write_inode(uint32_t inum, const DiskInode &inode)
{
    auto block = block_store->acquire(FileSystem::inode_block(inum));
    if (!block)
    {
        return false;
    }
    auto *entries                    = reinterpret_cast<DiskInode *>(block.data());
    entries[inum % INODES_PER_BLOCK] = inode;
    return block.write();
}

// inode を読み出す
bool read_inode(uint32_t inum, DiskInode &out)
{
    auto block = block_store->acquire(FileSystem::inode_block(inum));
    if (!block)
    {
        return false;
    }
    const auto *entries = reinterpret_cast<const DiskInode *>(block.data());
    out                 = entries[inum % INODES_PER_BLOCK];
    return true;
}

bool load_superblock()
{
    auto block = block_store->acquire(1);
    if (!block)
    {
        return false;
    }
    superblock_state = *reinterpret_cast<const SuperBlock *>(block.data());
    return superblock_state.magic == FS_MAGIC;
}

} // namespace FileSystem::detail

namespace FileSystem
{

Manager::Manager(uint32_t total_blocks, IBlockStore *store, IConsole *console)
{
    block_store   = (store != nullptr) ? store : &null_block_store;
    IConsole *out = console_or_null_object(console);

    if (detail::load_superblock())
    {
        out->puts("[FS]   ");
        out->printf("already formatted: size=%u ninodes=%u nblocks=%u\n",
                    superblock_state.size,
                    superblock_state.ninodes,
                    superblock_state.nblocks);
        valid_ = true;
        return;
    }

    // if the magic number is not matched, the disk is not formatted yet. format it.
    out->puts("[FS]   not formatted, creating filesystem...\n");

    if (!format(total_blocks, out))
    {
        return; // valid_ は false のまま
    }
    // verify the superblock after formatting
    valid_ = detail::load_superblock();
}


const SuperBlock &superblock()
{
    return superblock_state;
}

uint32_t inode_block(uint32_t inum)
{
    return inum / INODES_PER_BLOCK + superblock_state.inodestart;
}

uint32_t bitmap_block(uint32_t b)
{
    return b / BLOCKS_PER_BITMAP_BLOCK + superblock_state.bmapstart;
}

std::optional<uint32_t> allocate_block()
{
    for (uint32_t base = 0; base < superblock_state.size; base += BLOCKS_PER_BITMAP_BLOCK)
    {
        auto block = block_store->acquire(bitmap_block(base));
        if (!block)
        {
            return std::nullopt;
        }

        for (uint32_t offset = 0; offset < BLOCKS_PER_BITMAP_BLOCK && base + offset < superblock_state.size; offset++)
        {
            uint8_t mask = static_cast<uint8_t>(1u << (offset % 8));
            if (block.data()[offset / 8] & mask)
            {
                continue; // 使用中
            }

            block.data()[offset / 8] |= mask;
            if (!block.write())
            {
                return std::nullopt;
            }
            block.reset(); // zero_block が同じバッファを取れるよう先に手放す

            uint32_t blockno = base + offset;
            if (!detail::zero_block(blockno))
            {
                return std::nullopt;
            }
            return blockno;
        }
    }
    return std::nullopt; // 空きなし
}

void free_block(uint32_t blockno)
{
    auto block = block_store->acquire(bitmap_block(blockno));
    if (!block)
    {
        return;
    }
    uint32_t bit_index = blockno % BLOCKS_PER_BITMAP_BLOCK;
    block.data()[bit_index / 8] &= static_cast<uint8_t>(~(1u << (bit_index % 8)));
    block.write();
}

} // namespace FileSystem
