#include "tests.hpp"
#include "file.hpp"
#include "file_system_internal.hpp"
#include "process.hpp"
#include "systemcall.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace tests
{
namespace
{

// スレッド関数は EntryPoint (void (*)()) として create_process に渡すので引数を
// 取れない。結果はグローバル変数で受け渡す。
volatile bool console_fds_ok   = false;
volatile bool fork_shares_ok   = false;
volatile bool child_shares_ok  = false;
volatile bool wait_restores_ok = false;
volatile bool syscall_fds_ok   = false;
volatile bool child_close_ok   = false;
volatile bool close_ok         = false;

bool report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[FILE] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "FAILED");
    return ok;
}

// 空きスロットの数を数える (いったん全部確保して返す)。満杯で nullptr が返ることの確認も兼ねる。
int count_free_slots()
{
    std::array<File *, FileSystem::NUM_FILES + 1> taken{};
    int count = 0;
    while (count <= FileSystem::NUM_FILES)
    {
        taken[count] = FileSystem::file_allocate();
        if (taken[count] == nullptr)
            break;
        count++;
    }
    for (int i = 0; i < count; i++)
        FileSystem::file_close(taken[i]);
    return count;
}

bool allocate_dup_close(IConsole *console)
{
    File *file = FileSystem::file_allocate();
    bool ok    = file != nullptr && file->refcnt == 1;
    if (ok)
    {
        ok = FileSystem::file_duplicate(file) == file && file->refcnt == 2;
        FileSystem::file_close(file);
        ok = ok && file->refcnt == 1;
        FileSystem::file_close(file);
        ok = ok && file->refcnt == 0 && file->type == FileType::kNone;
    }
    return report(console, "allocate / dup / close", ok);
}

bool table_can_be_exhausted(IConsole *console)
{
    const int free_slots = count_free_slots();
    // 全部返したので、もう一度数えても同じ。満杯なら NUM_FILES を超えない
    return report(console,
                  "exhausted table returns nullptr",
                  free_slots > 0 && free_slots <= FileSystem::NUM_FILES && count_free_slots() == free_slots);
}

// 1 ブロックをまたぐ大きさ
constexpr uint32_t kDataSize = 700;
std::array<uint8_t, kDataSize> source_data;
std::array<uint8_t, kDataSize> read_buffer;

bool read_buffer_matches(uint32_t offset, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (read_buffer[i] != source_data[offset + i])
            return false;
    }
    return true;
}

File *open_inode_file(InodeNumber inode_number, bool readable, bool writable)
{
    File *file = FileSystem::file_allocate();
    if (file == nullptr)
        return nullptr;
    file->type     = FileType::kInode;
    file->readable = readable;
    file->writable = writable;
    file->inode    = FileSystem::get_inode(inode_number);
    return file;
}

bool inode_file(IConsole *console)
{
    InodeRef inode = FileSystem::allocate_inode(InodeType::kFile);
    if (!inode)
        return report(console, "inode file", false);
    const InodeNumber inode_number = inode->inode_number;

    for (uint32_t i = 0; i < kDataSize; i++)
        source_data[i] = static_cast<uint8_t>(i * 31 + i / FSBLOCK_SIZE);

    File *writer = open_inode_file(inode_number, false, true);
    File *reader = open_inode_file(inode_number, true, false);
    if (writer == nullptr || reader == nullptr)
        return report(console, "inode file", false);

    // 2 回に分けて書くと、offset が進んで続きに書かれる
    auto first  = FileSystem::file_write(*writer, source_data.data(), 100);
    auto second = FileSystem::file_write(*writer, source_data.data() + 100, kDataSize - 100);
    bool ok     = first && *first == 100 && second && *second == kDataSize - 100 && writer->offset == kDataSize;
    report(console, "write advances offset", ok);

    // reader は writer とは別の File なので offset は 0 から
    auto all    = FileSystem::file_read(*reader, read_buffer.data(), kDataSize);
    bool all_ok = report(console,
                         "separate File has own offset",
                         all && *all == kDataSize && read_buffer_matches(0, kDataSize) && reader->offset == kDataSize);
    ok          = ok && all_ok;

    auto end = FileSystem::file_read(*reader, read_buffer.data(), 1);
    ok &= report(console, "read at end returns 0", end && *end == 0);

    // dup した File は offset を共有する
    reader->offset = 0;
    File *shared   = FileSystem::file_duplicate(reader);
    auto part1     = FileSystem::file_read(*reader, read_buffer.data(), 600);
    bool part1_ok  = part1 && *part1 == 600 && read_buffer_matches(0, 600);
    auto part2     = FileSystem::file_read(*shared, read_buffer.data(), 100);
    ok &= report(console, "dup shares offset", part1_ok && part2 && *part2 == 100 && read_buffer_matches(600, 100));
    FileSystem::file_close(shared);

    // 向きが違う操作は失敗し、offset も動かない
    const uint32_t reader_offset = reader->offset;
    const uint32_t writer_offset = writer->offset;
    ok &= report(console,
                 "wrong direction fails",
                 !FileSystem::file_write(*reader, source_data.data(), 1) &&
                     !FileSystem::file_read(*writer, read_buffer.data(), 1) && reader->offset == reader_offset &&
                     writer->offset == writer_offset);

    // 最後の close で inode の参照を手放す。refcnt は inode, writer, reader の 3
    const int refcnt_before = inode->refcnt;
    FileSystem::file_close(writer);
    FileSystem::file_close(reader);
    ok &= report(console, "close releases inode", refcnt_before == 3 && inode->refcnt == 1);

    // 後片付け: ブロックと inode を空きに戻す (fs.img は次回も使われる)
    for (BlockNumber block_number : inode->disk.addrs)
    {
        if (block_number.value != 0)
            FileSystem::free_block(block_number);
    }
    FileSystem::internal::write_inode(inode_number, DiskInode{});
    return ok;
}

