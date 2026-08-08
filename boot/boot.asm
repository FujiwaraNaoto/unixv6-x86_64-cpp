; boot.asm - Multiboot2 ヘッダ + x86-64 エントリポイント
; GRUB が本ファイルをロードし、64-bit long mode へ移行する

BITS 32

; 高位仮想オフセット (この値を引くと物理アドレスになる)
KERNEL_VMA equ 0xFFFFFFFF80000000

; リンカが高位アドレスを付けるので、低位で使うシンボルは
; (シンボル - KERNEL_VMA) で物理アドレスに変換する
%define PHYS(x) ((x) - KERNEL_VMA)


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
; NOTE: This is the minimal GDT at boot. It will be rebuilt with user segments + TSS in kernel_main.
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
    dq gdt64                            ; リンカは高位アドレスを入れる (移行後の再ロード用)

; 低位ブート時用の物理アドレス版GDTポインタ
gdt64_ptr_phys:
    dw gdt64_ptr - gdt64 - 1
    dq PHYS(gdt64)


; ─── 初期ページテーブル (identity map 0-2MiB) ─────────────────────
section .bss
ALIGN 4096
pml4_table: resb 4096
pdpt_table:  resb 4096
pd_table:    resb 4096
pt_table:    resb 4096 ; 2MiB page table  8byte * 512entries = 4096 bytes

; 高位カーネルマップ用
pdpt_high:   resb 4096
pd_high:     resb 4096
; direct map 用 (段階2で使うが先に確保)
pdpt_direct: resb 4096

; ─── 32-bit スタートアップ ────────────────────────────────────────
section .text
GLOBAL _start
_start:
    ; GRUBから: eax=Multiboot2マジック, ebx=情報構造体アドレス
    mov edi, eax        ; 後でカーネルに渡すため保存
    mov esi, ebx

    ; スタックも物理アドレスで設定 (まだ低位実行中)
    mov esp, PHYS(stack_top)

    ; ページテーブル構築
    call setup_paging

    ; long mode 有効化
    call enable_long_mode

    ; 64-bit GDT ロード
    lgdt [PHYS(gdt64_ptr_phys)]

    ; 64-bit コードセグメントへジャンプ
    jmp gdt64.code:PHYS(long_mode_entry)

; ─── ページテーブル設定 ───────────────────────────────────────────
setup_paging:
    ; PML4[0] → PDPT
    ; ─── 低位 identity map (PML4[0], ブート実行用) ───
    mov eax, PHYS(pdpt_table)
    or  eax, 0x3        ; Present + Writable
    mov [PHYS(pml4_table)], eax

    ; PDPT[0] → PD
    mov eax, PHYS(pd_table)
    or  eax, 0x3
    mov [PHYS(pdpt_table)], eax

    ; PD[0] → PT (巨大ページフラグなし)
    mov eax, PHYS(pt_table)
    or  eax, 0x3          ; Present + Writable
    mov [PHYS(pd_table)], eax

    ; PT[0..511] → 物理アドレス 0x0000 〜 0x1FF000 (各4KiB)
    mov ecx, 0             ; ループカウンタ
.pt_loop:
    mov eax, ecx
    shl eax, 12            ; index × 4096 = 物理アドレス
    or  eax, 0x3           ; Present + Writable
    mov [PHYS(pt_table) + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jl  .pt_loop

    ; ─── 高位カーネルマップ (0xFFFFFFFF80000000) ───
    ; 0xFFFFFFFF80000000 の分解:
    ;   PML4 index = 511
    ;   PDPT index = 510
    ;   PD   index = 0
    ; PML4[511] → pdpt_high
    mov eax, PHYS(pdpt_high)
    or  eax, 0x3
    mov [PHYS(pml4_table) + 511*8], eax

    ; PDPT[510] → pd_high
    mov eax, PHYS(pd_high)
    or  eax, 0x3
    mov [PHYS(pdpt_high) + 510*8], eax
    ; PD[0] → 同じ pt_table を再利用 (物理0-2MiB を高位にマップ)
    mov eax, PHYS(pt_table)
    or  eax, 0x3
    mov [PHYS(pd_high) + 0*8], eax


    ; CR3 ← PML4
    mov eax, PHYS(pml4_table)
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

    ; ─── 高位アドレスへジャンプ ───
    ; 高位の higher_half_entry のアドレスを取得して jmp
    mov rax, higher_half_entry   ; リンカが高位アドレスを入れる
    jmp rax                      ; ここで RIP が高位に移る


; ─── 以降は高位アドレスで実行 ───
higher_half_entry:
    ; スタックを高位アドレスに切り替え
    mov rsp, stack_top            ; 高位アドレス (リンカ値そのまま)

    ; Multiboot情報のポインタは物理 (低位) なので、
    ; そのまま kernel_main に渡す (kernel_main側でdirect map経由で読む)
    ; edi/esi は _start で保存済み

    ; カーネル main へ
    extern kernel_main
    call kernel_main

    ; カーネルが返ってきた場合は停止
.halt:
    cli
    hlt
    jmp .halt
