#pragma once
#include "console.hpp"

// syscall/sysret を有効化する (EFER.SCE / STAR / LSTAR / FMASK の設定)。
// 汎用の SystemCall::init() が、出力先を登録した後に呼ぶ。
// console は初期化メッセージの出力先。
void arch_syscall_init(IConsole *console);
