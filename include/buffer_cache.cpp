#include "buffer_cache.hpp"

namespace
{

Buffer buffers[NBUF];

// LRU リストの番兵。
//   head.next … 最近使ったバッファ (MRU)
//   head.prev … 最も長く使っていないバッファ (LRU)
// 再利用はこの LRU 側から探す。
//
// 静的初期化の時点で自分自身を指す = 「空のリスト」にしておく。こうすると
// Manager を作る前に acquire() が呼ばれても、リスト走査が空振りして nullptr が
// 返るだけで済む。各関数で初期化済みフラグを見る必要が無くなる。
// (自身のアドレスは定数式なので .init_array ではなく .data に畳まれる)
Buffer head = {
    .blockno = 0,
    .valid   = false,
    .dirty   = false,
    .refcnt  = 0,
    .prev    = &head,
    .next    = &head,
    .data    = {},
};

BufferCache::BlockDevice device{}; // 未初期化状態では両方 nullptr
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
    if (!device.write_block(buffer->blockno, buffer->data))
    {
        return false;
    }
    buffer->dirty = false;
    stats.writebacks++;
    return true;
}

// blockno に対応するバッファを確保する (xv6 の bget 相当)。
// キャッシュに有ればそれを、無ければ LRU 側の未使用バッファを再利用する。
// 戻り値は refcnt を 1 増やした状態。再利用した場合は valid=false で返るので、
// 呼び出し側がデバイスから読み込む責任を持つ。
// blockno のバッファを探す。見つからなければ nullptr。refcnt は変えない。
Buffer *find_cached(uint32_t blockno)
{
    for (Buffer *b = head.next; b != &head; b = b->next)
    {
        if (b->valid && b->blockno == blockno)
        {
            return b;
        }
    }
    return nullptr;
}

Buffer *find_or_recycle(uint32_t blockno)
{
    // 1. 既にキャッシュに載っているか
    if (Buffer *cached = find_cached(blockno))
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
        b->blockno = blockno;
        b->valid   = false;
        b->dirty   = false;
        b->refcnt  = 1;
        return b;
    }

    // 3. 全バッファが使用中 = release() 漏れ。上位層のバグ。
    return nullptr;
}

} // namespace

namespace BufferCache
{

Manager::Manager(const BlockDevice &block_device)
{
    if (block_device.read_block == nullptr || block_device.write_block == nullptr)
    {
        return; // valid_ は false のまま
    }
    device = block_device;

    head.next = &head;
    head.prev = &head;
    for (Buffer &buffer : buffers)
    {
        buffer.blockno = 0;
        buffer.valid   = false;
        buffer.dirty   = false;
        buffer.refcnt  = 0;
        link_as_most_recent(&buffer);
    }

    stats  = Statistics{.hits = 0, .misses = 0, .evictions = 0, .writebacks = 0};
    valid_ = true;
}

Buffer *read(uint32_t blockno)
{
    Buffer *buffer = find_or_recycle(blockno);
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
    if (!device.read_block(blockno, buffer->data))
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

BufferRef acquire(uint32_t blockno)
{
    return BufferRef{read(blockno)};
}

// ─── IBlockStore の実装 ──────────────────────────────────────────
// BlockRef は Buffer* ではなくブロック番号を持つので、write_back / release は
// キャッシュ内を引き直す。acquire 済み = refcnt > 0 なので追い出されておらず、
// 必ず同じバッファが見つかる。

BlockRef BlockStore::acquire(uint32_t blockno)
{
    Buffer *buffer = BufferCache::read(blockno); // refcnt を 1 増やして返る
    if (buffer == nullptr)
    {
        return BlockRef{};
    }
    return BlockRef{this, blockno, buffer->data};
}

bool BlockStore::write_back(uint32_t blockno)
{
    Buffer *buffer = find_cached(blockno);
    return buffer != nullptr && BufferCache::write(buffer);
}

void BlockStore::release(uint32_t blockno)
{
    if (Buffer *buffer = find_cached(blockno))
    {
        BufferCache::release(buffer);
    }
}

Statistics statistics()
{
    return stats;
}

} // namespace BufferCache
