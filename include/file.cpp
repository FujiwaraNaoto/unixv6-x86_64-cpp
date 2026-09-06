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
            // キーボードから1行読む (改行まで、または n バイト)。
            //
            // keyboard::getchar() は非ブロッキングで、入力が無ければ 0 を返す。
            // read(2) の意味に合わせるにはここで待つ必要があるので、バッファが
            // 空の間は hlt して IRQ1 (キーボード) が来るまで CPU を止める。
            // syscall 経由で来た場合 FMASK が IF を落としているため、hlt の前に
            // sti で割り込みを開け直す (でないと二度と起きられない)。
            //
            // NOTE: 本来はプロセスを sleep させて他のプロセスに CPU を渡すべき。
            //       キーボード側に wakeup を仕込むまでは、この待ち方で代用する。
            uint32_t i = 0;
            while (i < n)
            {
                const char c = keyboard::getchar();
                if (c == 0)
                {
                    asm volatile("sti; hlt");
                    continue; // まだ入力が無い
                }

                if (c == '\b')
                {
                    // 行編集はここで完結させ、消した文字は呼び出し側に渡さない
                    if (i > 0)
                    {
                        i--;
                        vga::vga->putchar('\b'); // VGA 側が消去まで面倒を見る
                    }
                    continue;
                }

                vga::vga->putchar(c); // 打った文字をその場で見せる (エコー)
                buffer[i++] = static_cast<uint8_t>(c);
                if (c == '\n')
                {
                    break; // 行が完成した
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
