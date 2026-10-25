#include <array>
#include <cstdint>
#include "tests.hpp"
#include "io.hpp"

namespace tests
{
namespace
{

// isa-debug-exit (-device isa-debug-exit,iobase=0xf4,iosize=0x04) に書くと QEMU が終了する。
// 終了コードは (書いた値 << 1) | 1 になるので、0x10 -> 33 (成功) / 0x11 -> 35 (失敗)。
// デバイスを付けずに起動した場合は未使用ポートへの書き込みになるだけで何も起きず、
// kernel_main の hlt ループへそのまま進む (make tests などの対話実行は従来どおり)。
constexpr uint16_t kQemuExitPort    = 0xf4;
constexpr uint32_t kQemuExitSuccess = 0x10;
constexpr uint32_t kQemuExitFailure = 0x11;

void qemu_exit(uint32_t code)
{
    io::out32b(kQemuExitPort, code);
}

struct TestCase
{
    const char *name;
    bool (*run)(IConsole *console);
};

// run_all() で実行するテスト。追加・削除はここだけで済む。
// NOTE: 下の「呼ぶと戻ってこない」テストはここに入れられない (ONLY= で個別に起動する)。
constexpr std::array kTests{
    TestCase{"higher_half_check", higher_half_check},
    TestCase{"pmm_alloc_free", pmm_alloc_free},
    TestCase{"heap_sbrk_alloc", heap_sbrk_alloc},
    TestCase{"division_by_zero", division_by_zero},
    TestCase{"scheduler_yield", scheduler_yield},
    TestCase{"addrspace_separation", addrspace_separation},
    TestCase{"sleep_wakeup", sleep_wakeup},
    TestCase{"usermode_ring3", usermode_ring3},
    TestCase{"fork_from_kernel", fork_from_kernel},
    TestCase{"fork_from_ring3", fork_from_ring3},
    TestCase{"process_free", process_free},
    TestCase{"virtio_block_read", virtio_block_read},
    TestCase{"virtio_block_read_write", virtio_block_read_write},
};

} // namespace

// 実行するテストの選択。
// 追加・削除はここだけで済むよう、kernel_main からは run_all() のみを呼ぶ。
//
// NOTE: 以下は「呼ぶと戻ってこない」テストなので、run_all() には入れず
//       必要なときだけ個別に呼ぶこと。
//         - keyboard_echo()  : 入力エコーの無限ループ
void run_all(IConsole *console)
{
    // nullptr が渡されたときは、出力を捨てる既定のコンソール (kernel_main が作る) に出す
    IConsole *out = (console != nullptr) ? console : null_console;

#ifdef TEST_ONLY
    // ONLY=<テスト名> を付けてビルドしたとき (Makefile 参照)。上の NOTE にある
    // 「呼ぶと戻ってこない」テストを、tests.cpp を書き換えずに 1 つだけ走らせる。
    TEST_ONLY(out);
#else
    // 合否は各テストが返す。ここでは集計して 1 行にまとめ、QEMU の終了コードにする。
    unsigned passed = 0;
    unsigned failed = 0;
    for (const TestCase &test : kTests)
    {
        const bool ok = test.run(out);
        (ok ? passed : failed)++;
        out->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
        out->printf("[TEST] %s: %s\n", test.name, ok ? "OK" : "FAILED");
        out->set_color(Color::LightGrey, Color::Black);
    }

    out->printf("[TEST] %u passed, %u failed\n", passed, failed);
    qemu_exit(failed == 0 ? kQemuExitSuccess : kQemuExitFailure);
#endif
}

} // namespace tests
