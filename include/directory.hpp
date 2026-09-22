#ifndef DIRECTORY_HPP
#define DIRECTORY_HPP
#include <cstdint>
#include "inode.hpp"

// ディレクトリは「DirectoryEntry (名前 → inode 番号) の並び」を中身に持つファイル。
// ここの関数は全て inode 層 (read_inode / write_inode) の上に載っていて、
// ディスクのブロックを直接は触らない。
//
// nlink は「この inode を指しているディレクトリエントリの数」をそのまま数える。
// link_directory() が +1、unlink_directory() が -1 する。
// ディレクトリ自身の "." も 1 つのエントリなので数に入る
// (空のディレクトリは "." と親からの名前で nlink = 2。UNIX と同じ)。
namespace FileSystem
{

// ディレクトリの名前 1 つ分のバッファ長 (DIRSIZ 文字 + 終端)
constexpr int NAME_BUFFER_SIZE = DIRSIZ + 1;

// ディレクトリ dp の中から name を探し、見つかった inode を返す (参照数 +1)。
// offset_out を渡すと、見つかったエントリのディレクトリ内オフセットを書き込む。
// 見つからない / dp がディレクトリでない場合は nullptr。
Inode *lookup_directory(Inode *dp, const char *name, uint32_t *offset_out = nullptr);

// ディレクトリ dp に「name → inum」のエントリを追加し、inum の nlink を 1 増やす。
// 同じ名前が既にある、名前が空・長すぎる (DIRSIZ 超)・'/' を含む場合は false。
bool link_directory(Inode *dp, const char *name, uint32_t inum);

// ディレクトリ dp から name のエントリを消し、その inode の nlink を 1 減らす。
// 対象がディレクトリなら空 ("." と ".." だけ) のときに限り消せる。
// その場合は "." の分と、親 (dp) が子の ".." から指されていた分も減らす。
// "." と ".." そのものは消せない。
bool unlink_directory(Inode *dp, const char *name);

// 新しく作ったディレクトリ dir に "." (自分) と ".." (parent_inum) を書く。
// ルートは親が自分自身なので parent_inum = ROOT_INODE を渡す。
bool initialize_directory(Inode *dir, uint32_t parent_inum);

// パスをルートから 1 段ずつたどり、指している inode を返す (参照数 +1)。
// "/a/b"、"a/b" (今はカレントディレクトリが無いので、どちらもルートから)、
// 連続した "//" も受け付ける。見つからなければ nullptr。
Inode *resolve_path(const char *path);

// パスの親ディレクトリの inode を返し (参照数 +1)、末尾の名前を name_out に書く。
// name_out は NAME_BUFFER_SIZE バイト以上あること。
// 例: "/dir/file" → dir の inode、name_out = "file"。
// "/" のように末尾の名前が無いパスは nullptr。
Inode *resolve_parent(const char *path, char *name_out);

} // namespace FileSystem

#endif // DIRECTORY_HPP
