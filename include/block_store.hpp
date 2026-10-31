#ifndef BLOCK_STORE_HPP
#define BLOCK_STORE_HPP
#include <cstdint>


// ブロック番号。inode 番号やバイト数などの普通の整数と取り違えないよう、型で区別する。
// ブロック 0 も実在するので (ブート用)、0 かどうかの判定は operator bool にせず、
// 「未割当」の印として使う場所 (DiskInode::addrs など) が .value == 0 で明示する。
struct BlockNumber
{
    uint32_t value = 0;

    constexpr BlockNumber() = default;
    constexpr explicit BlockNumber(uint32_t number) : value(number) { }

    bool operator==(const BlockNumber &) const = default;
};
static_assert(sizeof(BlockNumber) == 4, "BlockNumber is stored on disk as 32 bits");

class BlockRef;

class IBlockStore
{
  public:
    virtual BlockRef acquire(BlockNumber block_number) = 0;
    virtual bool write_back(BlockNumber block_number)  = 0;
    virtual void release(BlockNumber block_number)     = 0;
    virtual ~IBlockStore()                             = default;
};

// ─── ブロック記憶域への口 ────────────────────────────────────────
// 上位層 (ファイルシステム) はこのインタフェースだけを知る。
// 下にキャッシュがあるのか、どのドライバに繋がっているのかは知らない。
// 実装は BufferCache 側が提供し、利用側はコンストラクタで注入して受け取る。

class BlockRef final
{
  public:
    BlockRef() = default;
    explicit BlockRef(BlockNumber block_number, IBlockStore *store = nullptr, uint8_t *data = nullptr)
        : block_number_(block_number), store_(store), data_(data)
    {
    }

    BlockRef(const BlockRef &)            = delete;
    BlockRef &operator=(const BlockRef &) = delete;

    // 所有権は 1 つだけ。ムーブ元は「何も持っていない」状態にする。
    BlockRef(BlockRef &&other) noexcept : block_number_(other.block_number_), store_(other.store_), data_(other.data_)
    {
        other.store_ = nullptr;
        other.data_  = nullptr;
    }
    BlockRef &operator=(BlockRef &&other) noexcept
    {
        if (this != &other)
        {
            reset(); // 今持っているものを先に返す
            block_number_ = other.block_number_;
            store_        = other.store_;
            data_         = other.data_;
            other.store_  = nullptr;
            other.data_   = nullptr;
        }
        return *this;
    }

    ~BlockRef()
    {
        reset();
    }
    BlockNumber block_number() const
    {
        return block_number_;
    }
    IBlockStore *store() const
    {
        return store_;
    }
    uint8_t *data() const
    {
        return data_;
    }

    bool operator!() const
    {
        return data_ == nullptr;
    }

    // 本体は IBlockStore の定義より後ろに置く (ここではまだ前方宣言しか見えないため)
    bool write_back()
    {
        if (store_ == nullptr)
            return false;
        if (data_ == nullptr)
            return false;
        return store_->write_back(block_number_);
    }

    void reset()
    {
        if (store_ != nullptr and data_ != nullptr)
        {
            store_->release(block_number_);
            store_ = nullptr;
            data_  = nullptr;
        }
    }

  private:
    BlockNumber block_number_{};
    IBlockStore *store_ = nullptr;
    uint8_t *data_      = nullptr;
};

#endif // BLOCK_STORE_HPP
