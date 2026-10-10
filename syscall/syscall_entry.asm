; syscall_entry.asm - syscall 命令のエントリスタブ
;
; CPU が syscall 実行時にやること:
;   RCX  <- ユーザーの RIP (戻りアドレス)
;   R11  <- ユーザーの RFLAGS
;   RIP  <- IA32_LSTAR (このラベル)
;   CS/SS <- IA32_STAR から (リング0へ)
;
; 呼び出し規約 (Linux互換):
;   RAX = syscall番号
;   RDI, RSI, RDX, R10, R8, R9 = 引数
;   戻り値は RAX
;

BITS 64

section .bss
saved_user_rsp: resq 1


section .text
extern syscall_dispatch
extern syscall_kernel_rsp ; 現在プロセスのカーネルスタックの先頭を指す変数
extern handle_syscall

GLOBAL syscall_entry
syscall_entry:
    ; caller-saved レジスタを退避 (dispatch が壊す可能性)
    mov [rel saved_user_rsp], rsp ; syscall_entry でのユーザスタックを保存.ユーザRSPをグローバル変数へ逃す
    mov rsp, [rel syscall_kernel_rsp] ; カーネルスタックに切り替え.
    push qword [rel saved_user_rsp] ; syscall_entry でのユーザスタックを退避. 逃した値をカーネルスタックに積み直す
    push rdi
    push rsi
    push rdx
    push rcx ; ユーザのrip 
    push rax ; syscall番号
    push rbx
    push rbp
    push r8
    push r9
    push r10
    push r11 ; ユーザのRFLAGS
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp ; TrapFrame* (syscall_entry で積んだフレームの先頭) を引数にする
    call handle_syscall
    jmp syscall_return_path ; syscall_dispatch から戻ったら、syscall_return_path へジャンプして sysret でリング3へ戻る


; fork した子がスケジューラから最初に選ばれたときの着地点。
; switch_context は 6 本 pop して ret するので、ここに来た時点で rsp は
; 子の TrapFrame の先頭を指している (fork() がそう置いている)。
GLOBAL fork_return
fork_return:
    jmp syscall_return_path

; fork した子はここに着地する (自分の TrapFrame に rsp を合わせた状態で来る)
; NOTE: process.hppのTrapFrameと順番を同じにすること
GLOBAL syscall_return_path
syscall_return_path:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11     ; ユーザーのRFLAGS(sysretで復帰)
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rbx
    pop rax     ; return address (ユーザーのRIP, sysretで復帰)
    pop rcx     ; ユーザーのRIP(sysretがRCX->RIPにする)
    pop rdx
    pop rsi
    pop rdi
    mov rsp, [rsp]      ; rspを元に戻す
    o64 sysret          ; RCX->RIP, R11->RFLAGS, リング3へ復帰
