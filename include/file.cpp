#include "file.hpp"
#include "keyboard.hpp"   // コンソール入力 (既存のキーボードドライバ)
#include "vga.hpp"

namespace
{

constexpr int NFILE = 100;

FileSystem::File file_table[NFILE];

} // namespace

namespace FileSystem
{

FileTableManager::FileTableManager()
{
    for (File &file : file_table)
    {
        file.type     = FileType::kNone;
        file.refcnt   = 0;
        file.readable = false;
        file.writable = false;
        file.offset   = 0;
        file.inode.reset();
    }
    valid_ = true;
}

File *file_allocate()
{
    for (File &file : file_table)
    {
        if (file.refcnt == 0)
        {
            file.refcnt = 1;
            return &file;
        }
    }
    return nullptr; // テーブル枯渇
}

File *file_duplicate(File *file)
{
    if (file == nullptr || file->refcnt <= 0)
    {
        return nullptr;
    }
    file->refcnt++;
    return file;
}

void file_close(File *file)
{
    if (file == nullptr || file->refcnt <= 0)
    {
        return;
    }
    if (--file->refcnt > 0)
    {
        return; // まだ誰かが使っている
    }

    // 最後の参照が消えたのでスロットを空ける
    file->inode.reset();   // InodeRef の RAII が inode を手放す
    file->type     = FileType::kNone;
    file->readable = false;
    file->writable = false;
    file->offset   = 0;
}

int file_read(File *file, uint8_t *buffer, uint32_t n)
{
    if (file == nullptr || !file->readable)
    {
        return -1;
    }

    switch (file->type)
    {
        case FileType::kConsole:
        {
            // キーボードから1行読む (改行まで、または n バイト)
            uint32_t i = 0;
            while (i < n)
            {
                char c = keyboard::getchar(); // 入力があるまで待つ
                buffer[i++] = static_cast<uint8_t>(c);
                if (c == '\n' || c == 0)
                {
                    // stop reading after newline or no more input
                    break;
                }
            }
            return static_cast<int>(i);
        }

        case FileType::kInode:
        {
            int read_bytes = readi(file->inode, buffer, file->offset, n);
            if (read_bytes > 0)
            {
                file->offset += static_cast<uint32_t>(read_bytes);
            }
            return read_bytes;
        }

        default:
            return -1;
    }
}

int file_write(File *file, const uint8_t *buffer, uint32_t n)
{
    if (file == nullptr || !file->writable)
    {
        return -1;
    }

    switch (file->type)
    {
        case FileType::kConsole:
        {
            for (uint32_t i = 0; i < n; i++)
            {
                vga::vga->putchar(static_cast<char>(buffer[i]));
            }
            return static_cast<int>(n);
        }

        case FileType::kInode:
        {
            int written = writei(file->inode, buffer, file->offset, n);
            if (written > 0)
            {
                file->offset += static_cast<uint32_t>(written);
            }
            return written;
        }

        default:
            return -1;
    }
}

File *file_open_console(bool readable, bool writable)
{
    File *file = file_allocate();
    if (file == nullptr)
    {
        return nullptr;
    }
    file->type     = FileType::kConsole;
    file->readable = readable;
    file->writable = writable;
    file->offset   = 0;
    return file;
}

} // namespace FileSystem
