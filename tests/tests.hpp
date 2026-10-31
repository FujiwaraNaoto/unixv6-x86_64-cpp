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
// run_all() に入るテストは、合否を bool で返す (true = 成功)。判定はテスト自身が行い、
// run_all() はそれを集計して QEMU の終了コードにする。CI は終了コードだけを見る。
//
// 出力先は引数 console で受け取る (kernel_main から vga を注入する)。
// テスト自身は VGA / シリアルのどちらに出るかを知らない。
// 個々のテスト関数に nullptr を渡してはならない (run_all() は NullConsole に置き換える)。
namespace tests
{

// 高位カーネル: 現在の RIP が高位アドレス (0xFFFFFFFF80000000〜) かを表示する。
bool higher_half_check(IConsole *console);

// PMM: allocate / free / 解放したページの再割り当てを確認する。
bool pmm_alloc_free(IConsole *console);

// ヒープ: sbrk によるページ拡張と alloc/free のブロック再利用を確認する。
bool heap_sbrk_alloc(IConsole *console);

// 例外: IDT の 0 番が isr_stub_0 を指していることと、0 除算で #DE が起きることを確認する。
// 本番のハンドラは戻らないので、テストの間だけ IDT の 0 番をテスト用ハンドラに差し替える。
bool division_by_zero(IConsole *console);

// スケジューラ: 2 スレッドが yield で協調的に切り替わることを確認する。
bool scheduler_yield(IConsole *console);

// アドレス空間: プロセスごとの PML4 分離 (同じ仮想アドレスが別物理ページ) を確認する。
bool addrspace_separation(IConsole *console);

// sleep / wakeup: チャネル待ちで寝たプロセスが起こされることを確認する。
bool sleep_wakeup(IConsole *console);

// fork / exit / wait: カーネルスレッドから fork し、子プロセスの生成と終了ステータスの回収を確認する。
bool fork_from_kernel(IConsole *console);

// fork / exit / wait: リング3のユーザーページに降りて、syscall で write/exit を呼ぶ。
bool fork_from_ring3(IConsole *console);

// プロセス終了時の解放: アドレス空間の破棄と fork + wait の繰り返しで、空きページ数が減らないことを確認する。
bool process_free(IConsole *console);


// リング3: プロセスからユーザーページへ遷移し、syscall で write/exit を呼ぶ。
// syscall をまたいで callee-saved レジスタが保たれるかも確認する。
bool usermode_ring3(IConsole *console);

// キーボード: 入力をそのままエコーする。無限ループなので戻らない。
[[noreturn]] void keyboard_echo(IConsole *console);

// VirtIO ブロックデバイス: 初期化してセクタ0を読み、hexdump で表示する。
bool virtio_block_read(IConsole *console);

// VirtIO ブロックデバイス: 連続した読み込みと、書き込み → 読み戻しを確認する。
// 最後のセクタを一時的に書き換え、終わったら元の中身に戻す。
// デバイスを初期化済みの状態 (virtio_block_read() の後) で呼ぶこと。
bool virtio_block_read_write(IConsole *console);

bool inode_basic(IConsole *console);

// ディレクトリ: directory_lookup / directory_link の検索・追加・空きスロットの再利用・ブロック越えを確認する。
bool directory_basic(IConsole *console);

// パス解決: resolve_path / resolve_parent を確認する。
bool path_basic(IConsole *console);

// ファイル層: File の確保・offset・共有と、プロセスの fd 表 (fork / exit / syscall) を確認する。
bool file_basic(IConsole *console);


// 有効化しているテストをまとめて実行する。
// どのテストを走らせるかは tests.cpp 側で切り替える。
void run_all(IConsole *console);


} // namespace tests
