#pragma once

#include "console.hpp"

// カーネル各機能の動作確認テスト。
//
// kernel_main は「ハードウェアとカーネルサブシステムを初期化して hlt ループに
// 入る」ことだけを担当する。個々の機能が期待どおり動くかを確かめるコードは
// 全てこの namespace に集約し、実行するテストの選択は run_all() で行う。
//
// 各テストは kernel_main の初期化 (pmm / vmm / heap / process など) が
// 完了していることを前提とし、グローバルポインタ (pmm::pmm_ptr,
// heap::heap_ptr, vmm::vmm_ptr) 経由で各サブシステムにアクセスする。
//
// 出力先は引数 console で受け取る (kernel_main から vga を注入する)。
// テスト自身は VGA / シリアルのどちらに出るかを知らない。
// 個々のテスト関数に nullptr を渡してはならない (run_all() は NullConsole に置き換える)。
namespace tests
{

// 高位カーネル: 現在の RIP が高位アドレス (0xFFFFFFFF80000000〜) かを表示する。
void higher_half_check(IConsole *console);

// PMM: allocate / free / 解放したページの再割り当てを確認する。
void pmm_alloc_free(IConsole *console);

// ヒープ: sbrk によるページ拡張と alloc/free のブロック再利用を確認する。
void heap_sbrk_alloc(IConsole *console);

// 例外: 意図的に 0 除算 (#DE) を起こして isr_common_handler を確認する。
// NOTE: ハンドラが復帰しない実装なら、以降のテストには進まない。
void division_by_zero(IConsole *console);

// スケジューラ: 2 スレッドが yield で協調的に切り替わることを確認する。
void scheduler_yield(IConsole *console);

// アドレス空間: プロセスごとの PML4 分離 (同じ仮想アドレスが別物理ページ) を確認する。
void addrspace_separation(IConsole *console);

// sleep / wakeup: チャネル待ちで寝たプロセスが起こされることを確認する。
void sleep_wakeup(IConsole *console);

// fork / exit / wait: 子プロセスの生成と終了ステータスの回収を確認する。
void fork_wait(IConsole *console);

// リング3: ユーザーページへ遷移し、syscall で write/exit を呼ぶ。
// NOTE: usermode::enter() は戻らないため、このテストからは復帰しない。
[[noreturn]] void usermode_ring3(IConsole *console);

// キーボード: 入力をそのままエコーする。無限ループなので戻らない。
[[noreturn]] void keyboard_echo(IConsole *console);

// VirtIO ブロックデバイス: 初期化してセクタ0を読み、hexdump で表示する。
void virtio_block_read(IConsole *console);

// 有効化しているテストをまとめて実行する。
// どのテストを走らせるかは tests.cpp 側で切り替える。
void run_all(IConsole *console);

} // namespace tests
