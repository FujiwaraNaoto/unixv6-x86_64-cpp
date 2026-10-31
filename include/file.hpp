#ifndef FILE_HPP
#define FILE_HPP

#include "console.hpp"
#include "inode.hpp"

#include <cstdint>
#include <optional>

// fd (整数) と inode の間の層。
//   Process::files[fd] → File → InodeRef
//
// inode と分けているのは、読み書き位置 (offset) を inode ではなく File が持つため。
// 同じファイルを 2 回 open すれば offset は別々になり、fork した親子は同じ File を
// 指すので offset を共有する。コンソールも同じ口で読み書きできる。

enum class FileType : uint8_t
{
    kNone,    // 空きスロット
    kConsole, // fd 0/1/2 用
    kInode,   // ディスク上のファイル
};

struct File
{
    FileType type = FileType::kNone;
    int refcnt    = 0; // fd 表から何か所で指されているか (fork / dup で増える)
    bool readable = false;
    bool writable = false;
    InodeRef inode;      // kInode のときだけ有効。close で reset → put_inode
    uint32_t offset = 0; // kInode のときの読み書き位置
};

namespace FileSystem
{

constexpr int NUM_FILES = 100; // システム全体で開けるファイル数 (xv6 の NFILE)

// kConsole の書き込み先を登録する。nullptr なら null_console に捨てる。
void file_set_console(IConsole *console);

File *file_allocate();       // 空きスロットを refcnt = 1 で返す。満杯なら nullptr
File *file_dup(File *file);  // refcnt++ して同じ File を返す (fork 用)。nullptr はそのまま返す
void file_close(File *file); // refcnt--。0 になったら inode を手放して空きに戻す

// kConsole の File を作る。満杯なら nullptr。
File *file_open_console(bool readable, bool writable);

// 現在の offset から読み書きして offset を進める。処理したバイト数を返す。
// 読めない / 書けない File と、下の層の失敗は std::nullopt。ファイル末尾を読むと 0。
// NOTE: kConsole の読み込みは 1 行入力されるまで返らない。
std::optional<uint32_t> file_read(File &file, uint8_t *dest, uint32_t count);
std::optional<uint32_t> file_write(File &file, const uint8_t *source, uint32_t count);

} // namespace FileSystem

#endif // FILE_HPP
