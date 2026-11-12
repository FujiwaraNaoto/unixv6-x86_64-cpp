#include "file.hpp"
#include "console.hpp"
#include "inode.hpp"
#include "keyboard.hpp"
#include "asm/irqflags.hpp"
#include "inode.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace
{
std::array<StandardIO,FileSystem::NUM_FILES> console_table{};
std::array<File, FileSystem::NUM_FILES> file_table{}; // 実体
IConsole *file_console = nullptr;

//deprecated
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
            irq_enable_and_wait();
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

template <class T, size_t N>
T *find_free(std::array<T, N> &table)
{
    for (T &f : table)
        if (!f.in_use())
            return &f;
    return nullptr;
}


} // namespace


size_t  StandardIO::read(void* buffer,size_t len){
    if(readable){
        return console_read(reinterpret_cast<uint8_t*>(buffer),len);
    }
    return -1;
}

size_t  StandardIO::write(void* buffer,size_t len){
    
    if(writable){
    IConsole *console = (file_console != nullptr) ? file_console : null_console;
    console->write(reinterpret_cast<const char *>(buffer), len);
    return len;
    }
    return -1;
}

void  StandardIO::release(){
    io_console = nullptr;
    return;
}


size_t  FileIO::read(void* buffer,size_t len){
    auto res = FileSystem::read_data(*inoderef.get(), reinterpret_cast<uint8_t*>(buffer), offset, len);
    return res.value_or(-1);
}

size_t  FileIO::write(void* buffer,size_t len){
    auto res = FileSystem::write_data(*inoderef.get(), reinterpret_cast<uint8_t*>(buffer), offset, len);
    return res.value_or(-1);
}

void  FileIO::release(){
    inoderef.reset();
    offset = 0;
    return;
}


namespace FileSystem
{

StandardIO *open_console(bool readable,bool writable)
{
    StandardIO* f = find_free(console_table);
    if(f==nullptr) return nullptr;

    *f = StandardIO(file_console,readable,writable);
    return f;
}

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

// //deprecated
// File *file_open_console(bool readable, bool writable)
// {
//     File *file = file_allocate();
//     if (file == nullptr)
//         return nullptr;
//     file->type     = FileType::kConsole;
//     file->readable = readable;
//     file->writable = writable;
//     return file;
// }

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
