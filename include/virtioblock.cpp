#include "virtioblock.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include "io.hpp"
#include "pci.hpp"

namespace
{

// ─── virtio legacy レジスタオフセット (I/O空間) ──────────────────
constexpr uint16_t REG_DEVICE_FEATURES = 0x00;
constexpr uint16_t REG_DRIVER_FEATURES = 0x04;
constexpr uint16_t REG_QUEUE_ADDRESS   = 0x08;
constexpr uint16_t REG_QUEUE_SIZE      = 0x0C;
constexpr uint16_t REG_QUEUE_SELECT    = 0x0E;
constexpr uint16_t REG_QUEUE_NOTIFY    = 0x10;
constexpr uint16_t REG_DEVICE_STATUS   = 0x12;

// ─── ステータスビット ────────────────────────────────────────────
// NOTE: FEATURES_OK (0x08) は virtio 1.0 以降のみ。legacy では使わない。
constexpr uint8_t STATUS_ACKNOWLEDGE = 0x01;
constexpr uint8_t STATUS_DRIVER      = 0x02;
constexpr uint8_t STATUS_DRIVER_OK   = 0x04;

// ─── Descriptor フラグ ───────────────────────────────────────────
constexpr uint16_t DESC_F_NEXT  = 1;
constexpr uint16_t DESC_F_WRITE = 2;

// ─── virtio-blk リクエスト種別 ───────────────────────────────────
constexpr uint32_t BLK_T_IN  = 0; // read
constexpr uint32_t BLK_T_OUT = 1; // write

// ─── PCI ID (virtio legacy block device) ─────────────────────────
constexpr uint16_t VIRTIO_VENDOR_ID     = 0x1AF4;
constexpr uint16_t VIRTIO_BLK_DEVICE_ID = 0x1001;

constexpr uint16_t SECTOR_SIZE = 512;

// virtqueue はページ番号でデバイスに渡すため、ページ長を知っている必要がある
constexpr size_t VIRTIO_PAGE_SIZE = 4096;

// ─── DMA 対象のメモリ ────────────────────────────────────────────
// デバイスが直接読み書きするので、実体はここ (1翻訳単位) だけに置く。
alignas(4096) uint8_t virtqueue_memory[16384];
alignas(16) VirtIOBlockRequestHeader request_header;
alignas(512) uint8_t data_buffer[SECTOR_SIZE];
alignas(1) volatile uint8_t status_byte = 0;

// ─── ドライバ内部状態 ────────────────────────────────────────────
uint16_t io_base          = 0;
uint16_t queue_size       = 0;
VirtQueueDescriptor *desc = nullptr;
VirtQueueAvailable *avail = nullptr;
VirtQueueUsed *used       = nullptr;
bool ready                = false;

// DMA バッファの物理アドレス。対象はいずれもカーネル .bss 上の固定の変数で、
// そのマッピングはブート後変化しない (プロセス切り替えでも PML4 の上位エントリは
// 共有される) ため、初期化時に一度だけ解決して保持すればよい。
// これにより read/write の実行経路からアドレス変換の依存が消える。
uint64_t request_header_phys = 0;
uint64_t data_buffer_phys    = 0;
uint64_t status_byte_phys    = 0;

uint16_t port(uint16_t offset)
{
    return static_cast<uint16_t>(io_base + offset);
}

} // namespace

