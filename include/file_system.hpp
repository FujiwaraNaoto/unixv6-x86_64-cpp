#ifndef FILE_SYSTEM_HPP
#define FILE_SYSTEM_HPP
#include <cstdint>
#include "console.hpp"
#include "kstring.hpp"

constexpr uint32_t FS_MAGIC       = 0x10203040;
constexpr uint32_t FSBLOCK_SIZE   = 512;
constexpr int NDIRECT             = 12;                              // 直接ブロック数
constexpr int NINDIRECT           = FSBLOCK_SIZE / sizeof(uint32_t); // 128
constexpr int MAXFILE             = NDIRECT + NINDIRECT;             // 140ブロック
constexpr int DIRECTORY_NAME_SIZE = 14;                              // ファイル名長 (V6と同じ)
constexpr uint32_t ROOTINO        = 1;                               // ルートの inode 番号

// inode の種類。
// ディスク上の DiskInode::type にそのまま格納されるので、基底型を uint16_t に
// 固定してレイアウトを保つ。固定基底型の enum は基底型の範囲すべてが有効な値
// なので、壊れたディスクから読んだ未知の値を保持しても未定義動作にはならない
// (その代わり switch には default が要る)。
enum class InodeType : uint16_t
{
    kUnused    = 0,
    kDirectory = 1,
    kFile      = 2,
};
// ─── スーパーブロック (ブロック1) ────────────────────────────────
struct SuperBlock
{
    uint32_t magic;
    uint32_t size;       // the number of blocks in the file system
    uint32_t nblocks;    // the number of data blocks
    uint32_t ninodes;    // the number of inodes
    uint32_t inodestart; // inode start block
    uint32_t bmapstart;  // bitmap start block
};
// ─── ディスク上の inode (64バイト) ───────────────────────────────
struct DiskInode
{
    InodeType type;
    uint16_t major;
    uint16_t minor;
    uint16_t nlink; // the number of hardlinks
    uint32_t size; // bytes
    uint32_t addrs[NDIRECT + 1]; // direct blocks + 1 indirect block
};
static_assert(sizeof(DiskInode) == 64, "DiskInode must be 64 bytes");

// 1 ブロックに収まる inode の数。inode 番号 ↔ ブロック番号の換算に使う。
//   inum / INODES_PER_BLOCK … 何番目のブロックか
//   inum % INODES_PER_BLOCK … そのブロック内の何番目か
constexpr int INODES_PER_BLOCK = FSBLOCK_SIZE / sizeof(DiskInode); // 512 / 64 = 8

// ビットマップ 1 ブロックが管理できるブロック数 (1 ブロック = 1 ビット)。
//   blockno / BLOCKS_PER_BITMAP_BLOCK … 何番目のビットマップブロックか
//   blockno % BLOCKS_PER_BITMAP_BLOCK … その中の何ビット目か
constexpr int BLOCKS_PER_BITMAP_BLOCK = FSBLOCK_SIZE * 8; // 512 * 8 = 4096
// ディレクトリエントリの名前を扱う値型。
// 容量は DIRECTORY_NAME_SIZE 文字 + NUL 終端の 1 バイト。
using FileName = kstring<DIRECTORY_NAME_SIZE + 1>;

// ─── ディレクトリエントリ ────────────────────────────────────────
// ディスク上の形とメモリ上で扱う形を分ける。
//
// ディスク側 (DiskDirEntry) は「バイト列の仕様」として固定する。長さフィールドを
// 持たせないので、ホストの size_t のサイズやエンディアンに左右されず、V6 と同じ
// 16 バイトのままになる。ディスクから読んだ値を長さとして信用する経路も生まれない。
//
// メモリ側 (DirEntry) は kstring を使い、比較や代入を素直に書けるようにする。
// 相互変換は to_memory() / to_disk() に閉じ込める。

// ディスク上の並び (16バイト、V6 互換)。
// name は DIRECTORY_NAME_SIZE 文字ちょうどのとき NUL 終端されない。
struct [[gnu::packed]] DiskDirEntry
{
    uint16_t inum;
    char name[DIRECTORY_NAME_SIZE];
};
static_assert(sizeof(DiskDirEntry) == 16, "DiskDirEntry must be 16 bytes");

// メモリ上で扱うときの型
struct DirEntry
{
    uint16_t inum;
    FileName name;
};

namespace FileSystem
{

// ファイルシステムの初期化を担うクラス。
// コンストラクタが「スーパーブロックを読み、未フォーマットならフォーマットする」
// 処理を行う。状態は fs.cpp のモジュール内 static が持ち、
// superblock() / balloc() などはフリー関数としてそれを操作する
// (BufferCache::Manager と同じ形)。
class Manager final
{
  public:
    // console は進行状況の出力先。出力先を差し替えられるよう注入で受け取る
    // (このクラスは VGA / シリアルのどちらに出るかを知らない)。
    // nullptr を渡した場合はログを捨てる。
    Manager(uint32_t total_blocks, IConsole *console);
    // 初期化に成功したか。
    // フォーマット済みだった場合も、新規にフォーマットした場合も true。
    bool valid() const
    {
        return valid_;
    }

  private:
    bool valid_ = false;
};

// ディスク上の並び ←→ メモリ上の型。
// to_memory() は長さ上限付きで読むので、ディスクの内容が壊れていても
// name が DIRECTORY_NAME_SIZE 文字を超えることはない。
DirEntry to_memory(const DiskDirEntry &entry);
DiskDirEntry to_disk(const DirEntry &entry);

const SuperBlock &superblock();
// inode番号 inum が入っているブロック番号
uint32_t inode_block(uint32_t inum);
// ブロック b のビットが入っているビットマップブロック番号
uint32_t bitmap_block(uint32_t b);

// 空きブロックを1つ確保してゼロクリアする (0なら失敗)
uint32_t allocate_block();
// ブロックを解放する
void free_block(uint32_t blockno);
} // namespace FileSystem

#endif // FILE_SYSTEM_HPP
