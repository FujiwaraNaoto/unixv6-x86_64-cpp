#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include "tests.hpp"
#include "virtioblock.hpp"
#include "vmm.hpp"
#include "vga.hpp"
#include "buffer_cache.hpp"

namespace tests
{
namespace
{

// バイト列を 1行16バイトの hexdump 形式で表示する (xxd / od -tx1z 風)。
//   0000: 48 45 4c 4c 4f 20 56 49 52 54 49 4f 20 42 4c 4f  |HELLO VIRTIO BLO|
// 端数行は空白で桁を揃え、印字できないバイトは '.' に置き換える。
// 1行は 6 + 16*3 + 2 + 16 + 1 = 73 桁なので VGA の 80 桁に収まる。
void hexdump(const uint8_t *data, size_t size)
{
    constexpr size_t COLUMNS = 16;

    for (size_t offset = 0; offset < size; offset += COLUMNS)
    {
        const size_t line_length = (size - offset < COLUMNS) ? (size - offset) : COLUMNS;

        // 自前 printf の可変長引数は unsigned long long として取り出されるので、
        // 呼び出し側で 64bit に揃えておく。
        vga::vga->printf("%04x: ", static_cast<unsigned long long>(offset));

        for (size_t i = 0; i < COLUMNS; i++)
        {
            if (i < line_length)
                vga::vga->printf("%02x ", static_cast<unsigned long long>(data[offset + i]));
            else
                vga::vga->puts("   ");
        }

        vga::vga->puts(" |");
        for (size_t i = 0; i < line_length; i++)
        {
            const char c = static_cast<char>(data[offset + i]);
            vga::vga->putchar((c >= 0x20 && c < 0x7F) ? c : '.');
        }
        vga::vga->puts("|\n");
    }
}

} // namespace

void virtio_block_read()
{
    // 仮想 → 物理の変換方法はカーネル側の関心事なので、ドライバには関数として渡す。
    // (キャプチャなしラムダは関数ポインタへ暗黙変換される)
    const auto resolve_physical = [](const void *p) -> std::optional<uint64_t>
    {
        if (vmm::vmm_ptr == nullptr)
        {
            return std::nullopt; // VMM 未初期化: 変換できない
        }
        // virtual_to_physical() 自体も未マップなら nullopt を返すので、そのまま伝播させる
        return vmm::vmm_ptr->virtual_to_physical(reinterpret_cast<uint64_t>(p));
    };

    if (!VirtIOBlock::initialize(resolve_physical))
    {
        vga::vga->set_color(Color::LightRed, Color::Black);
        vga::vga->puts("[VIRTIO] VirtIO Block Device initialization failed\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        return;
    }

    vga::vga->set_color(Color::LightGreen, Color::Black);
    vga::vga->puts("[VIRTIO] VirtIO Block Device initialized successfully\n");
    vga::vga->set_color(Color::LightGrey, Color::Black);

    static std::array<uint8_t, 512> buffer;
    if (VirtIOBlock::read_block(0, buffer.data()))
    {
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->puts("[VIRTIO] Read block 0 successfully\n");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        vga::vga->puts("[VIRTIO] Block 0 data:\n");
        vga::vga->set_color(Color::LightCyan, Color::Black);
        hexdump(buffer.data(), buffer.size());
        vga::vga->set_color(Color::LightGrey, Color::Black);
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
        vga::vga->set_color(Color::LightGreen, Color::Black);
        vga::vga->puts("[BCACHE] ");
        vga::vga->set_color(Color::LightGrey, Color::Black);
        vga::vga->printf("initialized: %u buffers x %u bytes\n",
                         static_cast<unsigned long long>(NBUF),
                         static_cast<unsigned long long>(BLOCK_SIZE));

        // 1回目: キャッシュに無いのでデバイスを叩く (miss)
        // BufferRef はスコープを抜けるときに自動で release() される
        if (BufferCache::BufferRef block0 = BufferCache::acquire(0))
        {
            vga::vga->set_color(Color::LightGreen, Color::Black);
            vga::vga->puts("[BCACHE] ");
            vga::vga->set_color(Color::LightGrey, Color::Black);
            vga::vga->puts("block 0:\n");
            vga::vga->set_color(Color::LightCyan, Color::Black);
            hexdump(block0->data, BLOCK_SIZE);
            vga::vga->set_color(Color::LightGrey, Color::Black);
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
            vga::vga->set_color(Color::LightGreen, Color::Black);
            vga::vga->puts("[BCACHE] ");
            vga::vga->set_color(Color::LightGrey, Color::Black);
            vga::vga->printf("BufferRef leak test: %s\n", no_leak ? "OK" : "LEAKED");

            const BufferCache::Statistics stats = BufferCache::statistics();
            vga::vga->set_color(Color::LightGreen, Color::Black);
            vga::vga->puts("[BCACHE] ");
            vga::vga->set_color(Color::LightGrey, Color::Black);
            vga::vga->printf("hits=%u misses=%u evictions=%u writebacks=%u\n",
                             static_cast<unsigned long long>(stats.hits),
                             static_cast<unsigned long long>(stats.misses),
                             static_cast<unsigned long long>(stats.evictions),
                             static_cast<unsigned long long>(stats.writebacks));
        }
        else
        {
            vga::vga->set_color(Color::LightRed, Color::Black);
            vga::vga->puts("[BCACHE] acquire(0) failed\n");
            vga::vga->set_color(Color::LightGrey, Color::Black);
        }
    }
}

} // namespace tests