namespace VirtIOBlock
{

bool initialize(PhysicalAddressResolver resolve_physical)
{
    if (resolve_physical == nullptr)
        return false; // 物理アドレスが引けないので初期化できない

    auto found = PCI::pci_device_exists(VIRTIO_VENDOR_ID, VIRTIO_BLK_DEVICE_ID);
    if (!found)
        return false;
    const PCIDevice &dev = *found;

    // BAR0 は I/O 空間でなければ legacy virtio ではない
    if (!dev.bars[0].is_io_space)
        return false;
    io_base = static_cast<uint16_t>(dev.bars[0].base_address & 0xFFFC);
    if (io_base == 0)
        return false;

    PCI::enable_bus_master(dev); // DMA を行うので Bus Master を有効化

    // ─── 1. リセット ───
    io::outb(port(REG_DEVICE_STATUS), 0);

    // ─── 2. ACKNOWLEDGE ───
    io::outb(port(REG_DEVICE_STATUS), STATUS_ACKNOWLEDGE);

    // ─── 3. DRIVER ───
    io::outb(port(REG_DEVICE_STATUS), STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    // ─── 4. feature negotiation (最小構成: 何も使わない) ───
    (void)io::in32b(port(REG_DEVICE_FEATURES)); // Device Features を読むだけ
    io::out32b(port(REG_DRIVER_FEATURES), 0);   // Driver Features = 0

    // ─── 5. virtqueue セットアップ ───
    io::out16b(port(REG_QUEUE_SELECT), 0);        // Queue Select = 0
    queue_size = io::in16b(port(REG_QUEUE_SIZE)); // Queue Size
    if (queue_size == 0)
        return false;

    // virtio spec 2.6 "Split Virtqueues" のレイアウト。
    // avail / used には ring の後ろに used_event / avail_event の uint16_t が
    // 1 個ずつ付く (EVENT_IDX を使わなくても場所は確保する)。
    size_t descriptor_table_size = queue_size * sizeof(VirtQueueDescriptor);
    size_t available_ring_size   = sizeof(VirtQueueAvailable) + (queue_size + 1) * sizeof(uint16_t);
    size_t used_ring_offset      = (descriptor_table_size + available_ring_size + 4095) & ~4095UL;
    size_t used_size             = sizeof(VirtQueueUsed) + queue_size * sizeof(VirtQueueUsedElement) + sizeof(uint16_t);
    size_t total_size            = used_ring_offset + used_size;

    if (total_size > sizeof(virtqueue_memory))
        return false;

    // virtqueue 全体をゼロクリアする。
    // デバイスはリセット時に自分の used->idx を 0 に戻すので、こちら側の
    // avail->idx / used->idx に前回の値が残っていると do_request() の完了待ちが
    // 永久に抜けなくなる。.bss は起動時にゼロだが initialize() の再実行に備える。
    // (request_header は do_request() で全フィールド代入するのでクリア不要)
    std::memset(virtqueue_memory, 0, sizeof(virtqueue_memory));

    // ポインタを組み立てる
    desc  = reinterpret_cast<VirtQueueDescriptor *>(virtqueue_memory);
    avail = reinterpret_cast<VirtQueueAvailable *>(virtqueue_memory + descriptor_table_size);
    used  = reinterpret_cast<VirtQueueUsed *>(virtqueue_memory + used_ring_offset);

    // デバイスにはページ番号を 1 個しか渡せない = キュー全体が物理連続である前提。
    // 仮想連続でも物理連続とは限らないので確認しておく。
    const std::optional<uint64_t> queue_phys = resolve_physical(virtqueue_memory);
    if (!queue_phys)
        return false;
    for (size_t off = VIRTIO_PAGE_SIZE; off < sizeof(virtqueue_memory); off += VIRTIO_PAGE_SIZE)
    {
        const std::optional<uint64_t> page_phys = resolve_physical(virtqueue_memory + off);
        if (!page_phys || *page_phys != *queue_phys + off)
            return false;
    }

    // DMA バッファの物理アドレスもここで解決しておく (以降は変換関数を使わない)
    const std::optional<uint64_t> header_phys = resolve_physical(&request_header);
    const std::optional<uint64_t> buffer_phys = resolve_physical(data_buffer);
    const std::optional<uint64_t> status_phys = resolve_physical(const_cast<const uint8_t *>(&status_byte));
    if (!header_phys || !buffer_phys || !status_phys)
        return false;
    request_header_phys = *header_phys;
    data_buffer_phys    = *buffer_phys;
    status_byte_phys    = *status_phys;

    // 全部解決できてからデバイスにキューの位置を教える
    // (途中で失敗して return する経路でデバイスに中途半端な設定を残さないため)
    io::out32b(port(REG_QUEUE_ADDRESS), static_cast<uint32_t>(*queue_phys >> 12));

    // ─── 6. DRIVER_OK (最後) ───
    io::outb(port(REG_DEVICE_STATUS), STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_DRIVER_OK);

    ready = true;
    return true;
}

// ─── I/O 共通処理 ────────────────────────────────────────────────
static bool do_request(uint32_t type, uint64_t sector)
{
    if (!ready)
        return false;
    request_header.type     = type;
    request_header.reserved = 0;
    request_header.sector   = sector;
    status_byte             = 0xFF; // 未完了マーカー

    // Descriptor 0: リクエストヘッダ (デバイスが読む)
    desc[0].addr  = request_header_phys;
    desc[0].len   = sizeof(VirtIOBlockRequestHeader);
    desc[0].flags = DESC_F_NEXT;
    desc[0].next  = 1;

    // Descriptor 1: データバッファ
    //   read  → デバイスが書く (DESC_F_WRITE)
    //   write → デバイスが読む (フラグなし)
    desc[1].addr  = data_buffer_phys;
    desc[1].len   = SECTOR_SIZE;
    desc[1].flags = static_cast<uint16_t>(DESC_F_NEXT | (type == BLK_T_IN ? DESC_F_WRITE : 0));
    desc[1].next  = 2;

    // Descriptor 2: ステータス (デバイスが書く)
    desc[2].addr  = status_byte_phys;
    desc[2].len   = 1;
    desc[2].flags = DESC_F_WRITE;
    desc[2].next  = 0;

    // Available Ring に登録
    uint16_t last_used                   = used->idx;
    avail->ring[avail->idx % queue_size] = 0; // チェーン先頭のDescriptor番号
    __sync_synchronize();                     // メモリバリア
    avail->idx++;
    __sync_synchronize();

    // デバイスに通知
    io::out16b(port(REG_QUEUE_NOTIFY), 0);

    // 完了待ち (ポーリング)。
    // used->idx は非 volatile なので、"memory" クロバーを挟まないと -O2 で
    // ループ外に読み出しが巻き上げられて無限ループになる。
    while (true)
    {
        asm volatile("pause" ::: "memory");
        if (used->idx != last_used)
            break;
    }
    __sync_synchronize();

    return status_byte == 0;
}


bool read_block(uint64_t sector, uint8_t *buf)
{
    if (!do_request(BLK_T_IN, sector))
        return false;
    std::memcpy(buf, data_buffer, SECTOR_SIZE);
    return true;
}

bool write_block(uint64_t sector, const uint8_t *buf)
{
    std::memcpy(data_buffer, buf, SECTOR_SIZE);
    return do_request(BLK_T_OUT, sector);
}

} // namespace VirtIOBlock
