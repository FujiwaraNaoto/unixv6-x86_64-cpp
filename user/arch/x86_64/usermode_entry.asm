; usermode_entry.asm - iretq でリング3へ遷移する
;
; extern "C" void enter_usermode(uint64_t entry, uint64_t user_stack);
;   rdi = entry      (ユーザープログラムのRIP)
;   rsi = user_stack (ユーザースタックの先頭)

%include "gdt_selectors.inc"

BITS 64
section .text
GLOBAL enter_usermode

; ---------------------------------------------------------------------------
; enter_usermode
;
; 事前条件 (呼び出し前に満たしておくこと):
;   1. GDT が以下の配置でロード済みであること (gdt::initialize_gdt が構築する)
;        セレクタ : 用途
;        0x1B = 0b00011011 : ユーザーData セグメント (index=3(=0x1B>>3), DPL=3, L=0)
;        0x23 = 0b00100011 : ユーザーCode セグメント (index=4(=0x23>>3), DPL=3, L=1)
;      Data が先・Code が後という並びは syscall/sysret が要求する GDT 順序に
;      合わせたもの。GDT がこの順でなければセレクタ値の変更が必要
;      (値は gdt_selectors.inc の SEL_UserData / SEL_UserCode で定義)。
;   2. TSS の RSP0 にカーネルスタックが設定済みであること
;      (gdt::set_kernel_stack)。リング3へ移った後の割り込み/例外で
;      カーネルスタックへ切り替えるために必要で、この関数自体は設定しない。
;   3. rdi (entry) と rsi (user_stack) が、現在の CR3 のページテーブル上で
;      ユーザーモードからアクセス可能 (U/S=1) にマップ済みであること。
;      user_stack は「スタックの先頭 (最上位アドレス+1)」を指し、下方向へ伸びる。
;   4. 呼び出し時点では IF は不問。iretq が RFLAGS=0x202 をロードするため、
;      リング3では必ず割り込み許可になる。よってタイマ割り込みに耐えられる
;      状態 (IDT/PIC/スケジューラ) が整っていること。
;
; 事後条件:
;   - CPL=3 で rdi のアドレスから実行を再開する。CS=0x23, SS=0x1B,
;     ds/es/fs/gs=0x1B, RSP=rsi, RFLAGS=0x202 (IF=1)。
;   - この関数は戻らない ([[noreturn]])。カーネルへ戻る経路は syscall か
;     割り込み/例外のみ。
;   - 呼び出し時のカーネルスタック上の内容 (戻りアドレス含む) は破棄される。
;     汎用レジスタは rdi/rsi 以外は不定のままリング3へ引き継がれるので、
;     ユーザープログラム側はレジスタの初期値に依存してはならない。
; ---------------------------------------------------------------------------
enter_usermode:
    ; データセグメントをユーザー用に切り替え
    mov ax, SEL_UserData    ; ユーザーData (RPL=3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; iretq が読むフレームをスタックに積む (逆順)
    push SEL_UserData   ; SS  = ユーザーData (RPL=3)
    push rsi            ; RSP = ユーザースタック
    push 0x202          ; RFLAGS (IF=1)
    push SEL_UserCode   ; CS  = ユーザーCode (RPL=3)
    push rdi            ; RIP = エントリ

    iretq               ; リング3へ降りる
