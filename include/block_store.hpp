#ifndef BLOCK_STORE_HPP
#define BLOCK_STORE_HPP
#include <cstdint>


class BlockRef;

class IBlockStore
{
  public:
    virtual BlockRef acquire(uint32_t blockno) = 0;
    virtual bool write_back(uint32_t blockno)  = 0;
    virtual void release(uint32_t blockno)     = 0;
    virtual ~IBlockStore()                     = default;
};

// ─── ブロック記憶域への口 ────────────────────────────────────────
// 上位層 (ファイルシステム) はこのインタフェースだけを知る。
// 下にキャッシュがあるのか、どのドライバに繋がっているのかは知らない。
// 実装は BufferCache 側が提供し、利用側はコンストラクタで注入して受け取る。

class BlockRef final
{
public:
    BlockRef() = default;
    explicit BlockRef(uint32_t blockno, IBlockStore *store = nullptr, uint8_t *data = nullptr)
        : blockno_(blockno), store_(store), data_(data) { }
    
    BlockRef(const BlockRef &)            = delete;
    BlockRef &operator=(const BlockRef &) = delete;

    // 所有権は 1 つだけ。ムーブ元は「何も持っていない」状態にする。
    BlockRef(BlockRef &&other) noexcept
        : blockno_(other.blockno_), store_(other.store_), data_(other.data_)
    {
        other.store_ = nullptr;
        other.data_  = nullptr;
    }
    BlockRef &operator=(BlockRef &&other) noexcept
    {
        if (this != &other)
        {
            reset(); // 今持っているものを先に返す
            blockno_     = other.blockno_;
            store_       = other.store_;
            data_        = other.data_;
            other.store_ = nullptr;
            other.data_  = nullptr;
        }
        return *this;
    }

    ~BlockRef()
    {
        reset();
    }
    uint32_t blockno() const { return blockno_; }
    IBlockStore *store() const { return store_; }
    uint8_t *data() const { return data_; }

    bool operator!() const { return data_ == nullptr; }

    // 本体は IBlockStore の定義より後ろに置く (ここではまだ前方宣言しか見えないため)
    bool write_back(){
        if (store_ == nullptr) return false;
        if (data_ == nullptr) return false;
        return store_->write_back(blockno_);
    }

    void reset(){
        if (store_ != nullptr and data_ != nullptr)
        {
            store_->release(blockno_);
            store_ = nullptr;
            data_  = nullptr;
        }
    }

private:
    uint32_t blockno_ = 0;
    IBlockStore *store_ = nullptr;
    uint8_t *data_ = nullptr;
};

#endif // BLOCK_STORE_HPP
