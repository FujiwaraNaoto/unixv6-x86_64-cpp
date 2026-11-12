; isr.asm - 例外/IRQ スタブテーブル
; C ハンドラへ dispatch する薄いラッパー群

BITS 64

; ─── 例外ハンドラ (ISR 0-31) ────────────────────────────────────
; エラーコードを積まない例外はダミーの 0 を積んで統一

extern isr_common_handler

%macro ISR_NOERR 1
isr_stub_%1:
    push qword 0        ; dummy error code
    push qword %1       ; interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%1:
    push qword %1       ; error code は CPU が積む
    jmp isr_common
%endmacro

; エラーコードなし
ISR_NOERR 0   ; Divide by Zero
ISR_NOERR 1   ; Debug
ISR_NOERR 2   ; NMI
ISR_NOERR 3   ; Breakpoint
ISR_NOERR 4   ; Overflow
ISR_NOERR 5   ; Bound Range
ISR_NOERR 6   ; Invalid Opcode
ISR_NOERR 7   ; Device Not Available
; エラーコードあり
ISR_ERR   8   ; Double Fault
ISR_NOERR 9
ISR_ERR   10  ; Invalid TSS
ISR_ERR   11  ; Segment Not Present
ISR_ERR   12  ; Stack Fault
ISR_ERR   13  ; General Protection Fault
ISR_ERR   14  ; Page Fault
ISR_NOERR 15
ISR_NOERR 16  ; x87 FPE
ISR_ERR   17  ; Alignment Check
ISR_NOERR 18  ; Machine Check
ISR_NOERR 19  ; SIMD FPE
%assign i 20
%rep 12
ISR_NOERR i
%assign i i+1
%endrep

; 共通例外処理
isr_common:
    ; 汎用レジスタ保存
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; 第1引数: スタックポインタ (regs構造体)
    call isr_common_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16         ; error code + interrupt number を捨てる
    iretq

; ─── ISR スタブテーブル ────────────────────────────────────────
section .rodata
GLOBAL isr_stubs
isr_stubs:
%assign i 0
%rep 32
    dq isr_stub_%+i
%assign i i+1
%endrep

; ─── IRQ スタブ (IRQ 0-15) ───────────────────────────────────
section .text
extern irq0_handler
extern irq_handler

%macro IRQ_STUB 1
irq_stub_%1:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%if %1 == 0
    call irq0_handler
%else
    mov rdi, %1
    call irq_handler
%endif
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
%endmacro

%assign i 0
%rep 16
IRQ_STUB i
%assign i i+1
%endrep

section .rodata
GLOBAL irq_stubs
irq_stubs:
%assign i 0
%rep 16
    dq irq_stub_%+i
%assign i i+1
%endrep
