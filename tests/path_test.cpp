#include "tests.hpp"
#include "directory.hpp"
#include "file_system_internal.hpp"
#include "path.hpp"

#include <cstddef>
#include <string_view>

namespace tests
{
namespace
{

bool report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[PATH] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "FAILED");
    return ok;
}

// std::string_view の == は memcmp を呼ぶが、カーネルには実体が無い
bool same_name(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

// path が inode_number を指しているか
bool resolves_to(std::string_view path, InodeNumber inode_number)
{
    InodeRef ref = FileSystem::resolve_path(path);
    return ref && ref->inode_number == inode_number;
}

// resolve_parent(path) の結果が parent_number と name か
bool parent_is(std::string_view path, InodeNumber parent_number, std::string_view name)
{
    auto found = FileSystem::resolve_parent(path);
    return found && found->parent->inode_number == parent_number && same_name(found->name, name);
}

// 準備: /pt_dir (ディレクトリ) と /pt_dir/file (ファイル) を作る
bool create_test_tree(Inode &root, Inode &directory, InodeNumber file_number)
{
    return FileSystem::directory_link(directory, ".", directory.inode_number) &&
           FileSystem::directory_link(directory, "..", ROOT_INODE) &&
           FileSystem::directory_link(root, "pt_dir", directory.inode_number) &&
           FileSystem::directory_link(directory, "file", file_number);
}

// 後片付け: エントリとブロックと inode を空きに戻す (fs.img は次回も使われる)
void remove_test_tree(Inode &root, const Inode &directory, InodeNumber file_number)
{
    if (auto found = FileSystem::directory_lookup(root, "pt_dir"))
    {
        DirectoryEntry empty{};
        const auto *bytes = reinterpret_cast<const uint8_t *>(&empty);
        (void)FileSystem::write_data(root, bytes, found->offset, sizeof(empty));
    }
    for (BlockNumber block_number : directory.disk.addrs)
    {
        if (block_number.value != 0)
            FileSystem::free_block(block_number);
    }
    FileSystem::internal::write_inode(directory.inode_number, DiskInode{});
    FileSystem::internal::write_inode(file_number, DiskInode{});
}

} // namespace

bool path_basic(IConsole *console)
{
    InodeRef root      = FileSystem::get_inode(ROOT_INODE);
    InodeRef directory = FileSystem::allocate_inode(InodeType::kDirectory);
    InodeRef file      = FileSystem::allocate_inode(InodeType::kFile);
    if (!root || !directory || !file)
        return report(console, "setup", false);
    const InodeNumber directory_number = directory->inode_number;
    const InodeNumber file_number      = file->inode_number;

    bool all = report(console, "setup", create_test_tree(*root.get(), *directory.get(), file_number));

    all &= report(console, "/ is root", resolves_to("/", ROOT_INODE) && resolves_to("///", ROOT_INODE));
    all &= report(console, "/pt_dir", resolves_to("/pt_dir", directory_number));
    all &= report(console, "/pt_dir/file", resolves_to("/pt_dir/file", file_number));
    all &= report(console, "//pt_dir//file/", resolves_to("//pt_dir//file/", file_number));
    all &= report(console, "relative = from root", resolves_to("pt_dir/file", file_number));
    all &= report(console, "/pt_dir/.. is root", resolves_to("/pt_dir/..", ROOT_INODE));
    all &= report(console,
                  "missing / empty / through file / too long",
                  !FileSystem::resolve_path("/pt_dir/nothing") && !FileSystem::resolve_path("") &&
                      !FileSystem::resolve_path("/pt_dir/file/x") && !FileSystem::resolve_path("/abcdefghijklmno"));

    all &= report(console, "parent of /pt_dir/newname", parent_is("/pt_dir/newname", directory_number, "newname"));
    all &= report(console, "parent of /top/", parent_is("/top/", ROOT_INODE, "top"));
    all &= report(console,
                  "parent of / and empty fails",
                  !FileSystem::resolve_parent("/") && !FileSystem::resolve_parent(""));
    all &= report(console, "parent through missing fails", !FileSystem::resolve_parent("/nothing/x"));
    all &= report(console, "parent through file fails", !FileSystem::resolve_parent("/pt_dir/file/x"));

    // 参照を返し忘れていないか: テーブルより多く解決しても枯渇しない
    bool ok = true;
    for (int i = 0; i < FileSystem::NUM_INODE * 2 && ok; i++)
        ok = resolves_to("/pt_dir/file", file_number);
    all &= report(console, "no inode leak", ok);

    remove_test_tree(*root.get(), *directory.get(), file_number);
    return all;
}

} // namespace tests
