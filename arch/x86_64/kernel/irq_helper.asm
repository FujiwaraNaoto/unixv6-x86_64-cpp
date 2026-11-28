BITS 64
global LoadTR
LoadTR: ;void LoadTR(uint16_t sel)
    mov ax, di
    ltr ax
    ret

; page fault 例外発生時に CR2 レジスタに格納される、アクセスしようとした仮想アドレスを返す
global read_cr2
read_cr2:
    mov rax, cr2
    ret
