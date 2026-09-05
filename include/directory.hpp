#ifndef DIRECTORY_HPP
#define DIRECTORY_HPP
#include <cstdint>
#include "file_system.hpp"
#include "inode.hpp"

// ─── ディレクトリ層 ──────────────────────────────────────────────
// ディレクトリは「16バイトのエントリが並んだだけのファイル」なので、
// この層は readi / writei の上に薄く乗るだけになる。

namespace FileSystem
{

// メモリ上で扱うディレクトリエントリ。
// ディスク側 (DiskDirEntry) は長さフィールドを持たない固定16バイトだが、
// こちらは kstring を使って比較や代入を素直に書けるようにする。
struct DirEntry
{
    uint16_t inum;
    FileName name;
};

// ディスク上の並び ←→ メモリ上の型。
// to_memory() は長さ上限付きで読むので、ディスクの内容が壊れていても
// name が DIRECTORY_NAME_SIZE 文字を超えることはない。
DirEntry to_memory(const DiskDirEntry &entry);
DiskDirEntry to_disk(const DirEntry &entry);

// dir から name を探す。見つかれば inode 番号、無ければ 0。
// offset_out が非 nullptr なら、そのエントリのファイル内オフセットを返す。
uint32_t dirlookup(const InodeRef &dir, const FileName &name, uint32_t *offset_out);

// dir に name → inum のエントリを追加する。
// 既に同名があれば false。空きエントリを再利用し、無ければ末尾に追記する。
// nlink はここでは触らない (呼び出し側が管理する)。
bool dirlink(const InodeRef &dir, const FileName &name, uint32_t inum);

// dir から name のエントリを消す (inum を 0 にする)。
// nlink はここでは触らない。
bool dirunlink(const InodeRef &dir, const FileName &name);

// "." と ".." 以外のエントリが無いか。rmdir の判定に使う。
bool dir_is_empty(const InodeRef &dir);

} // namespace FileSystem

#endif // DIRECTORY_HPP
