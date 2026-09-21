#ifndef INODE_HPP
#define INODE_HPP
#include <cstdint>
#include <optional>
#include "file_system.hpp"

namespace FileSystem
{

// 同時に開いておける inode の数。
constexpr int NUM_OPEN_INODES = 50;

// メモリ上の inode。
// 同じ inum を何度 get_inode() しても実体は 1 つになるよう、テーブルで管理する。
//
// NOTE: 既定値を書かないのは、テーブルを .bss (ゼロ初期化) に置くため。
//       既定値を書くと実行時のコンストラクタが要る = .init_array の順序に依存する。
struct Inode
{
    uint32_t inum;  // inode 番号 (0 は未使用を表すので有効な inode には使わない)
    int ref;        // このエントリを参照している数。0 で解放
    bool valid;     // disk をディスクから読み込み済みか
    DiskInode disk; // ディスク上の内容のコピー
};

// ─── 段階1: inode の確保と参照 ──────────────────────────────────

// inum の inode を取得し、参照数を 1 増やす。ディスクからの読み込みもここで行う。
// 使い終わったら put_inode() を呼ぶこと。
// 範囲外の inum、テーブルが一杯、読み込み失敗の場合は nullptr。
Inode *get_inode(uint32_t inum);

// 参照数を 1 減らす。0 になり、かつリンクが無い (nlink == 0) inode は
// ブロックを解放して未使用に戻す。
void put_inode(Inode *ip);

// メモリ上の内容をディスクへ書き戻す。
bool update_inode(Inode *ip);

// 未使用の inode を 1 つ確保し、type を設定してその inum を返す。
// nlink は 0 で作るので、ディレクトリに登録 (link_directory) してから put_inode() すること。
// 登録せずに put_inode() すると、その時点で解放される。
// 空きが無ければ nullopt。
std::optional<uint32_t> allocate_inode(InodeType type);

// ─── 段階2: ファイル内ブロック番号 → ディスクのブロック番号 ─────

// block_index 番目のブロックのディスク上の番号を返す。
// allocate = true なら、未割り当てのときに確保する (メモリ上の inode を書き換えるので、
// 呼び出し側が update_inode() を呼ぶこと)。
// ファイルの最大サイズ (MAXFILE ブロック) を超える場合や、確保に失敗した場合は nullopt。
std::optional<uint32_t> block_map(Inode *ip, uint32_t block_index, bool allocate);

// ファイルの中身を全て解放し、サイズを 0 にする。
void truncate_inode(Inode *ip);

// ─── 段階3: 読み書き ────────────────────────────────────────────

// offset から n バイトを dst へ読む。読めたバイト数を返す。
// ファイル末尾を超える分は読まないので、戻り値が n より小さいことがある。
std::optional<uint32_t> read_inode(Inode *ip, void *dst, uint32_t offset, uint32_t n);

// offset から n バイトを src から書く。書けたバイト数を返す。
// 穴を作らないよう、offset は size 以下であること (size と同じなら追記)。
std::optional<uint32_t> write_inode(Inode *ip, const void *src, uint32_t offset, uint32_t n);

} // namespace FileSystem

#endif // INODE_HPP
