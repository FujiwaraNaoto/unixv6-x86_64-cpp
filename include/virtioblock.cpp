#include "virtioblock.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include "io.hpp"
#include "pci.hpp"

namespace
{

// ─── PCI ID (virtio legacy block device) ─────────────────────────
// see 2.1 PCI Discovery
constexpr uint16_t VIRTIO_VENDOR_ID     = 0x1AF4;

// https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html#x1-1320002
// Transitinal PCI Device ID (0x1000) は virtio 1.0 以降のデバイスで使われる。0x1001は block deviceを示す。
constexpr uint16_t VIRTIO_BLK_DEVICE_ID = 0x1001;

constexpr uint16_t SECTOR_SIZE = 512;

// virtqueue はページ番号でデバイスに渡すため、ページ長を知っている必要がある
constexpr size_t VIRTIO_PAGE_SIZE = 4096;

// ─── DMA 対象のメモリ ────────────────────────────────────────────
// デバイスが直接読み書きするので、実体はここ (1翻訳単位) だけに置く。
// staticな.bss変数にすると、includeした翻訳単位ごとに別実体ができてしまうので、
// それをデバイスに渡すと、ドライバが読む変数とデバイスが読む変数が別物になってしまう。
// そのため、匿名 namespace に置くことで、この翻訳単位内だけで有効な実体にする。
alignas(4096) uint8_t virtqueue_memory[16384];
alignas(16) VirtIOBlockRequestHeader request_header;
alignas(512) uint8_t data_buffer[SECTOR_SIZE];
alignas(1) volatile VirtIOBlockStatus status_byte{};

// ─── ドライバ内部状態 ────────────────────────────────────────────
uint16_t io_base    = 0;
uint16_t queue_size = 0;
// virtqueue_memory の中を指す view (型は virtioblock.hpp の VirtQueueView)
VirtQueueView queue;
bool ready = false;

// DMA バッファの物理アドレス。対象はいずれもカーネル .bss 上の固定の変数で、
// そのマッピングはブート後変化しない (プロセス切り替えでも PML4 の上位エントリは
// 共有される) ため、初期化時に一度だけ解決して保持すればよい。
// これにより read/write の実行経路からアドレス変換の依存が消える。
PhysicalAddress request_header_phys;
PhysicalAddress data_buffer_phys;
PhysicalAddress status_byte_phys;

// デバイスが最後に処理し終えるはずの used->idx。
// virtq_kick() で 1 進め、queue.used->idx がこれに追いつけば完了。
uint16_t last_used_index = 0;

// ─── レジスタアクセス ────────────────────────────────────────────
uint16_t port(VirtIORegister reg)
{
    return static_cast<uint16_t>(io_base + to_underlying(reg));
}

// デバイスステータスにビットを追加する (既に立っているビットはそのまま)
void add_device_status(VirtIODeviceStatus bits)
{
    io::outb(port(VirtIORegister::DEVICE_STATUS), io::inb(port(VirtIORegister::DEVICE_STATUS)) | to_underlying(bits));
}

// ─── 初期化の各段階 ──────────────────────────────────────────────

// PCI バスから legacy virtio-blk を探し、io_base を設定する
bool find_device()
{
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
    return true;
}


// DMA バッファの物理アドレスを解決して保持する (以降は変換関数を使わない)
bool resolve_dma_buffers(VirtIOBlock::PhysicalAddressResolver resolve_physical)
{
    const PhysicalAddress header_phys = resolve_physical(&request_header);
    const PhysicalAddress buffer_phys = resolve_physical(data_buffer);
    const PhysicalAddress status_phys = resolve_physical(const_cast<const VirtIOBlockStatus *>(&status_byte));
    if (!header_phys || !buffer_phys || !status_phys)
        return false;
    request_header_phys = header_phys;
    data_buffer_phys    = buffer_phys;
    status_byte_phys    = status_phys;
    return true;
}

// 2.3 Virtqueue Configuration
// virtqueue 0 を組み立て、最後にその位置をデバイスに教える
bool virtq_init(VirtIOBlock::PhysicalAddressResolver resolve_physical)
{
    io::out16b(port(VirtIORegister::QUEUE_SELECT), 0);        // Queue Select = 0
    queue_size = io::in16b(port(VirtIORegister::QUEUE_SIZE)); // Queue Size
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
    // queue.avail->idx / queue.used->idx / last_used_index に前回の値が残っていると
    // 完了待ちが永久に抜けなくなる。.bss は起動時にゼロだが initialize() の再実行に備える。
    // (request_header は setup_request() で全フィールド代入するのでクリア不要)
    std::memset(virtqueue_memory, 0, sizeof(virtqueue_memory));
    last_used_index = 0;

    // 1.1 Virtqueues
    queue.desc  = reinterpret_cast<VirtQueueDescriptor *>(virtqueue_memory);
    queue.avail = reinterpret_cast<VirtQueueAvailable *>(virtqueue_memory + descriptor_table_size);
    queue.used  = reinterpret_cast<VirtQueueUsed *>(virtqueue_memory + used_ring_offset);

    // デバイスにはページ番号を 1 個しか渡せない = キュー全体が物理連続である前提。
    // 仮想連続でも物理連続とは限らないので確認しておく。
    const PhysicalAddress queue_phys = resolve_physical(virtqueue_memory);
    if (!queue_phys)
        return false;
    for (size_t off = VIRTIO_PAGE_SIZE; off < sizeof(virtqueue_memory); off += VIRTIO_PAGE_SIZE)
    {
        const PhysicalAddress page_phys = resolve_physical(virtqueue_memory + off);
        if (!page_phys || *page_phys.address != *queue_phys.address + off)
            return false;
    }

    // 全部解決できてからデバイスにキューの位置を教える
    // (途中で失敗して return する経路でデバイスに中途半端な設定を残さないため)
    io::out32b(port(VirtIORegister::QUEUE_ADDRESS), static_cast<uint32_t>(*queue_phys.address >> 12));
    return true;
}

// ─── リクエストの各段階 ──────────────────────────────────────────

// リクエストヘッダと 3 つのディスクリプタ (ヘッダ → データ → ステータス) を書く
void setup_request(VirtIOBlockRequestType type, uint64_t sector)
{
    request_header.type     = type;
    request_header.reserved = 0;
    request_header.sector   = sector;
    status_byte             = VirtIOBlockStatus::PENDING;

    // 2.4.1.1 Placing Buffers into The Descriptor Table

    // Descriptor 0: リクエストヘッダ (デバイスが読む)
    queue.desc[0].addr  = *request_header_phys.address;
    queue.desc[0].len   = sizeof(VirtIOBlockRequestHeader);
    // If there is a buffer element after this:
    // i. Set d.next to the index of the next free descriptor element.
    // ii. Set d.flags to indicate that there is a next descriptor (VIRTQ_DESC_F_NEXT).
    queue.desc[0].flags = VirtQueueDescriptorFlags::DESC_F_NEXT;
    queue.desc[0].next  = 1;

    // Descriptor 1: データバッファ
    //   read  → デバイスが書く (DESC_F_WRITE)
    //   write → デバイスが読む (フラグなし)
    queue.desc[1].addr  = *data_buffer_phys.address;
    queue.desc[1].len   = SECTOR_SIZE;
    queue.desc[1].flags = VirtQueueDescriptorFlags::DESC_F_NEXT | (type == VirtIOBlockRequestType::VIRTIO_BLK_T_IN ? VirtQueueDescriptorFlags::DESC_F_WRITE : VirtQueueDescriptorFlags::NONE);
    queue.desc[1].next  = 2;

    // Descriptor 2: ステータス (デバイスが書く)
    queue.desc[2].addr  = *status_byte_phys.address;
    queue.desc[2].len   = 1;
    queue.desc[2].flags = VirtQueueDescriptorFlags::DESC_F_WRITE;
    queue.desc[2].next  = 0;
}

// desc_index から始まるチェーンを Available Ring に登録し、デバイスに通知する
void virtq_kick(uint16_t desc_index)
{
    // 2.4.1 Supplying Buffers to the Device

    queue.avail->ring[queue.avail->idx % queue_size] = desc_index;
    // 4. A memory barrier should be executed to ensure the device sees the updated descriptor table and available ring before the next step
    __sync_synchronize();
    // 5. The available idx field should be increased by the number of entries added to the available ring.
    queue.avail->idx++;
    // 6. A memory barrier should be executed to ensure the device sees the updated available idx before the next step
    __sync_synchronize();

    // 7. The device should be notified that new buffers are available by writing the queue's notify offset to the device's Queue Notify register.
    io::out16b(port(VirtIORegister::QUEUE_NOTIFY), 0);
    last_used_index++;
}

// デバイスがまだ処理中か (used->idx が last_used_index に追いついていないか)
bool virtq_is_busy()
{
    // queue.used->idx は非 volatile なので、"memory" クロバーを挟まないと -O2 で
    // ポーリングループの外に読み出しが巻き上げられて無限ループになる。
    asm volatile("pause" ::: "memory");
    return queue.used->idx != last_used_index;
}

} // namespace

