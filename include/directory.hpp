#ifndef DIRECTORY_HPP
#define DIRECTORY_HPP

#include "inode.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace FileSystem
{

struct DirectoryLookup
{
    uint32_t inode_num; // 見つかったエントリが指す inode 番号
    uint32_t offset;    // そのエントリの、ディレクトリ内でのバイト位置
};

// ディレクトリ directory の中から name を探す。
// 見つからない、directory がディレクトリでない、読み込みに失敗した場合は std::nullopt。
std::optional<DirectoryLookup> directory_lookup(Inode &directory, std::string_view name);

// ディレクトリ directory に (name → inode_num) のエントリを追加する。
// 空きスロット (inode_number == 0) があれば再利用し、無ければ末尾に足す。
// 失敗するのは、name が空・DIRECTORY_NAME_SIZE 超え・'/' を含む、inode_num が 0 か 16 bit に収まらない、
// 同名が既にある、directory がディレクトリでない、読み書きに失敗した場合。
// NOTE: inode_num 側の nlink は変更しない (呼び出し側の責任)。
[[nodiscard]] bool directory_link(Inode &directory, std::string_view name, uint32_t inode_num);

} // namespace FileSystem

#endif // DIRECTORY_HPP
