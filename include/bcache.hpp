#ifndef BCACHE_HPP
#define BCACHE_HPP

#include <cstdint>
#include <cstddef>

// ─── バッファキャッシュ層 ────────────────────────────────────────
// 目的:
//   1. 一度読んだブロックをメモリに保持し、ディスク I/O の回数を減らす
//   2. 上位層 (inode 層) に対して統一されたブロックアクセス API を提供する
//
// 位置づけ:
//   inode 層  ←→  bcache (ここ)  ←→  ブロックデバイス (virtio-blk 等)
//
// 前提: このカーネルのスケジューラは協調型 (タイマ割り込みからは yield しない)
//       なので、ロックを持たない。プリエンプティブにする際は xv6 同様に
//       バッファ単位の sleeplock が必要になる。

constexpr uint32_t BLOCK_SIZE = 512;
constexpr int NBUF            = 30; // キャッシュ数

struct Buffer
{
    uint32_t blockno;         // ブロック番号
    bool valid;               // ディスクから読み込み済みか
    bool dirty;               // 変更されたか (書き戻しが必要)
    int refcnt;               // 参照カウント (使用中の数)
    Buffer *prev, *next;      // LRUリスト用
    uint8_t data[BLOCK_SIZE]; // ブロックの中身
};

namespace bcache
{

// 下位のブロックデバイス。特定のドライバに依存しないよう関数で受け取る。
// (VirtIOBlock::read_block / write_block をそのまま渡せる形にしてある)
struct BlockDevice
{
    bool (*read_block)(uint64_t blockno, uint8_t *buffer);
    bool (*write_block)(uint64_t blockno, const uint8_t *buffer);
};

// キャッシュを初期化する。device の関数ポインタが両方揃っていなければ false。
bool initialize(const BlockDevice &device);

// ブロックを取得する。キャッシュに無ければデバイスから読み込む。
// 戻り値は参照カウントを 1 増やした状態のバッファ。失敗時は nullptr。
// 使い終わったら必ず release() すること。
Buffer *read(uint32_t blockno);

// buffer->data を書き換えた後に呼ぶ。実際の書き戻しは write() / flush() で行う。
void mark_dirty(Buffer *buffer);

// buffer をデバイスへ即座に書き戻す (write-through)。成功すると dirty が下りる。
bool write(Buffer *buffer);

// dirty なバッファをすべて書き戻す。1つでも失敗したら false を返す。
bool flush();

// 参照を手放す。refcnt が 0 になったバッファは LRU の「最近使った」側へ移る。
void release(Buffer *buffer);

// キャッシュの効き具合を確認するための統計。
struct Statistics
{
    uint32_t hits;       // キャッシュに載っていた回数
    uint32_t misses;     // デバイスから読んだ回数
    uint32_t evictions;  // 有効なバッファを追い出した回数
    uint32_t writebacks; // デバイスへ書き戻した回数
};
Statistics statistics();

} // namespace bcache

#endif // BCACHE_HPP
