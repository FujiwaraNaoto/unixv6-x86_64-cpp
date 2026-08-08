; gdt_selectors.inc - GDT セレクタ定義 (asm 側)
;
; !! include/gdt.hpp の gdt::SegmentSelector と同じ値を持つ二重定義。
;    片方だけ変えると壊れるので、必ず両方そろえて変更すること !!
;
; 値の内訳: [15:3]=GDT のエントリ番号, [2]=TI(0:GDT), [1:0]=RPL
; GDT の並び (User Data が User Code の直前) は sysret が要求する順序なので、
; 順番を入れ替えることは実質できない。
;
; %define ではなく %assign を使っているのは、%define が単なるトークン置換で
; 「mov ax, SEL_UserData」が「mov ax, 3<<3 | 0x03」に展開され、周囲の式と
; 結合したときに演算子の優先順位で意図せぬ値になりうるため。
; %assign は定義時に数値へ評価するのでその危険がない。

%ifndef GDT_SELECTORS_INC
%define GDT_SELECTORS_INC

%assign SEL_KernelCode 1<<3         ; 0x08 (index=1, DPL=0, L=1)
%assign SEL_KernelData 2<<3         ; 0x10 (index=2, DPL=0)
%assign SEL_UserData   3<<3 | 0x03  ; 0x1B (index=3, DPL=3, RPL=3)
%assign SEL_UserCode   4<<3 | 0x03  ; 0x23 (index=4, DPL=3, L=1, RPL=3)
%assign SEL_TSS        5<<3         ; 0x28 (index=5, 16バイトなので 5,6 の2エントリを占める)

%endif
