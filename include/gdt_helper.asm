; gdt_helper.asm - GDT の差し替えとセグメントレジスタのリロード
;
; extern "C" void LoadGDT(const gdt::GlobalDescriptorTablePointer* ptr);
;   rdi = &gdt_ptr
;
; call LoadGDT でC++戻りアドレスが積まれる 
; → GDTR差し替え 
; → データセグメント再ロード 
; → push CS; push RIP; 
; → retf でCSを新セグメントに切り替えつつ着地点へジャンプ 
; → 着地点ではRSPがcall直後に戻っているので ret でC++へ帰る
;
; [なぜ lgdt だけでは足りないか]
; lgdt は GDTR (テーブルの場所) を差し替えるだけで、既にロード済みのセグメント
; レジスタには何の影響もない。セグメントレジスタは代入した瞬間に GDT からディスク
; リプタを隠しレジスタ (base/limit/DPL/L bit ...) へコピーし、以降はそれだけを使う
; ため、新しい GDT の内容を反映させるにはセレクタを入れ直す必要がある。
; よって「lgdt -> データセグメント再ロード -> CS 再ロード」までが一連の作業になる。
;
; 事前条件 (呼び出し前に満たしておくこと):
;   1. rdi が有効な GlobalDescriptorTablePointer (limit:2byte + base:8byte, packed) を
;      指していること。指す先の GDT は関数から戻った後も生存し続けること
;      (CPU は例外/割り込みのたびにこのテーブルを引くので、スタック上の一時変数は不可)。
;   2. その GDT が以下のセレクタ配置であること。ここのセレクタ値は決め打ちなので、
;      並びを変えたらこのファイルも直す必要がある:
;        0x08 : カーネルCode (index=1, DPL=0, L=1)
;        0x10 : カーネルData (index=2, DPL=0)
;   3. 呼び出し時点で実行中のコードが、上記 0x08 と等価な (少なくとも矛盾しない)
;      コードセグメントで動いていること。boot.asm の暫定 GDT も index1=Code /
;      index2=Data で同じ並びなので、この条件は満たされている。
;   4. 割り込みが禁止されているか、または新旧 GDT でカーネル用セレクタの意味が
;      一致していること。lgdt から CS 再ロード完了までの間に割り込みが入ると、
;      IDT に書かれた CS セレクタが「新しい」GDT で解決されるため。
;
; 事後条件:
;   - GDTR が rdi のテーブルを指している。
;   - ds/es/ss/fs/gs = 0x10、cs = 0x08。いずれも隠しレジスタが新しい GDT の
;     ディスクリプタで埋め直されている。
;     => つまり、この関数から戻った後の C++ コードは「新しい GDT の新しい
;        コードセグメント (0x08) 上で」動いている。呼び出し前と後とで、走っている
;        コードセグメントの実体が別のディスクリプタに入れ替わっているのが、
;        この関数の本質的な効果。
;   - IA32_FS_BASE / IA32_GS_BASE が 0 にクリアされている (mov fs/gs の副作用)。
;   - 戻り先は呼び出し元の次の命令 (通常の関数と同じく戻る)。CPL は 0 のまま変わらない。
;   - 破壊するレジスタは rax のみ。rsp は呼び出し時の値に戻っている。

BITS 64
section .text
GLOBAL LoadGDT
LoadGDT:
    lgdt [rdi]                  ; GDTR を新しいテーブルへ差し替える

    ; データセグメントを再ロード (隠しレジスタを新GDTの内容で埋め直す)
    ; 注意: 64bitモードでは mov fs/gs が IA32_FS_BASE / IA32_GS_BASE MSR を
    ;       ゼロクリアする。将来 swapgs 用にベースを設定するなら、必ずこの後で行うこと。
    mov ax, 0x10                ; カーネルData (index=2(0x10>>3 = 2), RPL=0)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; CS は mov で書けない (CS の変更は RIP の変更と同時でなければならず、
    ; far jmp/call/ret・iret・syscall/sysret でしか変えられない)。
    ; 64bitモードでは即値の far jmp が無効命令なので far return を使う。
    push 0x08                   ; CS  = カーネルCode (index=1(0x08>>3 = 1), RPL=0)
    lea  rax, [rel .reload_cs]
    push rax                    ; RIP = 着地点
    o64  retf                   ; far return: RIP を pop -> CS を pop
.reload_cs:
    ; retf が自分で積んだ 2 つを pop したので、RSP は call 直後の状態に戻っている。
    ; そこに C++ からの戻りアドレスがあるのでそのまま near ret できる。
    ; 壊すのは rax のみ (System V ABI で caller-saved なので保存不要)。
    ret
