#ifndef PATH_HPP
#define PATH_HPP

#include "inode.hpp"

#include <optional>
#include <string_view>

namespace FileSystem
{

// path が指す inode を返す。見つからなければ空の InodeRef。
//   "/"       → ルート
//   "/a/b"    → a の中の b
//   "//a//b/" → "/a/b" と同じ (連続した / や末尾の / は無視する)
// カレントディレクトリがまだ無いので、"/" で始まらないパスもルートから解決する。
InodeRef resolve_path(std::string_view path);

struct ParentLookup
{
    InodeRef parent;       // 最後の要素を含むディレクトリ
    std::string_view name; // 最後の要素の名前 (path の一部を指す。path より長生きさせないこと)
};

// path の最後の要素の、1 つ手前のディレクトリを返す。最後の要素は存在しなくてよい。
//   "/a/b" → parent = a, name = "b"
// 最後の要素が無い場合 ("/" や "") と、手前がディレクトリでない場合は std::nullopt。
// NOTE: name が名前として使えるかは調べない (directory_link が弾く)。
std::optional<ParentLookup> resolve_parent(std::string_view path);

} // namespace FileSystem

#endif // PATH_HPP
