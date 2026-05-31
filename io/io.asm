; io.asm - ポート I/O プリミティブ (x86-64, System V AMD64 ABI)
; C++ 側からは extern "C" で outb / inb / io_wait を呼び出す。
; 引数: rdi=第1, rsi=第2  戻り値: rax

BITS 64
section .text

global outb            ; void outb(uint16_t port, uint8_t val)
global inb             ; uint8_t inb(uint16_t port)
global io_wait         ; void io_wait(void)

; ─── outb: ポート(di) に 1 バイト(sil) 出力 ─────────────────
outb:
    mov dx, di         ; port  -> dx
    mov al, sil        ; val   -> al
    out dx, al
    ret

; ─── inb: ポート(di) から 1 バイト読み込み al -> 戻り値 ──────
inb:
    mov dx, di         ; port  -> dx
    in  al, dx
    movzx eax, al      ; 上位ビットをゼロ拡張して返す
    ret

; ─── io_wait: 未使用ポート 0x80 へダミー出力して I/O 待機 ────
io_wait:
    xor eax, eax       ; val = 0
    mov dx, 0x80
    out dx, al
    ret
