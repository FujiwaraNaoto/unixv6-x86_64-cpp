#ifndef PATH_HPP
#define PATH_HPP
#include "file_system.hpp"
#include "inode.hpp"

// ─── パス解決層 ──────────────────────────────────────────────────
// "/a/b/c" のような文字列を inode へ変換する。
// カレントディレクトリはプロセス層がまだ無いので、当面は常にルート起点。

namespace FileSystem
{

// パス全体を解決して inode を返す。見つからなければ空の InodeRef。
InodeRef namei(const char *path);

// 最後の要素の1つ手前 (親ディレクトリ) までを解決し、最後の要素名を返す。
// create / unlink のように「親と名前」が欲しい場面で使う。
//   "/a/b/c" → 親 = "/a/b" の inode, name_out = "c"
InodeRef nameiparent(const char *path, FileName &name_out);

} // namespace FileSystem

#endif // PATH_HPP
