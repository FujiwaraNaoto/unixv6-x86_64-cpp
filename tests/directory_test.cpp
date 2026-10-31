#include "tests.hpp"
#include "directory.hpp"
#include "file_system_internal.hpp"

#include <cstdint>
#include <string_view>

namespace tests
{
namespace
{

bool report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[DIR] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "FAILED");
    return ok;
}

// 後片付け用: name のエントリを空きスロットに戻す (dir_unlink がまだ無いので直接書く)
void clear_entry(Inode &directory, std::string_view name)
{
    if (auto found = FileSystem::dir_lookup(directory, name))
    {
        DirectoryEntry empty{};
        const auto *bytes = reinterpret_cast<const uint8_t *>(&empty);
        (void)FileSystem::write_data(directory, bytes, found->offset, sizeof(empty));
    }
}

// 1 ブロック (32 エントリ) を超えるだけの数。"f00" 〜 "f39" の名前を付ける。
constexpr int kManyEntries = 40;

void set_numbered_name(char (&name)[4], int number)
{
    name[0] = 'f';
    name[1] = static_cast<char>('0' + number / 10);
    name[2] = static_cast<char>('0' + number % 10);
    name[3] = '\0';
}

} // namespace

bool directory_basic(IConsole *console)
{
    InodeRef root = FileSystem::get_inode(ROOT_INODE);
    if (!root)
        return report(console, "get root", false);

    bool all = true;

    // . と .. は format() が書いたもの
    auto dot    = FileSystem::dir_lookup(*root.get(), ".");
    auto dotdot = FileSystem::dir_lookup(*root.get(), "..");
    all &= report(console,
                  "lookup . and ..",
                  dot && dot->inode_num == ROOT_INODE && dot->offset == 0 && dotdot &&
                      dotdot->inode_num == ROOT_INODE && dotdot->offset == sizeof(DirectoryEntry));

    all &= report(console, "lookup missing", !FileSystem::dir_lookup(*root.get(), "nothing"));

    InodeRef file = FileSystem::allocate_inode(InodeType::kFile);
    if (!file)
        return report(console, "allocate file", false);
    const uint32_t inum = file->inode_num;

    // ディレクトリでない inode には使えない
    all &= report(console,
                  "reject non-directory",
                  !FileSystem::dir_lookup(*file.get(), "hello") && !FileSystem::dir_link(*file.get(), "hello", inum));

    bool ok    = FileSystem::dir_link(*root.get(), "hello", inum);
    auto found = FileSystem::dir_lookup(*root.get(), "hello");
    all &= report(console, "link then lookup", ok && found && found->inode_num == inum);

    // メモリ上の inode を捨てて、ディスクから読み直しても見つかるか
    root.reset();
    root = FileSystem::get_inode(ROOT_INODE);
    if (!root)
        return report(console, "get root again", false);
    auto reloaded = FileSystem::dir_lookup(*root.get(), "hello");
    all &= report(console, "lookup after reload", reloaded && reloaded->inode_num == inum);

    all &= report(console, "reject duplicate", !FileSystem::dir_link(*root.get(), "hello", inum));
    all &= report(console,
                  "reject empty / 15 chars / slash / inum 0 / inum over 16 bit",
                  !FileSystem::dir_link(*root.get(), "", inum) &&
                      !FileSystem::dir_link(*root.get(), "abcdefghijklmno", inum) &&
                      !FileSystem::dir_link(*root.get(), "a/b", inum) &&
                      !FileSystem::dir_link(*root.get(), "zero", 0) &&
                      !FileSystem::dir_link(*root.get(), "big", 0x10000));

    // ちょうど 14 文字は有効 (null 終端されない)。前方一致の名前とは区別される
    ok = FileSystem::dir_link(*root.get(), "abcdefghijklmn", inum);
    all &= report(console,
                  "14-char name",
                  ok && FileSystem::dir_lookup(*root.get(), "abcdefghijklmn") &&
                      !FileSystem::dir_lookup(*root.get(), "abcdefghijklm"));

    // 空きスロットの再利用: hello を消してから足すと、同じ offset に入る
    const uint32_t hello_offset = found ? found->offset : 0;
    clear_entry(*root.get(), "hello");
    all &= report(console, "lookup after clear", !FileSystem::dir_lookup(*root.get(), "hello"));
    ok         = FileSystem::dir_link(*root.get(), "again", inum);
    auto again = FileSystem::dir_lookup(*root.get(), "again");
    all &= report(console, "reuse free slot", ok && found && again && again->offset == hello_offset);

    // 1 ブロックを超えて足せるか
    char name[4];
    ok = true;
    for (int i = 0; i < kManyEntries && ok; i++)
    {
        set_numbered_name(name, i);
        ok = FileSystem::dir_link(*root.get(), name, inum);
    }
    set_numbered_name(name, kManyEntries - 1);
    all &= report(console,
                  "grow past one block",
                  ok && root->disk.size > FSBLOCK_SIZE && root->disk.addrs[1] != 0 &&
                      FileSystem::dir_lookup(*root.get(), name));

    // 後片付け: エントリと inode を空きに戻す (fs.img は次回も使われる)。
    // ディレクトリが伸びた分のブロックは縮める手段がまだ無いので残るが、次回は空きスロットとして再利用される。
    for (int i = 0; i < kManyEntries; i++)
    {
        set_numbered_name(name, i);
        clear_entry(*root.get(), name);
    }
    clear_entry(*root.get(), "again");
    clear_entry(*root.get(), "abcdefghijklmn");
    FileSystem::internal::write_inode(inum, DiskInode{});
    return all;
}

} // namespace tests
