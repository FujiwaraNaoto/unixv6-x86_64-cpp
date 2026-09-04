#ifndef BLOCK_STORE_HPP
#define BLOCK_STORE_HPP

#include <cstddef>
#include <cstdint>

// ─── ブロック記憶域への口 ────────────────────────────────────────
// 上位層 (ファイルシステム) はこのインタフェースだけを知る。
// 下にキャッシュがあるのか、どのドライバに繋がっているのかは知らない。
// 実装は BufferCache 側が提供し、利用側はコンストラクタで注入して受け取る。

class IBlockStore;

// 取得したブロックへの参照。スコープを抜けると自動で解放される。
// data() が指すのは store が所有する生バッファで、書き換えても write() を
// 呼ぶまでディスクには反映されない。
//
//   if (auto block = store->acquire(7))
//   {
//       block.data()[0] = 0xFF;
//       block.write();
//   } // ここで自動的に解放される
//
// コピーすると解放が二重になるのでコピー禁止、受け渡し用にムーブのみ許可する。
class BlockRef final
{
  public:
    BlockRef() = default;
    BlockRef(IBlockStore *store, uint32_t blockno, uint8_t *data) : store_(store), blockno_(blockno), data_(data) { }
    ~BlockRef()
    {
        reset();
    }

    BlockRef(const BlockRef &)            = delete;
    BlockRef &operator=(const BlockRef &) = delete;

    BlockRef(BlockRef &&other) noexcept : store_(other.store_), blockno_(other.blockno_), data_(other.data_)
    {
        other.store_ = nullptr;
        other.data_  = nullptr;
    }
    BlockRef &operator=(BlockRef &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            store_       = other.store_;
            blockno_     = other.blockno_;
            data_        = other.data_;
            other.store_ = nullptr;
            other.data_  = nullptr;
        }
        return *this;
    }

    // 取得に成功したかどうか。if (auto b = store->acquire(n)) と書ける。
    explicit operator bool() const
    {
        return data_ != nullptr;
    }

    uint8_t *data() const
    {
        return data_;
    }
    uint32_t blockno() const
    {
        return blockno_;
    }

    // 変更を書き戻す
    bool write();
    // スコープを抜ける前に明示的に手放したいときに使う
    void reset();

  private:
    IBlockStore *store_ = nullptr;
    uint32_t blockno_   = 0;
    uint8_t *data_      = nullptr;
};

class IBlockStore
{
  public:
    virtual ~IBlockStore() = default;

    // ブロックを取得する。失敗時は空の BlockRef を返す。
    virtual BlockRef acquire(uint32_t blockno) = 0;

    // 以下は BlockRef が呼ぶ。利用側が直接呼ぶ必要はない。
    virtual bool write_back(uint32_t blockno) = 0;
    virtual void release(uint32_t blockno)    = 0;
};

inline bool BlockRef::write()
{
    if (store_ == nullptr || data_ == nullptr)
    {
        return false;
    }
    return store_->write_back(blockno_);
}

inline void BlockRef::reset()
{
    if (store_ != nullptr && data_ != nullptr)
    {
        store_->release(blockno_);
    }
    store_ = nullptr;
    data_  = nullptr;
}

#endif // BLOCK_STORE_HPP
