#ifndef FILE_HPP
#define FILE_HPP

#include <cstdint>
#include "inode.hpp"

// ─── ファイル層 ────────────────────────────────────────────────
// fd (整数) と inode の間を埋める層。
//   fd → Process::ofile[fd] → File → InodeRef
//
// File を inode と分けているのは、
//   1. 同じファイルを複数回 open したとき offset を独立させたい
//   2. fork した親子では offset を共有したい (同じ File を指す)
//   3. コンソール (キーボード/VGA) も同じ fd インターフェースで扱いたい
// の3点のため。

namespace FileSystem
{

enum class FileType : uint8_t
{
    kNone = 0,
    kInode,      // 通常ファイル・ディレクトリ
    kConsole,    // 標準入出力
};

struct File
{
    FileType type     = FileType::kNone;
    int      refcnt   = 0;
    bool     readable = false;
    bool     writable = false;
    InodeRef inode;           // type == kInode のときだけ有効
    uint32_t offset   = 0;    // ファイル内の現在位置
};

// ファイルテーブルの初期化を担うクラス。
// (BufferCache::Manager と同じ形)
class FileTableManager final
{
public:
    FileTableManager();

    bool valid() const
    {
        return valid_;
    }

private:
    bool valid_ = false;
};

// 空きスロットを1つ確保する。失敗時は nullptr。
File *file_allocate();

// 参照カウントを1増やして同じポインタを返す (fork/dup 用)。
File *file_duplicate(File *file);

// 参照カウントを1減らす。0 になったらスロットを解放する。
void  file_close(File *file);

// file から最大 n バイト読む。読めたバイト数を返す (失敗は -1)。
// offset は読んだぶんだけ進む。
int   file_read(File *file, uint8_t *buffer, uint32_t n);

// file へ最大 n バイト書く。書けたバイト数を返す (失敗は -1)。
int   file_write(File *file, const uint8_t *buffer, uint32_t n);

// コンソール用の File を作る (fd 0/1/2 の初期化に使う)。
File *file_open_console(bool readable, bool writable);

} // namespace FileSystem

#endif // FILE_HPP
