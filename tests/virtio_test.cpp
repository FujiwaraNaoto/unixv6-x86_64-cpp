#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include "tests.hpp"
#include "virtioblock.hpp"
#include "vmm.hpp"
#include "buffer_cache.hpp"

namespace tests
{
namespace
{

// バイト列を 1行16バイトの hexdump 形式で表示する (xxd / od -tx1z 風)。
//   0000: 48 45 4c 4c 4f 20 56 49 52 54 49 4f 20 42 4c 4f  |HELLO VIRTIO BLO|
// 端数行は空白で桁を揃え、印字できないバイトは '.' に置き換える。
// 1行は 6 + 16*3 + 2 + 16 + 1 = 73 桁なので VGA の 80 桁に収まる。
void hexdump(IConsole *console, const uint8_t *data, size_t size)
{
    constexpr size_t COLUMNS = 16;

    for (size_t offset = 0; offset < size; offset += COLUMNS)
    {
        const size_t line_length = (size - offset < COLUMNS) ? (size - offset) : COLUMNS;

        console->printf("%04zx: ", offset);

        for (size_t i = 0; i < COLUMNS; i++)
        {
            if (i < line_length)
                console->printf("%02x ", static_cast<unsigned>(data[offset + i]));
            else
                console->puts("   ");
        }

        console->puts(" |");
        for (size_t i = 0; i < line_length; i++)
        {
            const char c = static_cast<char>(data[offset + i]);
            console->putchar((c >= 0x20 && c < 0x7F) ? c : '.');
        }
        console->puts("|\n");
    }
}

// 1 セクタの大きさ (virtio-blk は 512 バイト単位で読み書きする)
constexpr size_t SECTOR_BYTES = 512;

// "[VIRTIO] <name>: OK" / "FAILED" を 1 行で表示する
void report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[VIRTIO] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "FAILED");
}

// 2 つのセクタの中身が同じか。
// std::array の == は memcmp を呼ぶが、カーネルには memcmp の実体が無いので自前で比べる。
bool same_sector(const std::array<uint8_t, SECTOR_BYTES> &a, const std::array<uint8_t, SECTOR_BYTES> &b)
{
    for (size_t i = 0; i < SECTOR_BYTES; i++)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

} // namespace

void virtio_block_read(IConsole *console)
{
    // 仮想 → 物理の変換方法はカーネル側の関心事なので、ドライバには関数として渡す。
    // (キャプチャなしラムダは関数ポインタへ暗黙変換される)
    const auto resolve_physical = [](const void *virtual_address) -> PhysicalAddress
    {
        if (vmm::vmm_ptr == nullptr)
        {
            return PhysicalAddress{}; // VMM 未初期化: 変換できない
        }
        // virtual_to_physical() 自体も未マップなら無効な PhysicalAddress を返すので、そのまま伝播させる。
        return vmm::vmm_ptr->virtual_to_physical(PageVirtualAddress{reinterpret_cast<uint64_t>(virtual_address)});
    };

    if (!VirtIOBlock::initialize(resolve_physical))
    {
        console->set_color(Color::LightRed, Color::Black);
        console->puts("[VIRTIO] VirtIO Block Device initialization failed\n");
        console->set_color(Color::LightGrey, Color::Black);
        return;
    }

    console->set_color(Color::LightGreen, Color::Black);
    console->puts("[VIRTIO] VirtIO Block Device initialized successfully\n");
    console->set_color(Color::LightGrey, Color::Black);

    static std::array<uint8_t, 512> buffer;
    if (VirtIOBlock::read_block(0, buffer.data()))
    {
        console->set_color(Color::LightGreen, Color::Black);
        console->puts("[VIRTIO] Read block 0 successfully\n");
        console->set_color(Color::LightGrey, Color::Black);
        console->puts("[VIRTIO] Block 0 data:\n");
        console->set_color(Color::LightCyan, Color::Black);
        hexdump(console, buffer.data(), buffer.size());
        console->set_color(Color::LightGrey, Color::Black);
    }

    // バッファキャッシュ層をこのドライバの上に載せる。
    // 以降ブロックアクセスは BufferCache 経由で行い、上位層 (inode) からは
    // どのドライバかを見えなくする。
    // コンストラクタが初期化を行う。kernel_main は返らないので、この
    // インスタンスは以降の全アクセスより長生きする。
    BufferCache::Manager buffer_cache(BufferCache::BlockDevice{
        .read_block  = &VirtIOBlock::read_block,
        .write_block = &VirtIOBlock::write_block,
    });

    if (buffer_cache.valid())
    {
        console->set_color(Color::LightGreen, Color::Black);
        console->puts("[BCACHE] ");
        console->set_color(Color::LightGrey, Color::Black);
        console->printf("initialized: %llu buffers x %llu bytes\n",
                        static_cast<unsigned long long>(NBUF),
                        static_cast<unsigned long long>(BLOCK_SIZE));

        // 1回目: キャッシュに無いのでデバイスを叩く (miss)
        // BufferRef はスコープを抜けるときに自動で release() される
        if (BufferCache::BufferRef block0 = BufferCache::acquire(0))
        {
            console->set_color(Color::LightGreen, Color::Black);
            console->puts("[BCACHE] ");
            console->set_color(Color::LightGrey, Color::Black);
            console->puts("block 0:\n");
            console->set_color(Color::LightCyan, Color::Black);
            hexdump(console, block0->data.data(), block0->data.size());
            console->set_color(Color::LightGrey, Color::Black);
            block0.reset(); // 明示的に手放す (以降のスコープでも自動解放される)

            // 2回目: 同じブロックなのでデバイスを叩かない (hit)
            {
                BufferCache::BufferRef again = BufferCache::acquire(0);
            }

            // RAII が効いていることの確認:
            // release 漏れがあれば NBUF 個を超えた時点で acquire が失敗する
            bool no_leak = true;
            for (int i = 0; i < NBUF * 4; i++)
            {
                BufferCache::BufferRef probe = BufferCache::acquire(0);
                if (!probe)
                {
                    no_leak = false;
                    break;
                }
            }
            console->set_color(Color::LightGreen, Color::Black);
            console->puts("[BCACHE] ");
            console->set_color(Color::LightGrey, Color::Black);
            console->printf("BufferRef leak test: %s\n", no_leak ? "OK" : "LEAKED");

            const BufferCache::Statistics stats = BufferCache::statistics();
            console->set_color(Color::LightGreen, Color::Black);
            console->puts("[BCACHE] ");
            console->set_color(Color::LightGrey, Color::Black);
            console->printf("hits=%llu misses=%llu evictions=%llu writebacks=%llu\n",
                            static_cast<unsigned long long>(stats.hits),
                            static_cast<unsigned long long>(stats.misses),
                            static_cast<unsigned long long>(stats.evictions),
                            static_cast<unsigned long long>(stats.writebacks));
        }
        else
        {
            console->set_color(Color::LightRed, Color::Black);
            console->puts("[BCACHE] acquire(0) failed\n");
            console->set_color(Color::LightGrey, Color::Black);
        }
    }
}

