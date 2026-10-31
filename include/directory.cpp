#include "directory.hpp"
#include "file_system.hpp"
#include "inode.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

namespace
{

// 名前として使えるか。長すぎる名前は切り詰めず弾く (切り詰めると別のファイルを指す事故の元になる)。
// '/' はパスの区切りなので、名前に入れるとパス解決で辿れなくなる。
bool is_valid_name(std::string_view name)
{
    if (name.empty() || name.size() > DIRECTORY_NAME_SIZE)
        return false;
    for (char c : name)
    {
        if (c == '/')
            return false;
    }
    return true;
}

// ディスク上の名前 (短ければ null 終端) と name が一致するか。
// DIRECTORY_NAME_SIZE ちょうどの名前は null 終端されないので、比較は最大 DIRECTORY_NAME_SIZE バイトで打ち切る。
bool is_name_equal(const char (&entry_name)[DIRECTORY_NAME_SIZE], std::string_view name)
{
    if (name.size() > DIRECTORY_NAME_SIZE)
        return false;
    for (size_t i = 0; i < name.size(); i++)
    {
        if (entry_name[i] != name[i])
            return false;
    }
    return name.size() == DIRECTORY_NAME_SIZE || entry_name[name.size()] == '\0';
}

// offset の位置にあるエントリを 1 つ読む。
bool read_entry(Inode &directory, uint32_t offset, DirectoryEntry &entry)
{
    auto n = FileSystem::read_data(directory, reinterpret_cast<uint8_t *>(&entry), offset, sizeof(DirectoryEntry));
    return n && *n == sizeof(DirectoryEntry);
}

} // namespace

namespace FileSystem
{

std::optional<DirectoryLookup> dir_lookup(Inode &directory, std::string_view name)
{
    if (directory.disk.type != InodeType::kDirectory || !is_valid_name(name))
        return std::nullopt;

    DirectoryEntry entry{};
    for (uint32_t offset = 0; offset + sizeof(DirectoryEntry) <= directory.disk.size; offset += sizeof(DirectoryEntry))
    {
        if (!read_entry(directory, offset, entry))
            return std::nullopt;
        if (entry.inode_number == 0) // 空きスロット
            continue;
        if (is_name_equal(entry.name, name))
        {
            return DirectoryLookup{
                .inode_num = entry.inode_number,
                .offset    = offset,
            };
        }
    }
    return std::nullopt;
}

bool dir_link(Inode &directory, std::string_view name, uint32_t inode_num)
{
    if (directory.disk.type != InodeType::kDirectory || !is_valid_name(name))
        return false;
    // 0 は「空きスロット」の印なので指せない。DirectoryEntry::inode_number は 16 bit。
    if (inode_num == 0 || inode_num > std::numeric_limits<uint16_t>::max())
        return false;

    // 1 回の走査で「同名が無いか」の確認と「最初の空きスロット」探しを両方やる。
    std::optional<uint32_t> free_offset;
    DirectoryEntry entry{};
    for (uint32_t offset = 0; offset + sizeof(DirectoryEntry) <= directory.disk.size; offset += sizeof(DirectoryEntry))
    {
        if (!read_entry(directory, offset, entry))
            return false;
        if (entry.inode_number == 0)
        {
            if (!free_offset)
                free_offset = offset;
            continue;
        }
        if (is_name_equal(entry.name, name))
            return false; // 同名が既にある
    }

    // 空きが無ければ末尾に足す (write_data がブロック確保と size の更新をする)
    const uint32_t offset = free_offset ? *free_offset : directory.disk.size;

    DirectoryEntry fresh{}; // name は 0 埋めされるので、短い名前は自動で null 終端になる
    fresh.inode_number = static_cast<uint16_t>(inode_num);
    std::memcpy(fresh.name, name.data(), name.size());

    auto n = write_data(directory, reinterpret_cast<const uint8_t *>(&fresh), offset, sizeof(DirectoryEntry));
    return n && *n == sizeof(DirectoryEntry);
}

} // namespace FileSystem
