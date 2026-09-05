#include "directory.hpp"

namespace
{

constexpr uint32_t ENTRY_SIZE = sizeof(DiskDirEntry);

} // namespace

namespace FileSystem
{

DirEntry to_memory(const DiskDirEntry &entry)
{
    // name は DIRECTORY_NAME_SIZE 文字ちょうどのとき NUL 終端されないので、
    // 長さ上限を渡して読む。これにより壊れたイメージを読んでも
    // FileName の長さが容量を超えることはない。
    return DirEntry{entry.inum, FileName{entry.name, DIRECTORY_NAME_SIZE}};
}

DiskDirEntry to_disk(const DirEntry &entry)
{
    DiskDirEntry disk{};
    disk.inum = entry.inum;
    // 余った領域はゼロ埋めされる (未初期化のバイトをディスクに書かない)
    entry.name.copy_to(disk.name, DIRECTORY_NAME_SIZE);
    return disk;
}

uint32_t dirlookup(const InodeRef &dir, const FileName &name, uint32_t *offset_out)
{
    if (!dir || dir->type != InodeType::kDirectory)
    {
        return 0;
    }

    for (uint32_t off = 0; off + ENTRY_SIZE <= dir->size; off += ENTRY_SIZE)
    {
        DiskDirEntry raw{};
        if (readi(dir, reinterpret_cast<uint8_t *>(&raw), off, ENTRY_SIZE) != ENTRY_SIZE)
        {
            return 0;
        }
        if (raw.inum == 0)
        {
            continue; // 空きエントリ
        }
        // kstring は const char* とだけ比較できるので c_str() を挟む
        if (to_memory(raw).name == name.c_str())
        {
            if (offset_out != nullptr)
            {
                *offset_out = off;
            }
            return raw.inum;
        }
    }
    return 0;
}

bool dirlink(const InodeRef &dir, const FileName &name, uint32_t inum)
{
    if (!dir || dir->type != InodeType::kDirectory || inum == 0 || name.empty())
    {
        return false;
    }
    if (dirlookup(dir, name, nullptr) != 0)
    {
        return false; // 同名が既にある
    }

    // 空きエントリを探す。無ければ末尾に追記する。
    uint32_t target = dir->size;
    for (uint32_t off = 0; off + ENTRY_SIZE <= dir->size; off += ENTRY_SIZE)
    {
        DiskDirEntry raw{};
        if (readi(dir, reinterpret_cast<uint8_t *>(&raw), off, ENTRY_SIZE) != ENTRY_SIZE)
        {
            return false;
        }
        if (raw.inum == 0)
        {
            target = off;
            break;
        }
    }

    DiskDirEntry fresh = to_disk(DirEntry{static_cast<uint16_t>(inum), name});
    return writei(dir, reinterpret_cast<const uint8_t *>(&fresh), target, ENTRY_SIZE) == ENTRY_SIZE;
}

bool dirunlink(const InodeRef &dir, const FileName &name)
{
    uint32_t off = 0;
    if (dirlookup(dir, name, &off) == 0)
    {
        return false;
    }

    // inum を 0 にするだけ。ディレクトリは縮めない (穴として再利用する)。
    DiskDirEntry empty{};
    return writei(dir, reinterpret_cast<const uint8_t *>(&empty), off, ENTRY_SIZE) == ENTRY_SIZE;
}

bool dir_is_empty(const InodeRef &dir)
{
    if (!dir || dir->type != InodeType::kDirectory)
    {
        return false;
    }

    for (uint32_t off = 0; off + ENTRY_SIZE <= dir->size; off += ENTRY_SIZE)
    {
        DiskDirEntry raw{};
        if (readi(dir, reinterpret_cast<uint8_t *>(&raw), off, ENTRY_SIZE) != ENTRY_SIZE)
        {
            return false;
        }
        if (raw.inum == 0)
        {
            continue;
        }
        FileName name = to_memory(raw).name;
        if (name == "." || name == "..")
        {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace FileSystem
