#include "inode.hpp"
#include "file_system.hpp"
#include "file_system_internal.hpp"
#include <array>
#include <cstdint>
namespace tests
{

namespace
{

bool report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[INODE] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "FAILED");
    return ok;
}

bool root_directory(IConsole *console)
{
    InodeRef root = FileSystem::get_inode(ROOT_INODE);
    const bool ok = root && root->disk.type == InodeType::kDirectory;

    console->printf("[INODE]:first_inode_is_root_directory");

    return ok;
}

// 同じ番号を 2 回取ると同じスロットを共有し、スコープを抜けると refcnt が戻るか
bool same_inode_is_shared(IConsole *console)
{
    Inode *slot = nullptr;
    bool ok     = true;
    {
        InodeRef a = FileSystem::get_inode(ROOT_INODE);
        InodeRef b = FileSystem::get_inode(ROOT_INODE);
        ok         = a && b && a.get() == b.get() && a->refcnt == 2;
        slot       = a.get();
    } // ここで b → a の順に put_inode される
    ok = ok && slot != nullptr && slot->refcnt == 0;
    return report(console, "shared slot and refcnt", ok);
}


bool allocate_writes_type(IConsole *console)
{
    InodeRef file = FileSystem::allocate_inode(InodeType::kFile);
    bool ok       = file && file->inode_num >= 2;

    uint32_t inum = ok ? file->inode_num : 0;
    if (ok)
    {
        // メモリ上のコピーではなく、ディスクから読み直して確かめる
        auto disk = FileSystem::internal::read_inode(inum);
        ok        = disk && disk->type == InodeType::kFile;
    }

    // 後片付け: テストを何度走らせても inode を食い潰さないよう、空きに戻す
    if (inum != 0)
        FileSystem::internal::write_inode(inum, DiskInode{});

    return report(console, "allocate_inode(kFile)", ok);
}

// 7KB = 14 ブロック。直接ブロック 12 個を超えて、間接ブロックに 2 個入る。
constexpr uint32_t kFileSize = 7 * 1024;
static_assert(kFileSize > NDIRECT * FSBLOCK_SIZE);

// カーネルスタックに載せるには大きいので静的に置く
std::array<uint8_t, kFileSize> source_data;
std::array<uint8_t, kFileSize> read_buffer;

// ブロック番号も混ぜて、ブロックごとに違う内容にする (別のブロックを読むと一致しない)
uint8_t pattern(uint32_t pos)
{
    return static_cast<uint8_t>(pos * 31 + pos / FSBLOCK_SIZE);
}

// read_buffer の先頭 count バイトが、ファイルの offset からの内容と一致するか
bool read_buffer_matches(uint32_t offset, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (read_buffer[i] != pattern(offset + i))
            return false;
    }
    return true;
}

// 後片付け: テストを何度走らせてもディスクを食い潰さないよう、ブロックと inode を空きに戻す
void release_file(uint32_t inum)
{
    auto disk = FileSystem::internal::read_inode(inum);
    if (!disk)
        return;

    if (disk->addrs[NDIRECT] != 0)
    {
        auto block = FileSystem::internal::fs.block_store->acquire(disk->addrs[NDIRECT]);
        if (!block)
            return;
        const auto *table = reinterpret_cast<const uint32_t *>(block.data());
        for (int i = 0; i < NINDIRECT; i++)
        {
            if (table[i] != 0)
                FileSystem::free_block(table[i]);
        }
    }
    for (uint32_t address : disk->addrs)
    {
        if (address != 0)
            FileSystem::free_block(address);
    }
    FileSystem::internal::write_inode(inum, DiskInode{});
}

// 間接ブロックに入る大きさを書き、ディスクから読み直しても同じ内容が返るか
bool write_and_read_back(IConsole *console)
{
    InodeRef file = FileSystem::allocate_inode(InodeType::kFile);
    if (!file)
        return report(console, "write/read across indirect block", false);
    const uint32_t inum = file->inode_num;

    for (uint32_t i = 0; i < kFileSize; i++)
        source_data[i] = pattern(i);

    // ブロック境界でないところで分けて書く (2 回目は末尾への追記)
    constexpr uint32_t kFirstPart = 1000;
    auto first                    = FileSystem::write_data(*file.get(), source_data.data(), 0, kFirstPart);
    auto second =
        FileSystem::write_data(*file.get(), source_data.data() + kFirstPart, kFirstPart, kFileSize - kFirstPart);
    bool ok = first && *first == kFirstPart && second && *second == kFileSize - kFirstPart;

    // メモリ上のコピーを捨てて、size と addrs もディスクから読み直す
    file.reset();
    file = FileSystem::get_inode(inum);
    ok   = ok && file && file->disk.size == kFileSize && file->disk.addrs[NDIRECT] != 0;

    if (ok)
    {
        // 全体
        auto all = FileSystem::read_data(*file.get(), read_buffer.data(), 0, kFileSize);
        ok       = all && *all == kFileSize && read_buffer_matches(0, kFileSize);

        // 直接ブロックと間接ブロックの境界をまたぐ読み込み
        constexpr uint32_t kBoundary = NDIRECT * FSBLOCK_SIZE;
        auto across                  = FileSystem::read_data(*file.get(), read_buffer.data(), kBoundary - 100, 200);
        ok                           = ok && across && *across == 200 && read_buffer_matches(kBoundary - 100, 200);

        // 末尾で切り詰められる
        auto tail = FileSystem::read_data(*file.get(), read_buffer.data(), kFileSize - 10, 100);
        ok        = ok && tail && *tail == 10 && read_buffer_matches(kFileSize - 10, 10);

        // 末尾より先は失敗
        ok = ok && !FileSystem::read_data(*file.get(), read_buffer.data(), kFileSize + 1, 1);
    }

    file.reset();
    release_file(inum);
    return report(console, "write/read across indirect block", ok);
}

// 読み込みはブロックを確保しない。未割当のブロックは失敗し、addrs も変わらないか
bool read_does_not_allocate(IConsole *console)
{
    InodeRef file = FileSystem::allocate_inode(InodeType::kFile);
    if (!file)
        return report(console, "read does not allocate", false);
    const uint32_t inum = file->inode_num;

    // ブロックを持たないまま、メモリ上の size だけ伸ばす (ディスクには書かない)
    file->disk.size = (NDIRECT + 1) * FSBLOCK_SIZE;

    bool ok = !FileSystem::read_data(*file.get(), read_buffer.data(), 0, 1);
    ok      = ok && !FileSystem::read_data(*file.get(), read_buffer.data(), NDIRECT * FSBLOCK_SIZE, 1);
    ok      = ok && file->disk.addrs[0] == 0 && file->disk.addrs[NDIRECT] == 0;

    file.reset();
    release_file(inum);
    return report(console, "read does not allocate", ok);
}


} // namespace

bool inode_basic(IConsole *console)
{
    bool ok = true;
    ok &= root_directory(console);
    ok &= same_inode_is_shared(console);
    ok &= allocate_writes_type(console);
    ok &= write_and_read_back(console);
    ok &= read_does_not_allocate(console);
    return ok;
}

} // namespace tests
