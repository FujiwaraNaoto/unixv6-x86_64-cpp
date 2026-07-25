
BITS 64
section .text

; ─────────────────────────────────────────────────────────────────────────
; extern "C" uint64_t fork_capture(ProcessContext* out);
;   rdi = out  (7 qword バッファ。switch_context が pop する並びで書き込む)
;
; setjmp 風のヘルパ。呼び出し時点の callee-saved レジスタと「呼び出し元へ戻る
; アドレス(= fork() 内の復帰ポイント)」を out に保存し、呼び出し元(fork)の rsp を返す。
;
; 後で scheduler が switch_context(&child->context) でこのバッファをロードして
; ret すると、子は「fork_capture を呼んだ直後」から復帰する。復帰時には
; callee-saved が親と同じ値に復元され、rsp も子スタック上のミラー位置になるので、
; そのまま fork() の続きを実行できる(= リング0のカーネルスレッドとして続行)。
;
; メモリ配置(boot/switch.asm の restore と一致させる):
;   [out+0]=rbp [out+8]=rbx [out+16]=r12 [out+24]=r13 [out+32]=r14 [out+40]=r15 [out+48]=rip
; ─────────────────────────────────────────────────────────────────────────
global fork_capture
fork_capture:
    mov     rax, [rsp]      ; rax = 戻りアドレス(fork() 内の復帰ポイント)
    mov     [rdi+0],  rbp
    mov     [rdi+8],  rbx
    mov     [rdi+16], r12
    mov     [rdi+24], r13
    mov     [rdi+32], r14
    mov     [rdi+40], r15
    mov     [rdi+48], rax   ; rip = 復帰ポイント
    lea     rax, [rsp+8]    ; 戻り値 = 呼び出し元(fork)の rsp
    ret
