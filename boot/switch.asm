; switch.asm - コンテキストスイッチ本体
;
; extern "C" void switch_context(Context **old, Context *next);
;   rdi = old (現在のプロセスの Context** へのポインタ)
;   rsi = next (次に実行するプロセスの Context*)
;
; callee-saved レジスタ (rbx, rbp, r12-r15) と rsp だけ保存すればよい。
; rip は call/ret で自然に扱われる。

BITS 64
section .text
GLOBAL switch_context
switch_context:
    ; 現在のレジスタをスタックに積む (Context構造体と同じ並び)
    push r15
    push r14
    push r13
    push r12
    push rbx
    push rbp

    ; 現在の rsp を *old に保存 (old は Context** なので2段階)
    mov [rdi], rsp

    ; 次のプロセスの rsp に切り替え
    mov rsp, rsi

    ; 次のプロセスのレジスタを復元
    pop rbp
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15

    ; スタックに積まれている戻りアドレスへ ret
    ; (新規プロセスの場合は trampoline へ飛ぶよう仕込んである)
    ret
