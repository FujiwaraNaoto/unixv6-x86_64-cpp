#include "inode.hpp"
#include "file_system.hpp"
#include "file_system_internal.hpp"
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


} // namespace

bool inode_basic(IConsole *console)
{
    bool ok = true;
    ok &= root_directory(console);
    ok &= same_inode_is_shared(console);
    ok &= allocate_writes_type(console);
    return ok;
}

} // namespace tests
