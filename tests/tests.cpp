#include "tests.hpp"

namespace tests
{
namespace
{

// run_all() に nullptr が渡されたときの出力先 (何もしない)
NullConsole null_console;

} // namespace

// 実行するテストの選択。
// 追加・削除はここだけで済むよう、kernel_main からは run_all() のみを呼ぶ。
//
// NOTE: 以下は「呼ぶと戻ってこない」テストなので、run_all() には入れず
//       必要なときだけ個別に呼ぶこと。
//         - usermode_ring3() : リング3へ遷移したまま戻らない
//         - keyboard_echo()  : 入力エコーの無限ループ
//         - division_by_zero() : #DE ハンドラ次第で復帰しない
void run_all(IConsole *console)
{
    IConsole *out = (console != nullptr) ? console : &null_console;

    higher_half_check(out);
    pmm_alloc_free(out);
    heap_sbrk_alloc(out);

    // scheduler_yield(out);
    // addrspace_separation(out);
    // sleep_wakeup(out);
    fork_wait(out);

    virtio_block_read(out);
}

} // namespace tests
