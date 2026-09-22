#ifndef VIRTIOBLOCK_HPP
#define VIRTIOBLOCK_HPP
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include "address.hpp"

/*
Reference: https://ozlabs.org/~rusty/virtio-spec/virtio-0.9.5.pdf
*/


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

// 2.2.2.1 Device Status
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

// vring_avail
struct [[gnu::packed]] VirtQueueAvailable
{
    uint16_t flags;  // フラグ (VIRTQ_AVAIL_F_NO_INTERRUPT)
    uint16_t idx;    // 次に使用可能なディスクリプタのインデックス
    uint16_t ring[]; // 使用可能なディスクリプタのインデックスの配列
    // この後ろ (ring[num] の位置) に used_event の uint16_t が 1 個続く。
    // 位置が実行時のキューサイズで決まるので、フィールドとしては書けない
    // (大きさ未指定の配列 ring[] は構造体の最後にしか置けない)。
    // used_event: ドライバが書き、デバイスが読む。used の idx がこの値を超えるまで
    //             割り込みを控えてもらう (VIRTIO_RING_F_EVENT_IDX を使うときだけ有効)。
};

// vring_used_elem
struct [[gnu::packed]] VirtQueueUsedElement
{
    uint32_t id;  // 使用済みディスクリプタのインデックス
    uint32_t len; // 使用済みバッファの長さ
};

// vring_used
struct [[gnu::packed]] VirtQueueUsed
{
    uint16_t flags;              // フラグ (VIRTQ_USED_F_NO_NOTIFY)
    uint16_t idx;                // 次に使用済みのディスクリプタのインデックス
    VirtQueueUsedElement ring[]; // 使用済みディスクリプタの配列
    // この後ろ (ring[num] の位置) に avail_event の uint16_t が 1 個続く。
    // avail_event: デバイスが書き、ドライバが読む。avail の idx がこの値を超えるまで
    //              ドライバからの通知 (Queue Notify) を控えてもらう
    //              (VIRTIO_RING_F_EVENT_IDX を使うときだけ有効)。
};

// virtqueue のメモリの中を指す非所有の view (3つで1組)。
// 実体は virtioblock.cpp の静的配列 virtqueue_memory 1 個で、ここはその内部を
// 指しているだけなので解放しない。
// スマートポインタにしてはいけない: new で作られていないメモリを delete する
// ことになり、しかも 1 個のバッファを 3 つが「単独所有」する形になってしまう。
// Appendix A の struct vring に相当。
struct VirtQueueView
{
    uint16_t num              = 0;       // キューのサイズ (ディスクリプタの個数。2 のべき乗)
    VirtQueueDescriptor *desc = nullptr; // Descriptor Table
    VirtQueueAvailable *avail = nullptr; // Available Ring (ドライバが書き、デバイスが読む)
    VirtQueueUsed *used       = nullptr; // Used Ring (デバイスが書き、ドライバが読む)
};

// ─── virtqueue のレイアウト (Appendix A: virtio_ring.h) ──────────
// メモリ上は次の順に並ぶ (num はキューのサイズ, align は virtio PCI では 4096)。
//
//   struct vring_desc desc[num];          // ディスクリプタ (16 バイト × num)
//   __u16 avail_flags;                    // Available Ring
//   __u16 avail_idx;
//   __u16 available[num];
//   __u16 used_event_idx;
//   char pad[];                           // 次の align 境界まで詰め物
//   __u16 used_flags;                     // Used Ring
//   __u16 used_idx;
//   struct vring_used_elem used[num];
//   __u16 avail_event_idx;
//
// NOTE: 仕様どおり、avail 側の大きさには used_event の 2 バイトを数えていない。
//       desc と avail の合計は 18 * num + 4 バイトで、num が 2 のべき乗なら
//       ちょうど align の倍数にはならないので、切り上げの余白に収まる。

// Appendix A の vring_size() に相当。virtqueue 全体に必要なバイト数を返す。
constexpr size_t vring_size(uint16_t num, size_t align)
{
    return ((sizeof(VirtQueueDescriptor) * num + sizeof(uint16_t) * (2 + num) + align - 1) & ~(align - 1))
           + sizeof(uint16_t) * 3 + sizeof(VirtQueueUsedElement) * num;
}

// Appendix A の vring_init() に相当。
// p から始まるメモリに desc / avail / used を並べ、vr がそれぞれを指すようにする。
inline void vring_init(VirtQueueView &vr, uint16_t num, uint8_t *p, uintptr_t align)
{
    vr.num   = num;
    vr.desc  = reinterpret_cast<VirtQueueDescriptor *>(p);
    vr.avail = reinterpret_cast<VirtQueueAvailable *>(p + num * sizeof(VirtQueueDescriptor));
    vr.used  = reinterpret_cast<VirtQueueUsed *>((reinterpret_cast<uintptr_t>(&vr.avail->ring[num]) + align - 1) & ~(align - 1));
}

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
