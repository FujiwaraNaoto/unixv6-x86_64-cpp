#ifndef VIRTIOBLOCK_HPP
#define VIRTIOBLOCK_HPP
#include <cstdint>


struct [[gnu::packed]] VirtQueueDescriptor
{
    uint64_t addr;  // バッファの物理アドレス
    uint32_t len;   // バッファの長さ
    uint16_t flags; // フラグ (VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE)
    uint16_t next; // 次のディスクリプタのインデックス (flags に NEXT が立っている場合のみ有効)
};

struct [[gnu::packed]] VirtQueueAvailable
{
    uint16_t flags;  // フラグ (VIRTQ_AVAIL_F_NO_INTERRUPT)
    uint16_t idx;    // 次に使用可能なディスクリプタのインデックス
    uint16_t ring[]; // 使用可能なディスクリプタのインデックスの配列
                     // (この後ろに used_event の uint16_t が 1 個続く)
};

struct [[gnu::packed]] VirtQueueUsedElement
{
    uint32_t id;  // 使用済みディスクリプタのインデックス
    uint32_t len; // 使用済みバッファの長さ
};

struct [[gnu::packed]] VirtQueueUsed
{
    uint16_t flags;              // フラグ (VIRTQ_USED_F_NO_NOTIFY)
    uint16_t idx;                // 次に使用済みのディスクリプタのインデックス
    VirtQueueUsedElement ring[]; // 使用済みディスクリプタの配列
                                 // (この後ろに avail_event の uint16_t が 1 個続く)
};

struct [[gnu::packed]] VirtIOBlockRequestHeader
{
    uint32_t type; // リクエストの種類 (VIRTIO_BLK_T_IN=0(read), VIRTIO_BLK_T_OUT=1(write), VIRTIO_BLK_T_FLUSH=4)
    uint32_t reserved; // 予約領域 (0で埋める)
    uint64_t sector;   // セクタ番号 (512バイト単位)
};

// NOTE: DMA 対象のバッファ (リクエストヘッダ / データ / ステータス) の実体は
//       virtioblock.cpp の匿名 namespace に置いている。ヘッダで static 定義すると
//       include した翻訳単位ごとに別実体ができ、デバイスに渡した物理アドレスと
//       ドライバが読む変数が別物になる。

namespace VirtIOBlock
{

// 仮想アドレス → 物理アドレスの変換関数。
// デバイスは MMU を通らないので DMA 先は物理アドレスで渡す必要があるが、
// その変換方法 (ページテーブルを歩く / direct map から引く 等) はドライバの
// 関心事ではない。実装を知らずに済むよう初期化時に呼び出し側から受け取る。
using PhysicalAddressResolver = uint64_t (*)(const void *virtual_address);

// resolve_physical は初期化中にのみ呼ばれる。DMA バッファの物理アドレスは
// ここで解決してドライバ内に保持するので、以降の read/write では使わない。
bool initialize(PhysicalAddressResolver resolve_physical);
bool read_block(uint64_t sector, uint8_t *buffer);
bool write_block(uint64_t sector, const uint8_t *buffer);
} // namespace VirtIOBlock

#endif // VIRTIOBLOCK_HPP
