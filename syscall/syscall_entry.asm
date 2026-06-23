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
; 注意: フェーズ6では リング0→0 のテストに限定するため
;       swapgs とユーザースタック切り替えは省略 (フェーズ7で追加)

BITS 64
section .text
extern syscall_dispatch
GLOBAL syscall_entry
syscall_entry:
    ; ユーザーが使うかもしれない RCX(戻りRIP) と R11(RFLAGS) を退避
    ; syscallは命令の次の命令アドレス(戻りRIP)をRCXに、ユーザーのRFLAGSをR11に積むので、これを退避しておく
    push rcx
    push r11

    ; caller-saved レジスタを退避 (dispatch が壊す可能性)
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9

    ; System V ABI呼び出し規約に並べ替える
    ; syscall_dispatch(num, a1, a2, a3, a4, a5)
    ;   第1引数 RDI <- RAX (番号)
    ;   第2引数 RSI <- RDI (a1)
    ;   第3引数 RDX <- RSI (a2)
    ;   第4引数 RCX <- RDX (a3)
    ;   第5引数 R8  <- R10 (a4)
    ;   第6引数 R9  <- R8  (a5)
    ; 元の値はスタックに退避済みなので順序に注意して組み替える
    ; movの順番が重要
    mov r9,  r8       ; a5
    mov r8,  r10      ; a4
    mov rcx, rdx      ; a3
    mov rdx, rsi      ; a2
    mov rsi, rdi      ; a1
    mov rdi, rax      ; num

    call syscall_dispatch
    ; 戻り値は RAX に入っている (そのまま使う)

    ; レジスタ復元
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi

    pop r11
    pop rcx

    ; リング0→0 のため sysret は使えない (sysret は必ず CPL=3 へ戻り、
    ; STAR[63:48]+8/+16 のユーザーセグメントを要求するが、GDT に無い)。
    ; syscall が退避してくれた RCX(戻りRIP)/R11(RFLAGS) で手動復帰する。
    push r11
    popfq             ; R11由来のRFLAGS 復元 (FMASK で落とした IF もここで戻る)
    jmp rcx           ; syscall の次の命令(戻りRIP)へ戻る
