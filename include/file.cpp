#include "file.hpp"
#include "keyboard.hpp"

#include <array>

namespace
{

std::array<File, FileSystem::NUM_FILES> file_table{};
IConsole *file_console = nullptr;

uint32_t console_read(uint8_t *dest, uint32_t count)
{
    uint32_t done = 0;
    while (done < count)
    {
        while (!keyboard::has_input())
        {
            // syscall は FMASK で IF=0 (割り込み禁止) で入るため、ここで sti して
            // キーボード割り込みを受けられるようにする。sti の直後に hlt を置くと
            // sti から1命令ぶん割り込みが遅延する x86 の仕様により、check と hlt の
            // 間に来た入力も取りこぼさずに wake できる (lost-wakeup 回避)。
            asm volatile("sti; hlt");
        }
        char c = keyboard::getchar();
        if (c == 0)
            break; // no more input
        dest[done] = static_cast<uint8_t>(c);
        done++;
        if (c == '\n')
            break; // stop reading after newline
    }
    return done;
}

} // namespace

namespace FileSystem
{

void file_set_console(IConsole *console)
{
    file_console = console;
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
    return nullptr;
}

File *file_duplicate(File *file)
{
    if (file != nullptr)
        file->refcnt++;
    return file;
}

void file_close(File *file)
{
    if (file == nullptr || file->refcnt <= 0)
        return;
    file->refcnt--;
    if (file->refcnt > 0)
        return; // まだ誰かが使っている

    file->inode.reset();
    file->type     = FileType::kNone;
    file->readable = false;
    file->writable = false;
    file->offset   = 0;
}

File *file_open_console(bool readable, bool writable)
{
    File *file = file_allocate();
    if (file == nullptr)
        return nullptr;
    file->type     = FileType::kConsole;
    file->readable = readable;
    file->writable = writable;
    return file;
}

std::optional<uint32_t> file_read(File &file, uint8_t *dest, uint32_t count)
{
    if (!file.readable)
        return std::nullopt;

    switch (file.type)
    {
        case FileType::kConsole:
            return console_read(dest, count);
        case FileType::kInode:
        {
            auto done = read_data(*file.inode.get(), dest, file.offset, count);
            if (done)
                file.offset += *done;
            return done;
        }
        case FileType::kNone:
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> file_write(File &file, const uint8_t *source, uint32_t count)
{
    if (!file.writable)
        return std::nullopt;

    switch (file.type)
    {
        case FileType::kConsole:
        {
            IConsole *console = (file_console != nullptr) ? file_console : null_console;
            console->write(reinterpret_cast<const char *>(source), count);
            return count;
        }
        case FileType::kInode:
        {
            auto done = write_data(*file.inode.get(), source, file.offset, count);
            if (done)
                file.offset += *done;
            return done;
        }
        case FileType::kNone:
            break;
    }
    return std::nullopt;
}

} // namespace FileSystem
