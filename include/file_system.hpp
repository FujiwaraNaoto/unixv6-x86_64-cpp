#ifndef FILE_SYSTEM_HPP
#define FILE_SYSTEM_HPP
#include <cstdint>

constexpr uint32_t FS_MAGIC     = 0x10203040;
constexpr uint32_t FSBLOCK_SIZE = 512;
constexpr int      NDIRECT      = 12;                            // 直接ブロック数
constexpr int      NINDIRECT    = FSBLOCK_SIZE / sizeof(uint32_t); // 128
constexpr int      MAXFILE      = NDIRECT + NINDIRECT;           // 140ブロック
constexpr int      DIRSIZ       = 14;                            // ファイル名長 (V6と同じ)
constexpr uint32_t ROOTINO      = 1;                             // ルートの inode 番号

// inode の種類
constexpr uint16_t T_UNUSED = 0;
constexpr uint16_t T_DIR    = 1;
constexpr uint16_t T_FILE   = 2;
// ─── スーパーブロック (ブロック1) ────────────────────────────────
struct SuperBlock {
    uint32_t magic;
    uint32_t size;         // 全ブロック数
    uint32_t nblocks;      // データブロック数
    uint32_t ninodes;      // inode 数
    uint32_t inodestart;   // inode 領域の開始ブロック
    uint32_t bmapstart;    // ビットマップの開始ブロック
};
// ─── ディスク上の inode (64バイト) ───────────────────────────────
struct DiskInode {
    uint16_t type;
    uint16_t major;
    uint16_t minor;
    uint16_t nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT + 1];   // 直接12 + 間接1
};
static_assert(sizeof(DiskInode) == 64, "DiskInode must be 64 bytes");

constexpr int IPB = FSBLOCK_SIZE / sizeof(DiskInode);   // 8個/ブロック
constexpr int BPB = FSBLOCK_SIZE * 8;                   // 4096ブロック/ビットマップ
// ─── ディレクトリエントリ (16バイト) ─────────────────────────────
struct DirEntry {
    uint16_t inum;
    char     name[DIRSIZ];
};
static_assert(sizeof(DirEntry) == 16, "DirEntry must be 16 bytes");

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
    explicit Manager(uint32_t total_blocks);
    // 初期化に成功したか。
    // フォーマット済みだった場合も、新規にフォーマットした場合も true。
    bool valid() const
    {
        return valid_;
    }

private:
    bool valid_ = false;
};

const SuperBlock &superblock();
// inode番号 inum が入っているブロック番号
uint32_t inode_block(uint32_t inum);
// ブロック b のビットが入っているビットマップブロック番号
uint32_t bitmap_block(uint32_t b);

// 空きブロックを1つ確保してゼロクリアする (0なら失敗)
uint32_t allocate_block();
// ブロックを解放する
void    free_block(uint32_t blockno);
} // namespace FileSystem

#endif // FILE_SYSTEM_HPP
