#include "file.hpp"
#include "console.hpp"
#include "inode.hpp"
#include "keyboard.hpp"
#include "asm/irqflags.hpp"
#include "inode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{
// FileDescriptor の実体の置き場所。FileIO は offset を持つので open ごとに 1 スロット使う
std::array<FileSystem::detail::FileSlot, FileSystem::NUM_FILES> file_table{};
// 標準入出力はプロセスごとの状態を持たないので、実体は 1 つずつ (Linux の /dev/console と同じく共有する)。
// ここで 1 つ参照を持ち続けるので、全プロセスが閉じても破棄されない
FileRef standard_input;
FileRef standard_output;
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

} // namespace

void FileRef::reset()
{
    if (slot_ == nullptr)
        return;
    slot_->refcnt--;
    if (slot_->refcnt == 0)
    {
        // 最後の close。仮想デストラクタで派生クラス (FileIO なら InodeRef) を片付けてスロットを空ける
        slot_->object->~FileDescriptor();
        slot_->object = nullptr;
    }
    slot_ = nullptr;
}


ssize_t StandardInput::read(void *buffer, size_t len)
{
    return console_read(reinterpret_cast<uint8_t *>(buffer), static_cast<uint32_t>(len));
}

ssize_t StandardInput::write(const void *, size_t)
{
    return kError; // 入力専用
}

ssize_t StandardOutput::read(void *, size_t)
{
    return kError; // 出力専用
}

ssize_t StandardOutput::write(const void *buffer, size_t len)
{
    IConsole *console = (file_console != nullptr) ? file_console : null_console;
    console->write(reinterpret_cast<const char *>(buffer), len);
    return static_cast<ssize_t>(len);
}

// 現在の offset から読み書きして offset を進める。ファイル末尾を読むと 0。
ssize_t FileIO::read(void *buffer, size_t len)
{
    if (!readable)
        return kError;
    const ssize_t done =
        FileSystem::read_data(*inoderef.get(), reinterpret_cast<uint8_t *>(buffer), offset, static_cast<uint32_t>(len));
    if (done < 0)
        return kError;
    offset += static_cast<uint32_t>(done);
    return done;
}

ssize_t FileIO::write(const void *buffer, size_t len)
{
    if (!writable)
        return kError;
    const ssize_t done = FileSystem::write_data(*inoderef.get(),
                                                reinterpret_cast<const uint8_t *>(buffer),
                                                offset,
                                                static_cast<uint32_t>(len));
    if (done < 0)
        return kError;
    offset += static_cast<uint32_t>(done);
    return done;
}

namespace FileSystem
{

namespace detail
{
FileSlot *find_free_slot()
{
    for (FileSlot &slot : file_table)
        if (slot.refcnt == 0)
            return &slot;
    return nullptr;
}
} // namespace detail

void file_set_console(IConsole *console)
{
    file_console = console;
}

FileRef open_standard_input()
{
    if (!standard_input)
        standard_input = make_file<StandardInput>();
    return standard_input;
}

FileRef open_standard_output()
{
    if (!standard_output)
        standard_output = make_file<StandardOutput>();
    return standard_output;
}

FileRef open_file(InodeRef inode, bool readable, bool writable)
{
    if (!inode)
        return FileRef{};
    return make_file<FileIO>(std::move(inode), readable, writable);
}

} // namespace FileSystem