namespace VirtIOBlock
{

bool initialize(PhysicalAddressResolver resolve_physical)
{
    if (resolve_physical == nullptr)
        return false; // 物理アドレスが引けないので初期化できない

    if (!find_device())
        return false;

    // 2.2.1 Device Initialization Sequenceに従う

    // ─── 1. RESET ───
    // device status を 0 に書くとリセットされる。
    // Reset the device. This is not required on initial start up
    io::outb(port(VirtIORegister::DEVICE_STATUS), to_underlying(VirtIODeviceStatus::RESET));

    // ─── 2. ACKNOWLEDGE ───
    // The ACKNOWLEDGE status bit is set: we have noticed the device.
    add_device_status(VirtIODeviceStatus::ACKNOWLEDGE);

    // ─── 3. DRIVER ───
    // The DRIVER status bit is set: we know how to drive the device.
    add_device_status(VirtIODeviceStatus::DRIVER);

    // ─── 4. feature negotiation (最小構成: 何も使わない) ───
    // Device-specific setup, including reading the Device Feature Bits, discovery of virtqueues for the device, optional MSI-X setup, and reading and
    // possibly writing the virtio configuration space.
    (void)io::in32b(port(VirtIORegister::DEVICE_FEATURES)); // Device Features を読むだけ
    io::out32b(port(VirtIORegister::DRIVER_FEATURES), 0);   // Driver Features = 0


    // ─── 5. virtqueue セットアップ ───
    // The subset of Device Feature Bits understood by the driver is written to the device.
    // DMA バッファを先に解決しておく。キューの位置をデバイスに教えるのは
    // virtq_init() の最後なので、どちらで失敗してもデバイスに中途半端な設定は残らない。
    if (!resolve_dma_buffers(resolve_physical))
        return false;
    if (!virtq_init(resolve_physical))
        return false;

    // ─── 6. DRIVER_OK  ───
    // The DRIVER_OK status bit is set.
    add_device_status(VirtIODeviceStatus::DRIVER_OK);

    ready = true;
    return true;
}

// ─── I/O 共通処理 ────────────────────────────────────────────────
static bool do_request(VirtIOBlockRequestType type, uint64_t sector)
{
    if (!ready)
        return false;

    setup_request(type, sector);
    virtq_kick(0); // チェーン先頭のDescriptor番号

    // 完了待ち (ポーリング)
    while (virtq_is_busy())
    {
    }
    __sync_synchronize();

    return status_byte == VirtIOBlockStatus::OK;
}


uint64_t capacity()
{
    if (!ready)
    {
        return 0;
    }
    // 32bit ずつ 2 回に分けて読む (I/O 空間は最大 32bit 幅)
    const uint64_t low  = io::in32b(port(VirtIORegister::CONFIG_CAPACITY_LOW));
    const uint64_t high = io::in32b(port(VirtIORegister::CONFIG_CAPACITY_HIGH));
    return (high << 32) | low;
}


// 一度data_bufferに書き込んでからデバイスに渡すのは、物理アドレスがわかっている固定のバッファを経由する
// (バウンスバッファ)ことで、virtqueueのディスクリプタに渡すアドレスが常に同じになるようにするため。
bool read_block(uint64_t sector, uint8_t *buf)
{
    if (!do_request(VirtIOBlockRequestType::VIRTIO_BLK_T_IN, sector))
        return false;
    std::memcpy(buf, data_buffer, SECTOR_SIZE);
    return true;
}

bool write_block(uint64_t sector, const uint8_t *buf)
{
    std::memcpy(data_buffer, buf, SECTOR_SIZE);
    return do_request(VirtIOBlockRequestType::VIRTIO_BLK_T_OUT, sector);
}

} // namespace VirtIOBlock
