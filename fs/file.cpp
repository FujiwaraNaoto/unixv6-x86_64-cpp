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
// 実体のプール。new が無いので静的に置き、refcnt == 0 のスロットを空きとして使う
std::array<StandardInput, FileSystem::NUM_FILES> standard_input_table{};
std::array<StandardOutput, FileSystem::NUM_FILES> standard_output_table{};
std::array<FileIO, FileSystem::NUM_FILES> file_io_table{};
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


size_t StandardInput::read(void* buffer,size_t len){
    return console_read(reinterpret_cast<uint8_t*>(buffer),static_cast<uint32_t>(len));
}

size_t StandardInput::write(const void*,size_t){
    return kError; // 入力専用
}

void StandardInput::release(){
}

size_t StandardOutput::read(void*,size_t){
    return kError; // 出力専用
}

size_t StandardOutput::write(const void* buffer,size_t len){
    IConsole *console = (io_console != nullptr) ? io_console : null_console;
    console->write(reinterpret_cast<const char *>(buffer), len);
    return len;
}

void StandardOutput::release(){
    io_console = nullptr;
}

// 現在の offset から読み書きして offset を進める。ファイル末尾を読むと 0。
size_t FileIO::read(void* buffer,size_t len){
    if(!readable) return kError;
    auto done = FileSystem::read_data(*inoderef.get(), reinterpret_cast<uint8_t*>(buffer), offset, static_cast<uint32_t>(len));
    if(!done) return kError;
    offset += *done;
    return *done;
}

size_t FileIO::write(const void* buffer,size_t len){
    if(!writable) return kError;
    auto done = FileSystem::write_data(*inoderef.get(), reinterpret_cast<const uint8_t*>(buffer), offset, static_cast<uint32_t>(len));
    if(!done) return kError;
    offset += *done;
    return *done;
}

void FileIO::release(){
    inoderef.reset();
    offset   = 0;
    readable = false;
    writable = false;
}


namespace FileSystem
{

void file_set_console(IConsole *console)
{
    file_console = console;
}

FileDescriptor *open_standard_input()
{
    StandardInput *f = find_free(standard_input_table);
    if (f == nullptr)
        return nullptr;
    f->open();
    return f;
}

FileDescriptor *open_standard_output()
{
    StandardOutput *f = find_free(standard_output_table);
    if (f == nullptr)
        return nullptr;
    f->open(file_console);
    return f;
}

FileDescriptor *open_file(InodeRef inode, bool readable, bool writable)
{
    if (!inode)
        return nullptr;
    FileIO *f = find_free(file_io_table);
    if (f == nullptr)
        return nullptr;
    f->open(std::move(inode), readable, writable);
    return f;
}

} // namespace FileSystem
