/* kernel.ld - x86-64 カーネル リンカスクリプト
 *
 * GRUB は 1MiB 以降の物理アドレスにカーネルをロードする。
 * ここでは 1MiB (0x100000) を基点にリンクする。
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
    /* load address: 1 MiB */
    . = 0x100000;

    /* Multiboot2 ヘッダは先頭に必ず置く (8KiB 以内) */
    .multiboot2 : {
        *(.multiboot2)
    }

    /* コードセクション */
    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }

    /* 読み取り専用データ (GDT など) */
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* 初期化済みデータ */
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    /* グローバルコンストラクタへのポインタ配列 (CRT 相当)。
     * 境界シンボルをカーネルから走査してコンストラクタを呼ぶ。 */
    .init_array ALIGN(8) : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
        __init_array_end = .;
    }

    /* 未初期化データ (ページテーブル, スタックなど) */
    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* カーネル終端シンボル */
    kernel_end = .;
}
