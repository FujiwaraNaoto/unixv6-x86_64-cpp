; usermode_entry.asm - iretq でリング3へ遷移する
;
; extern "C" void enter_usermode(uint64_t entry, uint64_t user_stack);
;   rdi = entry      (ユーザープログラムのRIP)
;   rsi = user_stack (ユーザースタックの先頭)

BITS 64
section .text
GLOBAL enter_usermode
enter_usermode:
    ; データセグメントをユーザー用に切り替え
    mov ax, 0x1B        ; ユーザーData (RPL=3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; iretq が読むフレームをスタックに積む (逆順)
    push 0x1B           ; SS  = ユーザーData (RPL=3)
    push rsi            ; RSP = ユーザースタック
    push 0x202          ; RFLAGS (IF=1)
    push 0x23           ; CS  = ユーザーCode (RPL=3)
    push rdi            ; RIP = エントリ

    iretq               ; リング3へ降りる