bool unusable_files(IConsole *console)
{
    File *console_file = FileSystem::file_open_console(false, true);
    File *empty        = FileSystem::file_allocate();
    if (console_file == nullptr || empty == nullptr)
        return report(console, "unusable files fail", false);
    empty->readable = true;
    empty->writable = true;

    uint8_t byte = 0;
    // 読めないコンソールは、入力を待たずに失敗する
    const bool ok = !FileSystem::file_read(*console_file, &byte, 1) && !FileSystem::file_read(*empty, &byte, 1) &&
                    !FileSystem::file_write(*empty, &byte, 1);
    FileSystem::file_close(console_file);
    FileSystem::file_close(empty);
    return report(console, "unusable files fail", ok);
}

long call_syscall(int number, uint64_t fd, const void *buffer, uint64_t count)
{
    return SystemCall::syscall_dispatch(number, fd, reinterpret_cast<uint64_t>(buffer), count, 0, 0);
}

// プロセスの fd 表: 作成時の fd 0/1/2、fork での共有、終了時の解放、syscall からの参照
void process_thread()
{
    Process *self = process::current_process();
    auto &files   = self->files;

    console_fds_ok = files[0] != nullptr && files[0]->type == FileType::kConsole && files[0]->readable &&
                     !files[0]->writable && files[1] != nullptr && files[1]->type == FileType::kConsole &&
                     !files[1]->readable && files[1]->writable && files[2] == files[1] && files[3] == nullptr;
    if (!console_fds_ok)
        return;

    const int stdin_refcnt  = files[0]->refcnt;
    const int stdout_refcnt = files[1]->refcnt;

    const int pid = process::fork();
    if (pid == 0)
    {
        // 子。スタックは複製なので self は親を指す。自分の表は current_process() から取る
        auto &child_files = process::current_process()->files;
        child_shares_ok   = child_files[0] == self->files[0] && child_files[1] == self->files[1] &&
                          child_files[2] == self->files[2] && child_files[3] == nullptr;
        // 閉じるのは子の fd だけ。親の表は変わらない (残りの fd は exit が閉じる)
        child_close_ok = call_syscall(SyscallNo::kClose, 1, nullptr, 0) == 0 && child_files[1] == nullptr;
        process::exit(0);
    }

    // 協調型なので、子はまだ走っていない
    fork_shares_ok = pid > 0 && files[0]->refcnt == stdin_refcnt + 1 && files[1]->refcnt == stdout_refcnt + 2;

    int code;
    const auto waited = process::wait(&code);
    wait_restores_ok  = waited && files[1] != nullptr && files[0]->refcnt == stdin_refcnt &&
                       files[1]->refcnt == stdout_refcnt;

    const char message[] = "[FILE] write through fd 1\n";
    const long length    = static_cast<long>(sizeof(message) - 1);
    char byte            = 0;
    syscall_fds_ok       = call_syscall(SyscallNo::kWrite, 1, message, length) == length &&
                     call_syscall(SyscallNo::kWrite, 2, message, 0) == 0 &&
                     call_syscall(SyscallNo::kWrite, 0, message, 1) == -1 && // fd 0 は書けない
                     call_syscall(SyscallNo::kRead, 1, &byte, 1) == -1 &&    // fd 1 は読めない
                     call_syscall(SyscallNo::kWrite, 3, message, 1) == -1 && // 閉じている
                     call_syscall(SyscallNo::kRead, 3, &byte, 1) == -1 &&
                     call_syscall(SyscallNo::kWrite, NUM_OPEN_FILES, message, 1) == -1 && // 範囲外
                     call_syscall(SyscallNo::kRead, NUM_OPEN_FILES, &byte, 1) == -1;

    // close: fd を空けて参照を 1 つ返す。同じ File を指す他の fd はそのまま使える
    files[3]       = FileSystem::file_duplicate(files[1]);
    const long ret = call_syscall(SyscallNo::kClose, 3, nullptr, 0);
    close_ok       = ret == 0 && files[3] == nullptr && files[1]->refcnt == stdout_refcnt &&
               call_syscall(SyscallNo::kWrite, 3, message, 1) == -1 && // 閉じた fd は使えない
               call_syscall(SyscallNo::kWrite, 1, message, 0) == 0 &&
               call_syscall(SyscallNo::kClose, 3, nullptr, 0) == -1 && // 二重 close
               call_syscall(SyscallNo::kClose, NUM_OPEN_FILES, nullptr, 0) == -1;
}

bool process_files(IConsole *console)
{
    const int free_before = count_free_slots();

    if (process::create_process(process_thread, "files") == nullptr)
        return report(console, "create process", false);
    process::yield();

    bool ok = report(console, "new process has fd 0/1/2", console_fds_ok);
    ok &= report(console, "fork shares files", fork_shares_ok && child_shares_ok);
    ok &= report(console, "exit of child closes files", wait_restores_ok);
    ok &= report(console, "syscalls go through fd table", syscall_fds_ok);
    ok &= report(console, "close syscall", close_ok);
    ok &= report(console, "close in child keeps parent's fd", child_close_ok);
    // entry から戻って終わったスレッドの分も閉じられている
    ok &= report(console, "no File leak after thread ends", count_free_slots() == free_before);
    return ok;
}

} // namespace

bool file_basic(IConsole *console)
{
    bool ok = allocate_dup_close(console);
    ok &= table_can_be_exhausted(console);
    ok &= inode_file(console);
    ok &= unusable_files(console);
    ok &= process_files(console);
    return ok;
}

} // namespace tests
