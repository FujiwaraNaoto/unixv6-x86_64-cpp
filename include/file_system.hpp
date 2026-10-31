#ifndef FILE_SYSTEM_HPP
#define FILE_SYSTEM_HPP
#include <cstdint>
#include <array>
#include <optional>
#include "console.hpp"
#include "block_store.hpp"


// inode 番号。ブロック番号やバイト数などの普通の整数と取り違えないよう、型で区別する。
// 0 は「inode を指していない」を表す (ディレクトリエントリの空きスロットの印でもある)。
struct InodeNumber
{
    uint32_t value = 0;

    constexpr InodeNumber() = default;
    constexpr explicit InodeNumber(uint32_t number) : value(number) { }

    explicit operator bool() const
    {
        return value != 0;
    }

    bool operator==(const InodeNumber &) const = default;
};

constexpr InodeNumber ROOT_INODE{1}; // ルートの inode 番号

// xv6 の 0x10203040と同じ値にしておく。リトルエンディアンとビックエンディアンの両方で同じ値にならないように
constexpr uint32_t FS_MAGIC       = 0x10203040;
constexpr uint32_t FSBLOCK_SIZE   = 512;
constexpr int NDIRECT             = 12;                                 // 直接ブロック数
constexpr int INDIRECT_INDEX      = NDIRECT;                            // addrs 内の間接ブロックの位置
constexpr int NINDIRECT           = FSBLOCK_SIZE / sizeof(BlockNumber); // 128
constexpr int MAXFILE             = NDIRECT + NINDIRECT;                // 140ブロック
constexpr int DIRECTORY_NAME_SIZE = 14;                                 // ファイル名長 (V6と同じ)


// block 0        1         2 ...            bitmap_start ...      data_start ...
// ┌────────┬──────────┬──────────────────┬───────────────┬──────────────────┐
// │  boot  │  super   │  inode blocks    │  bitmap       │   data blocks    │
// └────────┴──────────┴──────────────────┴───────────────┴──────────────────┘
//          ↑ ここに書く  ← inode_blocks →   ← bitmap_blocks →  ← nblocks →
// └───────────────────────── size (= total_blocks) ─────────────────────────┘
//
// ディスクは FSBLOCK_SIZE (512 バイト) のブロックの並びで、先頭から上の順に使う。
// 各領域の位置と大きさは format() で計算し、スーパーブロックに記録する。
//
//   boot         (block 0)
//     ファイルシステムとしては使わない (xv6 ではブートセクタ用に空けている)。
//   super        (block 1)
//     SuperBlock を置く。magic でフォーマット済みかを判定でき、
//     ここを読めば以下の各領域の位置が分かる。
//   inode blocks (inode_start から inode_blocks 個)
//     DiskInode を 1 ブロックに INODES_PER_BLOCK (8) 個ずつ並べる。
//     inode_start は 2 固定、inode_blocks = ceil(ninodes / INODES_PER_BLOCK)。
//     inode 番号 inode_number は、inode_start + inode_number / INODES_PER_BLOCK 番目のブロックの
//     inode_number % INODES_PER_BLOCK 番目に入っている。
//   bitmap       (bitmap_start から bitmap_blocks 個)
//     ブロックの使用状況を 1 ブロック = 1 ビットで記録する (1 = 使用中)。
//     データ領域だけでなく block 0 からの全ブロックが対象なので
//     bitmap_blocks = ceil(size / BLOCKS_PER_BITMAP_BLOCK)。
//     boot からビットマップ自身までのメタデータ領域は、format() で使用中にしておく。
//     bitmap_start = inode_start + inode_blocks。
//   data blocks  (data_start から nblocks 個)
//     ファイルとディレクトリの中身。空きはビットマップで管理する。
//     data_start = bitmap_start + bitmap_blocks (スーパーブロックには持たず、計算で求める)。
//     nblocks = size - data_start。
//
// size はブロック 0 から数えた全ブロック数 (= total_blocks) で、boot も含む。
//
// 例: fs.img (2048 ブロック) を ninodes = 200 でフォーマットした場合
//   inode_start = 2, inode_blocks = 25   → bitmap_start  = 27
//   bitmap_blocks = 1                   → data_start = 28
//   nblocks = 2048 - 28 = 2020


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
    uint32_t size;            // 全ブロック数
    uint32_t nblocks;         // データブロック数
    uint32_t ninodes;         // inode 数
    BlockNumber inode_start;  // inode 領域の開始ブロック
    BlockNumber bitmap_start; // ビットマップの開始ブロック
};
static_assert(sizeof(SuperBlock) == 24, "SuperBlock must be 24 bytes");
// ─── ディスク上の inode (64バイト) ───────────────────────────────
struct DiskInode
{
    InodeType type;
    uint16_t major;
    uint16_t minor;
    uint16_t nlink;
    uint32_t size;
    std::array<BlockNumber, NDIRECT + 1> addrs; // 直接12 + 間接1
};
static_assert(sizeof(DiskInode) == 64, "DiskInode must be 64 bytes");

constexpr int INODES_PER_BLOCK = FSBLOCK_SIZE / sizeof(DiskInode); // 8個/ブロック
// ブロックビットマップは 1 ブロック = 1 ビットで管理するので、
// バイト内のビット位置を求めるときに 1 バイトのビット数が要る。
constexpr uint32_t BITS_PER_BYTE      = 8;
constexpr int BLOCKS_PER_BITMAP_BLOCK = FSBLOCK_SIZE * BITS_PER_BYTE; // 4096ブロック/ビットマップ
// ─── ディレクトリエントリ (16バイト) ─────────────────────────────
struct [[gnu::packed]] DirectoryEntry
{
    uint16_t inode_number;
    char name[DIRECTORY_NAME_SIZE];
};
static_assert(sizeof(DirectoryEntry) == 16, "DirectoryEntry must be 16 bytes");


class NullBlockStore final : public IBlockStore
{
  public:
    BlockRef acquire(BlockNumber) override
    {
        return BlockRef{};
    }
    bool write_back(BlockNumber) override
    {
        return false;
    }
    void release(BlockNumber) override { }
};

// 実体は kernel_main が作り、このポインタに設定する。
// グローバル変数として持つと .init_array の並び (= リンク順) で初期化されるため、
// 他のグローバルとの初期化順序が「たまたま動いている」状態になりやすい。
// main で明示的に構築することで、初期化順序の問題を避ける。
inline NullBlockStore *null_block_store = nullptr;


namespace FileSystem
{

// ファイルシステムの初期化 (スーパーブロックを読み、未フォーマットならフォーマットする)。
// 状態は file_system.cpp のモジュール内 static が持ち、superblock() / balloc() などは
// フリー関数としてそれを操作する (buffer_cache.cpp と同じ形)。
//
// block_store は差し替えの境目。nullptr なら null_block_store (何もしない実装) になる。
// テストでは偽の IBlockStore を渡せば、BufferCache 無しでファイルシステムを動かせる。
//
// 戻り値は初期化に成功したか。フォーマット済みだった場合も、新規にフォーマットした
// 場合も true。
[[nodiscard]] bool initialize(uint32_t total_blocks, IBlockStore *block_store = nullptr, IConsole *console = nullptr);

const SuperBlock &superblock();

std::optional<BlockNumber> allocate_block();
void free_block(BlockNumber block_number);

} // namespace FileSystem

#endif // FILE_SYSTEM_HPP
