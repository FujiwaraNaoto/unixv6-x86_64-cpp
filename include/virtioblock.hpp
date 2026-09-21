#ifndef VIRTIOBLOCK_HPP
#define VIRTIOBLOCK_HPP
#include <cstdint>
#include <optional>
#include <type_traits>
#include "address.hpp"

// ─── virtio legacy レジスタオフセット (I/O空間, BAR0 からの距離) ─────
enum class VirtIORegister : uint16_t
{
    DEVICE_FEATURES = 0x00,
    DRIVER_FEATURES = 0x04,
    QUEUE_ADDRESS   = 0x08,
    QUEUE_SIZE      = 0x0C,
    QUEUE_SELECT    = 0x0E,
    QUEUE_NOTIFY    = 0x10,
    DEVICE_STATUS   = 0x12,
    // legacy virtio ではデバイス固有 config がヘッダ (20バイト) の直後から始まる。
    // virtio-blk の先頭フィールドは capacity (512バイトセクタ数, u64)。
    // I/O 空間は最大 32bit 幅なので下位と上位に分けて読む。
    CONFIG_CAPACITY_LOW  = 0x14,
    CONFIG_CAPACITY_HIGH = 0x18,
};

// ─── デバイスステータス ──────────────────────────────────────────
// NOTE: FEATURES_OK (0x08) は virtio 1.0 以降のみ。legacy では使わない。
enum class VirtIODeviceStatus : uint8_t
{
    RESET       = 0x00, // 0 を書くとデバイスがリセットされる
    ACKNOWLEDGE = 0x01,
    DRIVER      = 0x02,
    DRIVER_OK   = 0x04,
};

// ─── Descriptor フラグ ───────────────────────────────────────────
enum class VirtQueueDescriptorFlags : uint16_t
{
    NONE         = 0,
    DESC_F_NEXT  = 1,
    DESC_F_WRITE = 2,
};

// フラグ同士を | で組み合わせられるようにする (呼び出し側で static_cast しなくて済む)
constexpr VirtQueueDescriptorFlags operator|(VirtQueueDescriptorFlags a, VirtQueueDescriptorFlags b)
{
    return static_cast<VirtQueueDescriptorFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

// ─── virtio-blk リクエスト種別 ───────────────────────────────────
enum class VirtIOBlockRequestType : uint32_t
{
    VIRTIO_BLK_T_IN  = 0, // read
    VIRTIO_BLK_T_OUT = 1, // write
};

// ─── virtio-blk ステータスバイトの値 (デバイスが書く) ────────────
enum class VirtIOBlockStatus : uint8_t
{
    OK      = 0, // VIRTIO_BLK_S_OK
    IOERR   = 1, // VIRTIO_BLK_S_IOERR
    UNSUPP  = 2, // VIRTIO_BLK_S_UNSUPP
    PENDING = 0xFF, // 仕様外。ドライバが「未完了」の印として先に書いておく値
};

// enum class の中身の整数を取り出す (C++23 の std::to_underlying 相当)。
// レジスタに書くときなど、生の値が必要な箇所で static_cast を並べずに済む。
template <typename E>
constexpr std::underlying_type_t<E> to_underlying(E e)
{
    return static_cast<std::underlying_type_t<E>>(e);
}

struct [[gnu::packed]] VirtQueueDescriptor
{
    uint64_t addr;  // バッファの物理アドレス
    uint32_t len;   // バッファの長さ
    VirtQueueDescriptorFlags flags; // フラグ (DESC_F_NEXT, DESC_F_WRITE)
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
    VirtIOBlockRequestType type; // リクエストの種類 (VIRTIO_BLK_T_IN=0(read), VIRTIO_BLK_T_OUT=1(write))
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
// 変換できない (未マップ等) 場合は無効な PhysicalAddress (address が nullopt) を返すこと。
using PhysicalAddressResolver = PhysicalAddress (*)(const void *virtual_address);

// resolve_physical は初期化中にのみ呼ばれる。DMA バッファの物理アドレスは
// ここで解決してドライバ内に保持するので、以降の read/write では使わない。
bool initialize(PhysicalAddressResolver resolve_physical);
// デバイスの容量 (512バイトセクタ数)。初期化前は 0。
uint64_t capacity();

bool read_block(uint64_t sector, uint8_t *buffer);
bool write_block(uint64_t sector, const uint8_t *buffer);
} // namespace VirtIOBlock

#endif // VIRTIOBLOCK_HPP