void virtio_block_read_write(IConsole *console)
{
    const uint64_t capacity = VirtIOBlock::capacity();
    if (capacity == 0)
    {
        report(console, "read/write test (device not initialized)", false);
        return;
    }

    // 1. 連続した読み込み
    // Available Ring はキューのサイズ個で一周する (QEMU の既定では 256)。
    // それより多く出して、ring の添字が折り返した後も完了を待てることを確かめる。
    constexpr int READ_COUNT = 1024;
    static std::array<uint8_t, SECTOR_BYTES> buffer;
    bool reads_ok = true;
    for (int i = 0; i < READ_COUNT; i++)
    {
        if (!VirtIOBlock::read_block(static_cast<uint64_t>(i % 8), buffer.data()))
        {
            reads_ok = false;
            break;
        }
    }
    report(console, "1024 sequential reads", reads_ok);

    // 2. 書き込み → 読み戻し
    // 書き換えるのは最後のセクタ。ファイルシステムは空きブロックを先頭から使うので、
    // ここが使われるのはディスクがほぼ埋まったときだけ。
    // BufferCache を通さずにドライバを直接呼ぶので、キャッシュの中身と食い違わないよう
    // 最後に必ず元の中身へ戻す。
    const uint64_t sector = capacity - 1;
    static std::array<uint8_t, SECTOR_BYTES> original;
    static std::array<uint8_t, SECTOR_BYTES> pattern;
    static std::array<uint8_t, SECTOR_BYTES> readback;

    if (!VirtIOBlock::read_block(sector, original.data()))
    {
        report(console, "read last sector", false);
        return;
    }

    // 元の中身と必ず違う値にする (同じだと書き込めていなくても一致してしまう)
    for (size_t i = 0; i < SECTOR_BYTES; i++)
    {
        pattern[i] = static_cast<uint8_t>(~original[i]);
    }

    readback.fill(0);
    const bool write_ok = VirtIOBlock::write_block(sector, pattern.data()) &&
                          VirtIOBlock::read_block(sector, readback.data()) && same_sector(readback, pattern);
    report(console, "write + read back", write_ok);

    // 3. 元に戻す
    readback.fill(0);
    const bool restore_ok = VirtIOBlock::write_block(sector, original.data()) &&
                            VirtIOBlock::read_block(sector, readback.data()) && same_sector(readback, original);
    report(console, "restore original", restore_ok);
}

} // namespace tests
