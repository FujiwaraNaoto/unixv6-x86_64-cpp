#include "path.hpp"
#include "directory.hpp"

namespace
{

// path から次の要素を切り出し、残りの位置を返す。
// 要素が無ければ nullptr。名前が長すぎる場合も nullptr (切り詰めない)。
//
// 切り詰めると "verylongname_a" と "verylongname_b" が同じ名前になり、
// 別のファイルを開いてしまう。エラーにするほうが安全。
const char *next_element(const char *path, FileName &out, bool &too_long)
{
    too_long = false;

    while (*path == '/')
    {
        path++;
    }
    if (*path == '\0')
    {
        return nullptr;
    }

    const char *start = path;
    while (*path != '\0' && *path != '/')
    {
        path++;
    }

    auto length = static_cast<size_t>(path - start);
    if (length > static_cast<size_t>(DIRECTORY_NAME_SIZE))
    {
        too_long = true;
        return nullptr;
    }

    out = FileName{start, length};
    return path;
}

// namei と nameiparent の共通実装。
// parent が true なら最後の要素を辿らず、その1つ手前で止める。
FileSystem::InodeRef namex(const char *path, bool parent, FileName *name_out)
{
    if (path == nullptr)
    {
        return {};
    }

    // カレントディレクトリがまだ無いので、相対パスもルート起点で解決する。
    // プロセス層ができたら (path[0] == '/') で分岐する。
    FileSystem::InodeRef current = FileSystem::iget(ROOTINO);
    if (!current)
    {
        return {};
    }

    FileName element;
    bool too_long = false;
    const char *rest = next_element(path, element, too_long);
    if (too_long)
    {
        return {};
    }

    while (rest != nullptr)
    {
        if (current->type != InodeType::kDirectory)
        {
            return {}; // 途中の要素がディレクトリでない
        }

        // 次の要素があるか先読みする。無ければ element が最後の要素。
        FileName lookahead;
        const char *after = next_element(rest, lookahead, too_long);
        if (too_long)
        {
            return {};
        }

        if (parent && after == nullptr)
        {
            if (name_out != nullptr)
            {
                *name_out = element;
            }
            return current; // 最後の要素は辿らず、親を返す
        }

        uint32_t next = FileSystem::dirlookup(current, element, nullptr);
        if (next == 0)
        {
            return {};
        }
        current = FileSystem::iget(next);
        if (!current)
        {
            return {};
        }

        element = lookahead;
        rest    = after;
    }

    if (parent)
    {
        return {}; // "/" に対する nameiparent は親が存在しない
    }
    return current;
}

} // namespace

namespace FileSystem
{

InodeRef namei(const char *path)
{
    return namex(path, false, nullptr);
}

InodeRef nameiparent(const char *path, FileName &name_out)
{
    return namex(path, true, &name_out);
}

} // namespace FileSystem
