; msr.asm
BITS 64
section .text
GLOBAL rdmsr
GLOBAL wrmsr

GLOBAL load_cr3

GLOBAL asm_flush_tlb

; uint64_t rdmsr(uint32_t msr)
;   RDI = msr番号
; rdmsr命令は 入力をECX, 出力を EDX:EAX に返すので、RDI -> ECX, EDX:EAX -> RAX に変換する
rdmsr:
    mov ecx, edi        ; MSR番号 -> ECX
    rdmsr               ; EDX:EAX <- MSR値
    shl rdx, 32
    or  rax, rdx        ; RAX = (hi<<32)|lo
    ret

; void wrmsr(uint32_t msr, uint64_t value)
;   RDI = msr番号, RSI = 値
; wrmsr命令は 入力をECX, EDX:EAX に返すので、RDI -> ECX, RSI -> EDX:EAX に変換する
wrmsr:
    mov ecx, edi        ; MSR番号 -> ECX
    mov eax, esi        ; 下位32bit -> EAX
    mov rdx, rsi
    shr rdx, 32         ; 上位32bit -> EDX  実行前[上位32bit H][下位32bit L] -> 実行後[    00000000][上位32bit H]
    wrmsr
    ret

; void load_cr3(uint64_t value)
; RDI = value
load_cr3:
    mov cr3, rdi
    ret

; void asm_flush_tlb()
asm_flush_tlb:
    ; CR3を再ロードしてTLBをフラッシュする
    mov rax, cr3
    mov cr3, rax
    ret
