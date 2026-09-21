#include <cstdint>
#include <cstring>
#include "tests.hpp"
#include "directory.hpp"
#include "inode.hpp"

namespace tests
{
namespace
{

void report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[DIR]   ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "NG");
}

// path を引いて、inum と一致するかだけを見る (参照は手放す)
bool resolves_to(const char *path, uint32_t inum)
{
    FileSystem::Inode *ip = FileSystem::resolve_path(path);
    const bool ok         = ip != nullptr && ip->inum == inum;
    FileSystem::put_inode(ip);
    return ok;
}

bool is_freed(uint32_t inum)
{
    FileSystem::Inode *ip = FileSystem::get_inode(inum);
    const bool freed      = ip != nullptr && ip->disk.type == InodeType::kUnused;
    FileSystem::put_inode(ip);
    return freed;
}

} // namespace

// ディレクトリ: 名前の検索 / 登録 / 削除とパス解決を確認する。
// 最後に作ったものを全て消すので、同じ fs.img で何度走らせても同じ結果になる。
void directory_lookup_link(IConsole *console)
{
    using namespace FileSystem;

    Inode *root = get_inode(ROOT_INODE);
    if (root == nullptr)
    {
        report(console, "get_inode(ROOT)", false);
        return;
    }

    // ─── format() が作ったルート ───
    Inode *dot     = lookup_directory(root, ".");
    Inode *dot_dot = lookup_directory(root, "..");
    report(console, "root has . and ..",
           dot == root && dot_dot == root && root->disk.nlink == 2);
    put_inode(dot);
    put_inode(dot_dot);

    // ─── ファイルを 1 つ登録する ───
    const auto file_inum = allocate_inode(InodeType::kFile);
    const bool linked    = file_inum && link_directory(root, "hello", *file_inum);
    report(console, "link_directory file", linked);
    if (!linked)
    {
        put_inode(root);
        return;
    }
    report(console, "resolve_path /hello", resolves_to("/hello", *file_inum));

    Inode *file = get_inode(*file_inum);
    report(console, "nlink counts the name", file != nullptr && file->disk.nlink == 1);
    put_inode(file);

    // ─── 失敗すべきもの ───
    report(console, "reject duplicate name", !link_directory(root, "hello", *file_inum));
    report(console, "reject too long name", !link_directory(root, "fifteen_chars__", *file_inum));
    report(console, "reject name with /", !link_directory(root, "a/b", *file_inum));
    Inode *missing = lookup_directory(root, "nothere");
    report(console, "missing name is nullptr", missing == nullptr && FileSystem::resolve_path("/nothere") == nullptr);
    report(console, "file is not a directory", FileSystem::resolve_path("/hello/x") == nullptr);

    // ─── サブディレクトリ ───
    const auto dir_inum = allocate_inode(InodeType::kDirectory);
    Inode *dir          = dir_inum ? get_inode(*dir_inum) : nullptr;
    const bool dir_ok   = dir != nullptr && initialize_directory(dir, ROOT_INODE) &&
                        link_directory(root, "dir", *dir_inum);
    report(console, "make sub directory", dir_ok);
    report(console, "dir nlink = 2 (. + name)", dir != nullptr && dir->disk.nlink == 2);
    report(console, "root nlink = 3 (+ dir/..)", root->disk.nlink == 3);

    const auto inner_inum = allocate_inode(InodeType::kFile);
    const bool inner_ok   = dir != nullptr && inner_inum && link_directory(dir, "file", *inner_inum);
    report(console, "resolve_path /dir/file", inner_ok && resolves_to("/dir/file", *inner_inum));
    report(console, "resolve_path with // and ..", resolves_to("//dir///../hello", *file_inum));
    report(console, "resolve_path / is root", resolves_to("/", ROOT_INODE));

    char name[NAME_BUFFER_SIZE];
    Inode *parent = resolve_parent("/dir/file", name);
    report(console, "resolve_parent /dir/file",
           parent != nullptr && dir != nullptr && parent->inum == dir->inum && std::memcmp(name, "file", 5) == 0);
    put_inode(parent);
    report(console, "resolve_parent / is nullptr", resolve_parent("/", name) == nullptr);

    // ─── 後始末 (unlink_directory) ───
    report(console, "reject unlink of non-empty dir", !unlink_directory(root, "dir"));
    report(console, "reject unlink of . and ..", !unlink_directory(root, ".") && !unlink_directory(root, ".."));

    const bool removed = unlink_directory(dir, "file") && unlink_directory(root, "dir") &&
                         unlink_directory(root, "hello");
    put_inode(dir); // ここで dir の最後の参照が消え、nlink 0 なので解放される
    report(console, "unlink_directory all", removed && FileSystem::resolve_path("/hello") == nullptr);
    report(console, "root nlink back to 2", root->disk.nlink == 2);
    report(console, "unlinked inodes are freed",
           is_freed(*file_inum) && is_freed(*dir_inum) && inner_inum && is_freed(*inner_inum));

    put_inode(root);
}

} // namespace tests
