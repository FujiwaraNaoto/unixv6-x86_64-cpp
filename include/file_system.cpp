#include "file_system.hpp"
#include "buffer_cache.hpp"
#include "console.hpp"

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
    for (uint32_t i = 0; i < BLOCK_SIZE; i++)
    {
        block->data[i] = 0;
    }
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
    uint32_t bit_index = blockno % BPB;
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
    auto *entries       = reinterpret_cast<DiskInode *>(block->data);
    entries[inum % IPB] = inode;
    return block.write();
}

// ルートディレクトリにエントリを追加する。
// フォーマット直後専用の簡易版で、ディレクトリが1ブロックに収まることを前提とする。
bool add_root_entry(DiskInode &root, uint32_t inum, const char *name)
{
    if (root.addrs[0] == 0)
    {
        root.addrs[0] = FileSystem::allocate_block();
        if (root.addrs[0] == 0)
        {
            return false;
        }
    }

    auto block = BufferCache::acquire(root.addrs[0]);
    if (!block)
    {
        return false;
    }

    auto *entries = reinterpret_cast<DirEntry *>(block->data);
    int capacity  = BLOCK_SIZE / sizeof(DirEntry);

    for (int i = 0; i < capacity; i++)
    {
        if (entries[i].inum != 0)
        {
            continue; // 使用中
        }

        entries[i].inum = static_cast<uint16_t>(inum);
        int j           = 0;
        for (; j < DIRSIZ && name[j] != '\0'; j++)
        {
            entries[i].name[j] = name[j];
        }
        for (; j < DIRSIZ; j++)
        {
            entries[i].name[j] = '\0';
        }

        if (!block.write())
        {
            return false;
        }
        root.size += sizeof(DirEntry);
        return true;
    }
    return false; // 空きエントリなし
}


// スーパーブロックを読み込む。magic が一致しなければ false。
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

    uint32_t inode_blocks  = (NINODES + IPB - 1) / IPB;
    uint32_t bitmap_blocks = (total_blocks + BPB - 1) / BPB;

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

    // ① inode 領域とビットマップをゼロクリア
    for (uint32_t b = superblock_state.inodestart; b < data_start; b++)
    {
        if (!zero_block(b))
        {
            return false;
        }
    }

    // ② メタデータ領域 (0 〜 data_start-1) を使用済みにする
    for (uint32_t b = 0; b < data_start; b++)
    {
        if (!mark_block_used(b))
        {
            return false;
        }
    }

    // ③ スーパーブロックを書く
    {
        auto block = BufferCache::acquire(1);
        if (!block)
        {
            return false;
        }
        for (uint32_t i = 0; i < BLOCK_SIZE; i++)
        {
            block->data[i] = 0;
        }
        *reinterpret_cast<SuperBlock *>(block->data) = superblock_state;
        if (!block.write())
        {
            return false;
        }
    }

    // ④ ルートディレクトリ (inum = ROOTINO) を作る
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

    // magic が一致しない → 初回起動とみなしてフォーマット
    out->puts("[FS]   not formatted, creating filesystem...\n");

    if (!format(total_blocks, out))
    {
        return; // valid_ は false のまま
    }
    // 書いた内容を読み返して検証する
    valid_ = load_superblock();
}

const SuperBlock &superblock()
{
    return superblock_state;
}

uint32_t inode_block(uint32_t inum)
{
    return inum / IPB + superblock_state.inodestart;
}

uint32_t bitmap_block(uint32_t b)
{
    return b / BPB + superblock_state.bmapstart;
}

uint32_t allocate_block()
{
    for (uint32_t base = 0; base < superblock_state.size; base += BPB)
    {
        auto block = BufferCache::acquire(bitmap_block(base));
        if (!block)
        {
            return 0;
        }

        for (uint32_t offset = 0; offset < BPB && base + offset < superblock_state.size; offset++)
        {
            uint8_t mask = static_cast<uint8_t>(1u << (offset % 8));
            if (block->data[offset / 8] & mask)
            {
                continue; // 使用中
            }

            block->data[offset / 8] |= mask;
            if (!block.write())
            {
                return 0;
            }
            block.reset(); // zero_block が同じバッファを取れるよう先に手放す

            uint32_t blockno = base + offset;
            if (!zero_block(blockno))
            {
                return 0;
            }
            return blockno;
        }
    }
    return 0; // 空きなし
}

void free_block(uint32_t blockno)
{
    auto block = BufferCache::acquire(bitmap_block(blockno));
    if (!block)
    {
        return;
    }
    uint32_t bit_index = blockno % BPB;
    block->data[bit_index / 8] &= static_cast<uint8_t>(~(1u << (bit_index % 8)));
    block.write();
}

} // namespace FileSystem
