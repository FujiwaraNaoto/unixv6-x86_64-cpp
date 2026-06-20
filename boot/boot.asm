; boot.asm - Multiboot2 ヘッダ + x86-64 エントリポイント
; GRUB が本ファイルをロードし、64-bit long mode へ移行する

BITS 32

; ─── Multiboot2 マジック定数 ───────────────────────────────────────
MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0           ; i386/x86
MB2_HDRLEN   equ (mb2_end - mb2_start)
MB2_CHECKSUM equ -(MB2_MAGIC + MB2_ARCH + MB2_HDRLEN) & 0xFFFFFFFF

section .multiboot2
ALIGN 8
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_HDRLEN
    dd MB2_CHECKSUM
    ; 終端タグ (type=0, flags=0, size=8)
    dw 0
    dw 0
    dd 8
mb2_end:

; ─── 初期スタック (16 KiB) ────────────────────────────────────────
section .bss
ALIGN 16
stack_bottom:
    resb 16384
stack_top:

; ─── GDT (64-bit 用) ──────────────────────────────────────────────
section .rodata
ALIGN 8
gdt64:
    dq 0                          ; NULL ディスクリプタ
.code: equ $ - gdt64
    dq (1<<43)|(1<<44)|(1<<47)|(1<<53)  ; 64-bit コードセグメント
.data: equ $ - gdt64
    dq (1<<41)|(1<<44)|(1<<47)          ; データセグメント
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64

; ─── 初期ページテーブル (identity map 0-2MiB) ─────────────────────
section .bss
ALIGN 4096
pml4_table: resb 4096
pdpt_table:  resb 4096
pd_table:    resb 4096
pt_table:    resb 4096 ; 2MiB page table  8byte * 512entries = 4096 bytes

; ─── 32-bit スタートアップ ────────────────────────────────────────
section .text
GLOBAL _start
_start:
    ; GRUBから: eax=Multiboot2マジック, ebx=情報構造体アドレス
    mov edi, eax        ; 後でカーネルに渡すため保存
    mov esi, ebx

    ; スタック設定
    mov esp, stack_top

    ; ページテーブル構築
    call setup_paging

    ; long mode 有効化
    call enable_long_mode

    ; 64-bit GDT ロード
    lgdt [gdt64_ptr]

    ; 64-bit コードセグメントへジャンプ
    jmp gdt64.code:long_mode_entry

; ─── ページテーブル設定 ───────────────────────────────────────────
setup_paging:
    ; PML4[0] → PDPT
    mov eax, pdpt_table
    or  eax, 0x3        ; Present + Writable
    mov [pml4_table], eax

    ; PDPT[0] → PD
    mov eax, pd_table
    or  eax, 0x3
    mov [pdpt_table], eax

    ; PD[0] → PT (巨大ページフラグなし)
    mov eax, pt_table
    or  eax, 0x3          ; Present + Writable
    mov [pd_table], eax

    ; PT[0..511] → 物理アドレス 0x0000 〜 0x1FF000 (各4KiB)
    mov ecx, 0             ; ループカウンタ
.pt_loop:
    mov eax, ecx
    shl eax, 12            ; index × 4096 = 物理アドレス
    or  eax, 0x3           ; Present + Writable
    mov [pt_table + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jl  .pt_loop

    ; CR3 ← PML4
    mov eax, pml4_table
    mov cr3, eax
    ret

; ─── Long Mode 有効化 ─────────────────────────────────────────────
enable_long_mode:
    ; PAE 有効化
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; EFER.LME セット
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; ページングと保護モード有効化
    mov eax, cr0
    or  eax, (1 << 31) | (1 << 0)
    mov cr0, eax
    ret

; ─── 64-bit エントリ ──────────────────────────────────────────────
BITS 64
long_mode_entry:
    ; データセグメント更新
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; カーネル main へ
    extern kernel_main
    call kernel_main

    ; カーネルが返ってきた場合は停止
.halt:
    cli
    hlt
    jmp .halt
