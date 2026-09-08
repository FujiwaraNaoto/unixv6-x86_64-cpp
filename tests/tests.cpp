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
void run_all()
{
    higher_half_check();
    pmm_alloc_free();
    heap_sbrk_alloc();

    // scheduler_yield();
    // addrspace_separation();
    // sleep_wakeup();
    fork_wait();

    virtio_block_read();
}

} // namespace tests
