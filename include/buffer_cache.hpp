#ifndef BUFFER_CACHE_HPP
#define BUFFER_CACHE_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include "block_store.hpp"

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
    uint32_t blockno;                     // ブロック番号
    bool valid;                           // ディスクから読み込み済みか
    bool dirty;                           // 変更されたか (書き戻しが必要)
    int refcnt;                           // 参照カウント (使用中の数)
    Buffer *prev, *next;                  // LRUリスト用
    std::array<uint8_t, BLOCK_SIZE> data; // ブロックの中身
};

namespace BufferCache
{

// 下位のブロックデバイス。特定のドライバに依存しないよう関数で受け取る。
// (VirtIOBlock::read_block / write_block をそのまま渡せる形にしてある)
//
// 生の関数ポインタではなく std::function にしてあるのは、テストから状態を持つ
// 偽デバイス (RAM ディスク等) を差し込めるようにするため。実 virtio-blk 無しで
// ヒット率や eviction を検証できる。
//
//   uint8_t disk[NBLOCK][BLOCK_SIZE];
//   BufferCache::BlockDevice{
//       .read_block = [&disk](uint64_t n, uint8_t *dst) { ... },
//       ...
//   }
//
// 注意: キャプチャは 16 バイト以内に収めること。これを超えると std::function が
//       グローバル operator new を要求するが、このカーネルには実体が無く
//       リンクエラーになる (crt_stubs.cpp の operator delete も空実装のまま)。
//       配列そのものではなくポインタや参照 1 個をキャプチャすれば足りる。
struct BlockDevice
{
    std::function<bool(uint64_t blockno, uint8_t *buffer)> read_block;
    std::function<bool(uint64_t blockno, const uint8_t *buffer)> write_block;
};

// バッファキャッシュの初期化 (LRU リストの構築とデバイスの登録)。
// 状態は buffer_cache.cpp のモジュール内 static が持ち、acquire() / release() など
// はフリー関数としてそれを操作する (process.cpp と同じ形)。
//
// false を返すのは次の 2 つ。
//   1. device の呼び出し先が揃っていない (何も登録せずに戻る)
//   2. 2 回目以降の呼び出し
// 2 を弾くのは、やり直すと refcnt と LRU リストが初期値に戻り、誰かが掴んでいる
// バッファが同時に別のブロックとして再利用されて中身が壊れるため。
//
// 戻り値を見るのは呼び出し側の 1 回だけでよい。acquire()/write()/flush() は
// 未初期化でも安全に空振りするので、呼び出しごとの初期化済み判定は持たない。
[[nodiscard]] bool initialize(const BlockDevice &device);

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
//
// NOTE: Buffer を直接触る低レベル側の口。上位層は block_store() 経由の BlockRef を
//       使うので、今これを使っているのはキャッシュ自体を調べる tests/virtio_test.cpp
//       だけ。
BufferRef acquire(uint32_t blockno);


// 上位層 (ファイルシステム) への入口。IBlockStore の呼び出しを BufferCache の
// フリー関数へ中継するだけの、状態を持たない変換役。
//
// デバイスは initialize() で登録済みのものを使うため、コンストラクタで受け取らない。
// (受け取れる形にすると、インスタンスごとに別のデバイスを指定できるように見えて、
//  実際は黙って無視される)
class BlockStore final : public IBlockStore
{
  public:
    BlockRef acquire(uint32_t blockno) override;
    bool write_back(uint32_t blockno) override;
    void release(uint32_t blockno) override;
};

// 実体は kernel_main が作り、ファイルシステムへ注入する。状態を持たないので、
// どのインスタンスも同じキャッシュと同じデバイスを指す。
//
// ファイルシステム側は IBlockStore を注入で受け取るので、テストでは偽の
// IBlockStore (RAM ディスク等) を渡して BufferCache 無しで動かせる。


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
