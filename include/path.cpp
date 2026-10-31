#include "path.hpp"
#include "directory.hpp"

#include <cstddef>
#include <utility>

// NOTE: std::string_view の find() は memchr を呼ぶがカーネルには実体が無いので、1 文字ずつ見る。
//       substr() も、範囲外のときに例外を投げる経路があるので使わない (-fno-exceptions)。

namespace
{

// rest の先頭から要素を 1 つ切り出して返し、rest をその後ろまで進める。
// 前の '/' は読み飛ばす。要素が無ければ空の string_view を返す。
//   rest = "//a//b/" → "a" を返し、rest = "//b/"
std::string_view next_component(std::string_view &rest)
{
    size_t begin = 0;
    while (begin < rest.size() && rest[begin] == '/')
        begin++;
    size_t end = begin;
    while (end < rest.size() && rest[end] != '/')
        end++;
    std::string_view component(rest.data() + begin, end - begin);
    rest.remove_prefix(end);
    return component;
}

// rest にまだ要素が残っているか ('/' だけなら残っていない)
bool has_component(std::string_view rest)
{
    for (char c : rest)
    {
        if (c != '/')
            return true;
    }
    return false;
}

// ルートから path を辿る。stop_before_last なら最後の要素の手前で止まり、その名前を last に入れる。
// resolve_path と resolve_parent の共通部分 (xv6 の namex 相当)。
InodeRef walk(std::string_view path, bool stop_before_last, std::string_view *last)
{
    if (path.empty())
        return InodeRef{};

    InodeRef current      = FileSystem::get_inode(ROOT_INODE);
    std::string_view rest = path;

    while (current)
    {
        std::string_view name = next_component(rest);
        if (name.empty())
            break; // 要素を使い切った

        if (stop_before_last && !has_component(rest))
        {
            // 最後の要素は探さないので、directory_lookup の代わりにここで確かめる
            if (current->disk.type != InodeType::kDirectory)
                return InodeRef{};
            *last = name;
            return current;
        }

        // current がディレクトリでなければ directory_lookup が失敗する
        auto found = FileSystem::directory_lookup(*current.get(), name);
        if (!found)
            return InodeRef{};

        // 次を取ってから current を手放す (ムーブ代入で current の put_inode が走る)
        current = FileSystem::get_inode(found->inode_number);
    }

    if (stop_before_last)
        return InodeRef{}; // "/" のように最後の要素が無かった
    return current;
}

} // namespace

namespace FileSystem
{

InodeRef resolve_path(std::string_view path)
{
    return walk(path, false, nullptr);
}

std::optional<ParentLookup> resolve_parent(std::string_view path)
{
    std::string_view name;
    InodeRef parent = walk(path, true, &name);
    if (!parent)
        return std::nullopt;
    return ParentLookup{.parent = std::move(parent), .name = name};
}

} // namespace FileSystem
