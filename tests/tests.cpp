#include "tests.hpp"

namespace tests
{

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
    // nullptr が渡されたときは、出力を捨てる既定のコンソール (kernel_main が作る) に出す
    IConsole *out = (console != nullptr) ? console : null_console;

    higher_half_check(out);
    pmm_alloc_free(out);
    heap_sbrk_alloc(out);

    // scheduler_yield(out);
    // addrspace_separation(out);
    // sleep_wakeup(out);
    fork_wait(out);

    virtio_block_read(out);
    virtio_block_read_write(out);
    inode_read_write(out);
    directory_lookup_link(out);
}

} // namespace tests
