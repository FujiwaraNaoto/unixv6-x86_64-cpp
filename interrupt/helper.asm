BITS 64
global LoadTR
LoadTR: ;void LoadTR(uint16_t sel)
    mov ax, di
    ltr ax
    ret
