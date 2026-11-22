#include "tests.hpp"
#include "file.hpp"
#include "file_system_internal.hpp"
#include "process.hpp"
#include "systemcall.hpp"
#include "block_store.hpp"
#include "file_system.hpp"
#include "inode.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace tests
{
namespace
{

// スレッド関数の引数 (void *) は使わず、結果はグローバル変数で受け渡す。
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

// FileIO を 1 つ開く。中身は読まないので、どのファイルでもよい
FileRef open_root()
{
    return FileSystem::open_file(FileSystem::get_inode(ROOT_INODE), true, false);
}

// スロットの表の空きの数を数える (open できなくなるまで開いてから全部閉じる)。
// 満杯で空の FileRef が返ることの確認も兼ねる。
int count_free_slots()
{
    std::array<FileRef, FileSystem::NUM_FILES + 1> taken{};
    int count = 0;
    while (count <= FileSystem::NUM_FILES)
    {
        taken[count] = open_root();
        if (!taken[count])
            break;
        count++;
    }
    for (int i = 0; i < count; i++)
        taken[i].reset();
    return count;
}

// 唯一の StandardInput / StandardOutput が、いま何か所から参照されているか (調べるための 1 つは除く)
size_t standard_refcounts()
{
    const FileRef input  = FileSystem::open_standard_input();
    const FileRef output = FileSystem::open_standard_output();
    return (input.use_count() - 1) + (output.use_count() - 1);
}

bool open_dup_close(IConsole *console)
{
    FileRef file = open_root();
    bool ok      = file && file.use_count() == 1;
    if (ok)
    {
        FileRef dup = file; // dup
        ok          = dup.get() == file.get() && file.use_count() == 2;
        dup.reset(); // close
        ok = ok && !dup && dup.get() == nullptr && file.use_count() == 1;
        // 他方の FileRef からはそのまま使える。最後の close で実体が破棄される
        uint8_t probe = 0;
        ok            = ok && file->read(&probe, 0) == 0;
        file.reset();
        ok = ok && !file && file.use_count() == 0;
    }
    // ムーブは参照を移すだけで数を変えない
    FileRef moved_from = open_root();
    FileRef moved_to   = std::move(moved_from);
    ok                 = ok && !moved_from && moved_to && moved_to.use_count() == 1;
    return report(console, "open / dup / close", ok);
}

bool table_can_be_exhausted(IConsole *console)
{
    const int free_slots = count_free_slots();
    // 全部返したので、もう一度数えても同じ。満杯なら NUM_FILES を超えない
    return report(console,
                  "exhausted table returns nullptr",
                  free_slots > 0 && free_slots <= FileSystem::NUM_FILES && count_free_slots() == free_slots);
}

// 標準入出力は実体が 1 つで、open は参照カウントを増やすだけ
bool standard_files_are_shared(IConsole *console)
{
    FileRef input1             = FileSystem::open_standard_input();
    FileRef input2             = FileSystem::open_standard_input();
    FileRef output1            = FileSystem::open_standard_output();
    FileRef output2            = FileSystem::open_standard_output();
    const size_t output_refcnt = output1.use_count();
    bool ok = input1.get() == input2.get() && output1.get() == output2.get() && input1.get() != output1.get();
    output2.reset();
    // 閉じても実体は残り、他の参照からはそのまま書ける
    ok = ok && output1.use_count() == output_refcnt - 1 && output1->write("", 0) == 0;
    return report(console, "standard files are shared", ok);
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

bool inode_file(IConsole *console)
{
    InodeRef inode = FileSystem::allocate_inode(InodeType::kFile);
    if (!inode)
        return report(console, "inode file", false);
    const InodeNumber inode_number = inode->inode_number;

    for (uint32_t i = 0; i < kDataSize; i++)
        source_data[i] = static_cast<uint8_t>(i * 31 + i / FSBLOCK_SIZE);

    FileRef writer = FileSystem::open_file(FileSystem::get_inode(inode_number), false, true);
    FileRef reader = FileSystem::open_file(FileSystem::get_inode(inode_number), true, false);
    FileRef other  = FileSystem::open_file(FileSystem::get_inode(inode_number), true, false);
    if (!writer || !reader || !other)
        return report(console, "inode file", false);

    // 2 回に分けて書くと、offset が進んで続きに書かれる (進まなければ size は 600 になる)
    const ssize_t first  = writer->write(source_data.data(), 100);
    const ssize_t second = writer->write(source_data.data() + 100, kDataSize - 100);
    bool ok              = report(console,
                     "write advances offset",
                     first == 100 && second == kDataSize - 100 && inode->disk.size == kDataSize);

    // reader は writer とは別の FileIO なので offset は 0 から
    const ssize_t all = reader->read(read_buffer.data(), kDataSize);
    ok &= report(console, "separate file has own offset", all == kDataSize && read_buffer_matches(0, kDataSize));

    ok &= report(console, "read at end returns 0", reader->read(read_buffer.data(), 1) == 0);

    // dup した FileRef は同じ FileDescriptor を指すので offset を共有する
    FileRef shared      = other;
    const ssize_t part1 = other->read(read_buffer.data(), 600);
    const bool part1_ok = part1 == 600 && read_buffer_matches(0, 600);
    const ssize_t part2 = shared->read(read_buffer.data(), 100);
    ok &= report(console, "dup shares offset", part1_ok && part2 == 100 && read_buffer_matches(600, 100));
    shared.reset();

    // 向きが違う操作は失敗し、offset も動かない (reader は末尾のまま)
    ok &= report(console,
                 "wrong direction fails",
                 reader->write(source_data.data(), 1) == kError && writer->read(read_buffer.data(), 1) == kError &&
                     reader->read(read_buffer.data(), 1) == 0 && inode->disk.size == kDataSize);

    // 最後の close で inode の参照を手放す。refcnt は inode, writer, reader, other の 4
    const int refcnt_before = inode->refcnt;
    writer.reset();
    reader.reset();
    other.reset();
    ok &= report(console, "close releases inode", refcnt_before == 4 && inode->refcnt == 1);

    // 後片付け: ブロックと inode を空きに戻す (fs.img は次回も使われる)
    for (BlockNumber block_number : inode->disk.addrs)
    {
        if (block_number.value != 0)
            FileSystem::free_block(block_number);
    }
    FileSystem::internal::write_disk_inode(inode_number, DiskInode{});
    return ok;
}

bool unusable_files(IConsole *console)
{
    FileRef input  = FileSystem::open_standard_input();
    FileRef output = FileSystem::open_standard_output();
    FileRef closed =
        FileSystem::open_file(FileSystem::get_inode(ROOT_INODE), false, false); // 読み書きどちらも不可で開く
    if (!input || !output || !closed)
        return report(console, "unusable files fail", false);

    uint8_t byte = 0;
    // 標準出力の read は、入力を待たずに失敗する
    const bool ok = input->write(&byte, 1) == kError && output->read(&byte, 1) == kError &&
                    closed->read(&byte, 1) == kError && closed->write(&byte, 1) == kError;
    return report(console, "unusable files fail", ok);
}

long call_syscall(int number, uint64_t fd, const void *buffer, uint64_t count)
{
    return SystemCall::syscall_dispatch(number, fd, reinterpret_cast<uint64_t>(buffer), count, 0, 0);
}

// プロセスの fd 表: 作成時の fd 0/1/2、fork での共有、終了時の解放、syscall からの参照
void process_thread(void *)
{
    Process *self = process::current_process();
    auto &files   = self->files;

    uint8_t probe  = 0;
    console_fds_ok = files[0] && files[1] && files[0].get() != files[1].get() && files[2].get() == files[1].get() &&
                     !files[3] && files[0]->write(&probe, 1) == kError && files[1]->read(&probe, 1) == kError;
    if (!console_fds_ok)
        return;

    const size_t stdin_refcnt  = files[0].use_count();
    const size_t stdout_refcnt = files[1].use_count();

    const int pid = process::fork();
    if (pid == 0)
    {
        // 子。スタックは複製なので self は親を指す。自分の表は current_process() から取る
        auto &child_files = process::current_process()->files;
        child_shares_ok   = child_files[0].get() == self->files[0].get() &&
                          child_files[1].get() == self->files[1].get() &&
                          child_files[2].get() == self->files[2].get() && !child_files[3];
        // 閉じるのは子の fd だけ。親の表は変わらない (残りの fd は exit が閉じる)
        child_close_ok = call_syscall(SyscallNo::kClose, 1, nullptr, 0) == 0 && !child_files[1];
        process::exit(0);
    }

    // 協調型なので、子はまだ走っていない
    fork_shares_ok = pid > 0 && files[0].use_count() == stdin_refcnt + 1 && files[1].use_count() == stdout_refcnt + 2;

    int code;
    const auto waited = process::wait(&code);
    wait_restores_ok  = waited && files[1] && files[0].use_count() == stdin_refcnt &&
                       files[1].use_count() == stdout_refcnt;

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
    files[3]       = files[1];
    const long ret = call_syscall(SyscallNo::kClose, 3, nullptr, 0);
    close_ok       = ret == 0 && !files[3] && files[1].use_count() == stdout_refcnt &&
               call_syscall(SyscallNo::kWrite, 3, message, 1) == -1 && // 閉じた fd は使えない
               call_syscall(SyscallNo::kWrite, 1, message, 0) == 0 &&
               call_syscall(SyscallNo::kClose, 3, nullptr, 0) == -1 && // 二重 close
               call_syscall(SyscallNo::kClose, NUM_OPEN_FILES, nullptr, 0) == -1;
}

bool process_files(IConsole *console)
{
    const size_t refcnt_before = standard_refcounts();

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
    ok &= report(console, "no file leak after thread ends", standard_refcounts() == refcnt_before);
    return ok;
}

} // namespace

bool file_basic(IConsole *console)
{
    bool ok = open_dup_close(console);
    ok &= table_can_be_exhausted(console);
    ok &= standard_files_are_shared(console);
    ok &= inode_file(console);
    ok &= unusable_files(console);
    ok &= process_files(console);
    return ok;
}

} // namespace tests
