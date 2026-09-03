#include "file_system.hpp"
#include "buffer_cache.hpp"
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

// ブロックをゼロで埋めて即座に書き戻す
bool zero_block(uint32_t blockno)
{
    auto block = BufferCache::acquire(blockno);
    if (!block)
    {
        return false;
    }
    std::memset(block->data, 0, BLOCK_SIZE);
    return block.write();
}

// ビットマップ上で blockno を使用中にする
bool mark_block_used(uint32_t blockno)
{
    auto block = BufferCache::acquire(FileSystem::bitmap_block(blockno));
    if (!block)
    {
        return false;
    }
    uint32_t bit_index = blockno % BLOCKS_PER_BITMAP_BLOCK;
    block->data[bit_index / 8] |= static_cast<uint8_t>(1u << (bit_index % 8));
    return block.write();
}

// inode を書き込む
bool write_inode(uint32_t inum, const DiskInode &inode)
{
    auto block = BufferCache::acquire(FileSystem::inode_block(inum));
    if (!block)
    {
        return false;
    }
    auto *entries                    = reinterpret_cast<DiskInode *>(block->data);
    entries[inum % INODES_PER_BLOCK] = inode;
    return block.write();
}

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

    auto block = BufferCache::acquire(root.addrs[0]);
    if (!block)
    {
        return false;
    }

    auto *entries = reinterpret_cast<DiskDirEntry *>(block->data);
    int capacity  = BLOCK_SIZE / sizeof(DiskDirEntry);

    for (int i = 0; i < capacity; i++)
    {
        if (entries[i].inum != 0)
        {
            continue; // 使用中
        }

        entries[i] = FileSystem::to_disk(DirEntry{static_cast<uint16_t>(inum), name});

        if (!block.write())
        {
            return false;
        }
        root.size += sizeof(DiskDirEntry);
        return true;
    }
    return false; // 空きエントリなし
}


bool load_superblock()
{
    auto block = BufferCache::acquire(1);
    if (!block)
    {
        return false;
    }
    superblock_state = *reinterpret_cast<const SuperBlock *>(block->data);
    return superblock_state.magic == FS_MAGIC;
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
        if (!zero_block(b))
        {
            return false;
        }
    }

    // make meta blocks used (0〜data_start-1)
    for (uint32_t b = 0; b < data_start; b++)
    {
        if (!mark_block_used(b))
        {
            return false;
        }
    }

    // write superblock to block 1. 0 is boot block, so skip it.
    {
        auto block = BufferCache::acquire(1);
        if (!block)
        {
            return false;
        }
        // スーパーブロックは構造体より小さいので、残りにゴミが残らないよう
        // ブロック全体を消してから書く。
        std::memset(block->data, 0, BLOCK_SIZE);
        *reinterpret_cast<SuperBlock *>(block->data) = superblock_state;
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
    if (!write_inode(ROOTINO, root))
    {
        return false;
    }
    console->puts("[FS]   ");
    console->printf("format done, root inode created (size=%u)\n", root.size);
    return true;
}

} // namespace

namespace FileSystem
{

Manager::Manager(uint32_t total_blocks, IConsole *console)
{
    IConsole *out = console_or_null_object(console);

    if (load_superblock())
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
    valid_ = load_superblock();
}


DirEntry to_memory(const DiskDirEntry &entry)
{
    // name は DIRECTORY_NAME_SIZE 文字ちょうどのとき NUL 終端されないので、
    // 長さ上限を渡して読む。これにより壊れたイメージを読んでも
    // FileName の長さが容量を超えることはない。
    return DirEntry{entry.inum, FileName{entry.name, DIRECTORY_NAME_SIZE}};
}

DiskDirEntry to_disk(const DirEntry &entry)
{
    DiskDirEntry disk{};
    disk.inum = entry.inum;
    // 余った領域はゼロ埋めされる (未初期化のバイトをディスクに書かない)
    entry.name.copy_to(disk.name, DIRECTORY_NAME_SIZE);
    return disk;
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
        auto block = BufferCache::acquire(bitmap_block(base));
        if (!block)
        {
            return std::nullopt;
        }

        for (uint32_t offset = 0; offset < BLOCKS_PER_BITMAP_BLOCK && base + offset < superblock_state.size; offset++)
        {
            uint8_t mask = static_cast<uint8_t>(1u << (offset % 8));
            if (block->data[offset / 8] & mask)
            {
                continue; // 使用中
            }

            block->data[offset / 8] |= mask;
            if (!block.write())
            {
                return std::nullopt;
            }
            block.reset(); // zero_block が同じバッファを取れるよう先に手放す

            uint32_t blockno = base + offset;
            if (!zero_block(blockno))
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
    auto block = BufferCache::acquire(bitmap_block(blockno));
    if (!block)
    {
        return;
    }
    uint32_t bit_index = blockno % BLOCKS_PER_BITMAP_BLOCK;
    block->data[bit_index / 8] &= static_cast<uint8_t>(~(1u << (bit_index % 8)));
    block.write();
}

} // namespace FileSystem
