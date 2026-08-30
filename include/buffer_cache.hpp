#ifndef BUFFER_CACHE_HPP
#define BUFFER_CACHE_HPP

#include <cstdint>
#include <cstddef>

// ─── バッファキャッシュ層 ────────────────────────────────────────
// 目的:
//   1. 一度読んだブロックをメモリに保持し、ディスク I/O の回数を減らす
//   2. 上位層 (inode 層) に対して統一されたブロックアクセス API を提供する
//
// 位置づけ:
//   inode 層  ←→  BufferCache (ここ)  ←→  ブロックデバイス (virtio-blk 等)
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

namespace BufferCache
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

// read() で取得した参照を、スコープを抜けるときに自動で release() する RAII ガード。
// release() の呼び忘れはバッファを枯渇させる (acquire が nullptr を返すようになる)
// ので、上位層では生の read()/release() ではなくこちらを使う。
//
//   if (auto block = BufferCache::acquire(blockno))
//   {
//       block->data[0] = 0xFF;
//       block.mark_dirty();
//   } // ここで自動的に release される
//
// コピーすると解放が二重になるのでコピー禁止、受け渡し用にムーブのみ許可する。
class BufferRef final
{
  public:
    BufferRef() = default;
    explicit BufferRef(Buffer *buffer) : buffer_(buffer) { }
    ~BufferRef()
    {
        reset();
    }

    BufferRef(const BufferRef &)            = delete;
    BufferRef &operator=(const BufferRef &) = delete;

    BufferRef(BufferRef &&other) noexcept : buffer_(other.buffer_)
    {
        other.buffer_ = nullptr;
    }
    BufferRef &operator=(BufferRef &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            buffer_       = other.buffer_;
            other.buffer_ = nullptr;
        }
        return *this;
    }

    // 取得に成功したかどうか。if (auto b = acquire(n)) と書ける。
    explicit operator bool() const
    {
        return buffer_ != nullptr;
    }

    Buffer *get() const
    {
        return buffer_;
    }
    Buffer *operator->() const
    {
        return buffer_;
    }
    Buffer &operator*() const
    {
        return *buffer_;
    }

    void mark_dirty()
    {
        BufferCache::mark_dirty(buffer_);
    }
    bool write()
    {
        return BufferCache::write(buffer_);
    }

    // スコープを抜ける前に明示的に手放したいときに使う。
    void reset()
    {
        if (buffer_ != nullptr)
        {
            BufferCache::release(buffer_);
            buffer_ = nullptr;
        }
    }

  private:
    Buffer *buffer_ = nullptr;
};

// read() の RAII 版。失敗時は空の BufferRef (operator bool が false) を返す。
BufferRef acquire(uint32_t blockno);

// キャッシュの効き具合を確認するための統計。
struct Statistics
{
    uint32_t hits;       // キャッシュに載っていた回数
    uint32_t misses;     // デバイスから読んだ回数
    uint32_t evictions;  // 有効なバッファを追い出した回数
    uint32_t writebacks; // デバイスへ書き戻した回数
};
Statistics statistics();

} // namespace BufferCache

#endif // BUFFER_CACHE_HPP
