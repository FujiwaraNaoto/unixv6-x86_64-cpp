
# boot

reference https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html#boot_002eS


## 動作モード宣言
NASMに32bitコードとしての機械語を生成せよと命令．GRUBはカーネルを保護モード(32bit)で起動するため，最初に32bitで書く
```asm
BITS 32
```

MultiBoot2ヘッダ`equ`は定数でCの`#define`に該当する．
定義は
https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html#EFI-64_002dbit-system-table-pointer
をみると良い
```asm
MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0
MB2_HDRLEN   equ (mb2_end - mb2_start)
MB2_CHECKSUM equ -(MB2_MAGIC + MB2_ARCH + MB2_HDRLEN) & 0xFFFFFFFF
```

GRUBはカーネルのバイナリ先頭32KiB以内に以下のヘッダを探す.見つけた場合はMultiboot2準拠のカーネルと認識する．type=0はタグの終わりを示すマーカー．

```asm
section .multiboot2
ALIGN 8
mb2_start:
    dd MB2_MAGIC     ; 4バイト: マジック値
    dd MB2_ARCH      ; 4バイト: アーキテクチャ (0=x86)
    dd MB2_HDRLEN    ; 4バイト: ヘッダ全体の長さ
    dd MB2_CHECKSUM  ; 4バイト: チェックサム
    dw 0             ; type=0  (終端タグ)
    dw 0             ; flags=0
    dd 8             ; size=8
mb2_end:
```

## スタック

スタックの確保
```asm
section .bss
ALIGN 16
stack_bottom:
    resb 16384   ; 16KiB を0初期化済み領域として予約
stack_top:
```

`.bss`セクションはゼロ初期化される領域．`resb N`はNバイト予約の意味で実際のバイト列はバイナリに含まれずロード時にゼロで埋められる．x86のスタックはアドレスが下方向へ伸びるため`stack_top`をスタックポインタに設定する．
`ALIGN 16`はSystem V AMD64 ABIの要件(スタックは16byte境界)への対応．



## 64bit用GDT


```asm
gdt64:
    dq 0                                        ; NULLディスクリプタ (必須)
.code: equ $ - gdt64
    dq (1<<43)|(1<<44)|(1<<47)|(1<<53)          ; コードセグメント
.data: equ $ - gdt64
    dq (1<<41)|(1<<44)|(1<<47)                  ; データセグメント
gdt64_ptr:
    dw $ - gdt64 - 1   ; GDTのサイズ-1 (limit)
    dq gdt64            ; GDTの物理アドレス (base)
```
GDT(Global Descriptor Table)はCPUがセグメント管理するテーブルのこと．

- bit41 = Writable = データセグメントに書き込み可能
- bit43 = Executable = コードセグメントとして実行可能
- bit44 = S = システムではなく通常セグメント
- bit47 = Present = このディスクリプタは有効
- bit53 = L = 64bitコードセグメント

`.code: equ $ - gdt64` は現在位置からgdt64先頭までのオフセットを定数として定義し，後で`gdt64.code`という形でセグメントセレクタとして扱う．


## ページテーブル

```asm
section .bss
ALIGN 4096
pml4_table: resb 4096   ; 第4階層
pdpt_table:  resb 4096  ; 第3階層
pd_table:    resb 4096  ; 第2階層
```
x86-64のページングは4階層構造(PML4, PDPT, PD, PT)であるが，2MiB巨大なページを使う場合はPDの段階で終わり，PTは使わない．各テーブルは4KiB(512エントリx8バイト)である．

## エントリポイント

```asm
_start:
    mov edi, eax    ; eax = Multiboot2マジック → edi に退避
    mov esi, ebx    ; ebx = 情報構造体アドレス → esi に退避
    mov esp, stack_top
```
GRUBは`eax`にマジック値,`ebx`に情報構造体のアドレスを入れて`_start`にジャンプする．これらを後で`kernel_main(edi,esi)`に渡すため，System V AMD64 ABIの第一と第二引数レジスタ(rdi/rsi)に対応する32bit版に退避させている


## ページテーブルの構築

```asm
    mov eax, pdpt_table
    or  eax, 0x3           ; bit0=Present, bit1=Writable
    mov [pml4_table], eax  ; PML4[0] → PDPT

    mov eax, pd_table
    or  eax, 0x3
    mov [pdpt_table], eax  ; PDPT[0] → PD

    mov dword [pd_table], 0x000083  ; PD[0] → 0x000000 (2MiB巨大ページ)
```

`0x000083`は`0b10000011`でbit0=Present, bit1=Writable,bit7=PageSizeです．これらにより仮想アドレス0~2MiBが物理アドレス0~2MiBにそのままマップされる．(identity mapping)

## 64bitモード有効化

step1: PAE(物理アドレス拡張)を有効化

64bitページングにはPAEが前提条件
```asm
    mov eax, cr4
    or  eax, (1 << 5)   ; CR4.PAE ビットをセット
    mov cr4, eax
```

step2: EFER.LMEをセット

```asm
mov ecx, 0xC0000080  ; EFER（Extended Feature Enable Register）のMSRアドレス
    rdmsr                 ; MSRを eax:edx に読み込む
    or  eax, (1 << 8)    ; LME（Long Mode Enable）ビットをセット
    wrmsr                 ; MSRに書き戻す
```

MSR(Model Specific Register)はVPU拡張設定レジスタ群でrdmsr/wrmsr命令で読み書きする．LMEをセットしただけではまだ64bitになれない

step3
PGとPEを有効化

```asm
    mov eax, cr0
    or  eax, (1 << 31) | (1 << 0)  ; bit31=PG（ページング）, bit0=PE（保護モード）
    mov cr0, eax
```
CR0.PGをセットした瞬間にLMEが有効になり，CPUがlong modeに移行する．このときすでにCR3が正しく設定されている日強王があるため，先にsetup_page()を呼びました．



## 64bitコードへ

```asm
jmp gdt64.code:long_mode_entry
```

far jump．
`gdt64.code`がセグメントセレクタ，`log_mode_entry`がオフセット．CPUはこのジャンプでコードセグメントレジスタ(CS)を64bitセグメントに切り替えて完全に64bitモードに入る． `lgdt`でGDTをロードしただけでは切り替わらず，このfar jumpが必要．

```asm
BITS 64
long_mode_entry:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
```
far jumpでCSは更新されたが，残りのセグメントレジスタ(DS,ES,FS,GS,SS)はまだ古い値を持っているため全部データセグメントに更新する．


```asm
call kernel_main
.halt:
    cli    ; 割り込み禁止
    hlt    ; CPUを停止
    jmp .halt
```

`kernel_main`を呼び出してもし帰って来た場合は割り込みを禁止してhaltする．
`jmp .halt`はhltが何らかの割り込みで復帰した場合の保険．

