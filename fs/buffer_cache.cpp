#include "buffer_cache.hpp"

namespace
{

std::array<Buffer, NBUF> buffers{};

// LRU リストの番兵。
//   head.next … 最近使ったバッファ (MRU)
//   head.prev … 最も長く使っていないバッファ (LRU)
// 再利用はこの LRU 側から探す。
//
// 静的初期化の時点で自分自身を指す = 「空のリスト」にしておく。こうすると
// initialize() の前に acquire() が呼ばれても、リスト走査が空振りして nullptr が
// 返るだけで済む。各関数で初期化済みフラグを見る必要が無くなる。
// (自身のアドレスは定数式なので .init_array ではなく .data に畳まれる)
Buffer head = {
    .block_number = BlockNumber{},
    .valid        = false,
    .dirty        = false,
    .refcnt       = 0,
    .prev         = &head,
    .next         = &head,
    .data         = {},
};

BufferCache::BlockDevice device{}; // 未初期化状態では両方とも nullptr
bool initialized = false;          // initialize() を 2 回通さないための印
BufferCache::Statistics stats{.hits = 0, .misses = 0, .evictions = 0, .writebacks = 0};

// buffer をリストから外す
void unlink(Buffer *buffer)
{
    buffer->prev->next = buffer->next;
    buffer->next->prev = buffer->prev;
}

// buffer を MRU 側 (head の直後) に繋ぐ
void link_as_most_recent(Buffer *buffer)
{
    buffer->next    = head.next;
    buffer->prev    = &head;
    head.next->prev = buffer;
    head.next       = buffer;
}

// デバイスへ書き戻す。成功したら dirty を下ろす。
bool write_back(Buffer *buffer)
{
    if (!device.write_block(buffer->block_number.value, buffer->data.data()))
    {
        return false;
    }
    buffer->dirty = false;
    stats.writebacks++;
    return true;
}

// block_number が既にキャッシュに載っているかを調べるだけの関数。
// refcnt は増やさず、バッファの再利用も行わない。載っていなければ nullptr。
// 「掴む」意図が無い問い合わせ (write_back / release) はこちらを使う。
Buffer *lookup(BlockNumber block_number)
{
    // MRU 側から探す
    for (Buffer *b = head.next; b != &head; b = b->next)
    {
        if (b->valid && b->block_number == block_number)
        {
            return b;
        }
    }
    return nullptr;
}

// block_number に対応するバッファを確保する (xv6 の bget 相当)。
// キャッシュに有ればそれを、無ければ LRU 側の未使用バッファを再利用する。
// 戻り値は refcnt を 1 増やした状態。再利用した場合は valid=false で返るので、
// 呼び出し側がデバイスから読み込む責任を持つ。
Buffer *find_cached(BlockNumber block_number)
{
    // 1. 既にキャッシュに載っているか
    if (Buffer *cached = lookup(block_number); cached != nullptr)
    {
        cached->refcnt++;
        return cached;
    }

    // 2. 無ければ LRU 側から未使用 (refcnt == 0) のものを再利用する
    for (Buffer *b = head.prev; b != &head; b = b->prev)
    {
        if (b->refcnt != 0)
        {
            continue;
        }
        // 追い出す前に、変更が残っていれば必ず書き戻す
        if (b->dirty && !write_back(b))
        {
            return nullptr;
        }
        if (b->valid)
        {
            stats.evictions++;
        }
        b->block_number = block_number;
        b->valid        = false;
        b->dirty        = false;
        b->refcnt       = 1;
        return b;
    }

    // 3. 全バッファが使用中 = release() 漏れ。上位層のバグ。
    return nullptr;
}

} // namespace

namespace BufferCache
{

bool initialize(const BlockDevice &block_device)
{
    // 2 回目はやり直さない。refcnt と LRU リストを初期値に戻すと、誰かが掴んでいる
    // バッファが同時に別のブロックとして再利用されてしまう。
    if (initialized)
    {
        return false;
    }

    // 呼び出し先が入っていなければ登録しない。
    if (!block_device.read_block || !block_device.write_block)
    {
        return false;
    }
    device = block_device;

    head.next = &head;
    head.prev = &head;
    for (Buffer &buffer : buffers)
    {
        buffer.block_number = BlockNumber{};
        buffer.valid        = false;
        buffer.dirty        = false;
        buffer.refcnt       = 0;
        link_as_most_recent(&buffer);
    }

    stats       = Statistics{.hits = 0, .misses = 0, .evictions = 0, .writebacks = 0};
    initialized = true;
    return true;
}

Buffer *read(BlockNumber block_number)
{
    Buffer *buffer = find_cached(block_number);
    if (buffer == nullptr)
    {
        return nullptr;
    }

    if (buffer->valid)
    {
        stats.hits++;
        return buffer;
    }

    // 再利用したバッファなので、ここで初めてデバイスを叩く
    stats.misses++;
    if (!device.read_block(block_number.value, buffer->data.data()))
    {
        release(buffer); // 中身が無いまま掴ませない
        return nullptr;
    }
    buffer->valid = true;
    return buffer;
}

void mark_dirty(Buffer *buffer)
{
    if (buffer != nullptr)
    {
        buffer->dirty = true;
    }
}

bool write(Buffer *buffer)
{
    // 未初期化なら有効なバッファは1つも存在しないので、この判定だけで足りる
    if (buffer == nullptr || !buffer->valid)
    {
        return false;
    }
    return write_back(buffer);
}

bool flush()
{
    // 未初期化なら valid なバッファが無いので、そのまま空振りして true が返る
    bool all_succeeded = true;
    for (Buffer &buffer : buffers)
    {
        if (buffer.valid && buffer.dirty && !write_back(&buffer))
        {
            all_succeeded = false;
        }
    }
    return all_succeeded;
}

void release(Buffer *buffer)
{
    if (buffer == nullptr || buffer->refcnt <= 0)
    {
        return;
    }
    if (--buffer->refcnt > 0)
    {
        return; // まだ誰かが使っている
    }
    // 誰も使わなくなったので「最近使った」側へ移し、追い出されにくくする
    unlink(buffer);
    link_as_most_recent(buffer);
}

BufferRef acquire(BlockNumber block_number)
{
    return BufferRef{read(block_number)};
}

Statistics statistics()
{
    return stats;
}

BlockRef BlockStore::acquire(BlockNumber block_number)
{
    Buffer *buffer = BufferCache::read(block_number);
    if (buffer == nullptr)
    {
        return BlockRef(block_number, this, nullptr);
    }
    return BlockRef(block_number, this, buffer->data.data());
}

bool BlockStore::write_back(BlockNumber block_number)
{
    return BufferCache::write(lookup(block_number));
}

void BlockStore::release(BlockNumber block_number)
{
    BufferCache::release(lookup(block_number));
}


} // namespace BufferCache
