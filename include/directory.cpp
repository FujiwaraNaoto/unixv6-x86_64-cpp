#include "directory.hpp"
#include <cstring>

namespace
{
using FileSystem::Inode;

constexpr uint32_t ENTRY_SIZE = sizeof(DirectoryEntry);

// DirectoryEntry::name は DIRSIZ 文字ちょうどのとき終端が無いので、
// DIRSIZ 文字までで比べる。
bool names_equal(const char *entry_name, const char *name)
{
    for (int i = 0; i < DIRSIZ; i++)
    {
        if (entry_name[i] != name[i])
        {
            return false;
        }
        if (name[i] == '\0')
        {
            return true;
        }
    }
    return name[DIRSIZ] == '\0'; // 14 文字ちょうどで一致し、name もそこで終わっている
}

bool is_dot_or_dot_dot(const char *name)
{
    return std::strlen(name) <= 2 && name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

// ディレクトリエントリとして登録できる名前か
bool valid_name(const char *name)
{
    if (name == nullptr || name[0] == '\0')
    {
        return false;
    }
    for (int i = 0; name[i] != '\0'; i++)
    {
        if (i >= DIRSIZ || name[i] == '/')
        {
            return false; // 長すぎる / 区切り文字を含む
        }
    }
    return true;
}

bool is_directory(const Inode *ip)
{
    return ip != nullptr && ip->valid && ip->disk.type == InodeType::kDirectory;
}

std::optional<DirectoryEntry> read_entry(Inode *dp, uint32_t offset)
{
    DirectoryEntry entry{};
    const auto read = FileSystem::read_inode(dp, &entry, offset, ENTRY_SIZE);
    if (!read || *read != ENTRY_SIZE)
    {
        return std::nullopt;
    }
    return entry;
}

// "." と ".." 以外のエントリが無いか
bool directory_is_empty(Inode *dp)
{
    for (uint32_t offset = 0; offset < dp->disk.size; offset += ENTRY_SIZE)
    {
        const auto entry = read_entry(dp, offset);
        if (!entry)
        {
            return false; // 読めない: 安全側に倒して「空ではない」とする
        }
        if (entry->inum != 0 && !is_dot_or_dot_dot(entry->name))
        {
            return false;
        }
    }
    return true;
}

void decrement_nlink(Inode *ip, uint16_t count)
{
    ip->disk.nlink = (ip->disk.nlink > count) ? static_cast<uint16_t>(ip->disk.nlink - count) : 0;
}

enum class Element
{
    kNone,    // もう名前が無い
    kFound,   // 1 つ取り出した
    kTooLong, // DIRSIZ を超える名前
};

// path の先頭から名前を 1 つ取り出して name に書き、path を次の名前の位置へ進める。
// 前後の連続した '/' は読み飛ばす。
//   "a/bb/c" → name = "a",  path = "bb/c"
//   "///a"   → name = "a",  path = ""
//   ""       → kNone
Element next_element(const char *&path, char *name)
{
    while (*path == '/')
    {
        path++;
    }
    if (*path == '\0')
    {
        return Element::kNone;
    }

    const char *start = path;
    while (*path != '/' && *path != '\0')
    {
        path++;
    }
    const auto length = static_cast<size_t>(path - start);
    if (length > static_cast<size_t>(DIRSIZ))
    {
        return Element::kTooLong;
    }
    std::memcpy(name, start, length);
    name[length] = '\0';

    while (*path == '/')
    {
        path++;
    }
    return Element::kFound;
}

// resolve_path / resolve_parent の共通部分。
// want_parent = true なら、最後の名前の手前 (親ディレクトリ) で止まる。
Inode *walk(const char *path, bool want_parent, char *name)
{
    if (path == nullptr)
    {
        return nullptr;
    }

    Inode *ip = FileSystem::get_inode(ROOT_INODE);
    if (ip == nullptr)
    {
        return nullptr;
    }

    while (true)
    {
        const Element element = next_element(path, name);
        if (element == Element::kNone)
        {
            break;
        }
        if (element == Element::kTooLong || !is_directory(ip))
        {
            FileSystem::put_inode(ip);
            return nullptr;
        }
        if (want_parent && *path == '\0')
        {
            return ip; // name に最後の名前が入った状態で、親を返す
        }

        Inode *next = FileSystem::lookup_directory(ip, name);
        FileSystem::put_inode(ip);
        if (next == nullptr)
        {
            return nullptr;
        }
        ip = next;
    }

    if (want_parent)
    {
        FileSystem::put_inode(ip); // "/" のように最後の名前が無い
        return nullptr;
    }
    return ip;
}

} // namespace

namespace FileSystem
{

Inode *lookup_directory(Inode *dp, const char *name, uint32_t *offset_out)
{
    if (!is_directory(dp) || name == nullptr)
    {
        return nullptr;
    }

    for (uint32_t offset = 0; offset < dp->disk.size; offset += ENTRY_SIZE)
    {
        const auto entry = read_entry(dp, offset);
        if (!entry)
        {
            return nullptr;
        }
        if (entry->inum == 0 || !names_equal(entry->name, name))
        {
            continue;
        }
        if (offset_out != nullptr)
        {
            *offset_out = offset;
        }
        return get_inode(entry->inum);
    }
    return nullptr;
}

bool link_directory(Inode *dp, const char *name, uint32_t inum)
{
    if (!is_directory(dp) || !valid_name(name))
    {
        return false;
    }

    // 同じ名前は 2 つ作らない
    if (Inode *existing = lookup_directory(dp, name))
    {
        put_inode(existing);
        return false;
    }

    // 先に対象の inode を取っておく。取れないなら、エントリも書かない
    Inode *target = get_inode(inum);
    if (target == nullptr)
    {
        return false;
    }

    // 空きエントリ (inum == 0) を再利用し、無ければ末尾に追加する
    uint32_t offset = dp->disk.size;
    for (uint32_t candidate = 0; candidate < dp->disk.size; candidate += ENTRY_SIZE)
    {
        const auto entry = read_entry(dp, candidate);
        if (entry && entry->inum == 0)
        {
            offset = candidate;
            break;
        }
    }

    DirectoryEntry entry{};
    entry.inum = static_cast<uint16_t>(inum);
    std::strncpy(entry.name, name, DIRSIZ); // 残りは '\0' で埋まる

    const auto written = write_inode(dp, &entry, offset, ENTRY_SIZE);
    if (!written || *written != ENTRY_SIZE)
    {
        put_inode(target);
        return false;
    }

    target->disk.nlink++;
    const bool updated = update_inode(target);
    put_inode(target);
    return updated;
}

bool unlink_directory(Inode *dp, const char *name)
{
    if (!is_directory(dp) || name == nullptr || is_dot_or_dot_dot(name))
    {
        return false;
    }

    uint32_t offset = 0;
    Inode *ip       = lookup_directory(dp, name, &offset);
    if (ip == nullptr)
    {
        return false;
    }

    const bool target_is_directory = is_directory(ip);
    if (target_is_directory && !directory_is_empty(ip))
    {
        put_inode(ip);
        return false;
    }

    const DirectoryEntry empty{};
    const auto written = write_inode(dp, &empty, offset, ENTRY_SIZE);
    if (!written || *written != ENTRY_SIZE)
    {
        put_inode(ip);
        return false;
    }

    if (target_is_directory)
    {
        // 親からの名前と、自分の "." の 2 つ分
        decrement_nlink(ip, 2);
        // 子の ".." が親を指していた分
        decrement_nlink(dp, 1);
        update_inode(dp);
    }
    else
    {
        decrement_nlink(ip, 1);
    }
    update_inode(ip);
    put_inode(ip); // nlink と ref が両方 0 になれば、ここで中身ごと解放される
    return true;
}

bool initialize_directory(Inode *dir, uint32_t parent_inum)
{
    if (!is_directory(dir))
    {
        return false;
    }
    return link_directory(dir, ".", dir->inum) && link_directory(dir, "..", parent_inum);
}

Inode *resolve_path(const char *path)
{
    char name[NAME_BUFFER_SIZE];
    return walk(path, false, name);
}

Inode *resolve_parent(const char *path, char *name_out)
{
    if (name_out == nullptr)
    {
        return nullptr;
    }
    return walk(path, true, name_out);
}

} // namespace FileSystem
