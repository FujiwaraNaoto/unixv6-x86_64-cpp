; io.asm - ポート I/O プリミティブ (x86-64, System V AMD64 ABI)
; C++ 側からは extern "C" で outb / inb / out32b / in32b / io_wait を呼び出す。
; 引数: rdi=第1, rsi=第2  戻り値: rax

BITS 64
section .text

global outb            ; void outb(uint16_t port, uint8_t val)
global inb             ; uint8_t inb(uint16_t port)
global out32b          ; void out32b(uint16_t port, uint32_t val)
global in32b           ; uint32_t in32b(uint16_t port)
global io_wait         ; void io_wait(void)

; void outb(uint16_t port, uint8_t val)
; 引数: rdi = port, rsi = val (System V ABI)
; 1バイト幅のレジスタを持つデバイス(UART等)はこちらを使う
outb:
    mov dx, di          ; port (16bit) をDXレジスタに
    mov al, sil         ; value (8bit) をALレジスタに
    out dx, al          ; I/Oポートへ1バイト出力
    ret                 ; 呼び出し元に戻る

; uint8_t inb(uint16_t port)
; 引数: rdi = port (System V ABI)
; 戻り値: rax (8bit有効、上位はゼロ拡張)
inb:
    mov dx, di          ; port (16bit) をDXレジスタに
    in  al, dx          ; I/Oポートから1バイト読み取り、結果はALに
    movzx eax, al       ; 上位ビットをゼロ拡張して返す
    ret                 ; 呼び出し元に戻る

; void out32b(uint16_t port, uint32_t val)
; 引数: rdi = port, rsi = val (System V ABI)
out32b:
    mov dx, di          ; port (16bit) をDXレジスタに
    mov eax, esi        ; value (32bit) をEAXレジスタに
    out dx, eax         ; I/Oポートへ出力
    ret                 ; 呼び出し元に戻る

; uint32_t in32b(uint16_t port)
; 引数: rdi = port (System V ABI)
; 戻り値: rax (32bit有効)
in32b:
    mov dx, di          ; port (16bit) をDXレジスタに
    in eax, dx          ; I/Oポートから読み取り、結果はEAXに
    ret                 ; 呼び出し元に戻る

; ─── io_wait: 未使用ポート 0x80 へダミー出力して I/O 待機 ────
io_wait:
    xor eax, eax        ; val = 0
    mov dx, 0x80
    out dx, al
    ret
