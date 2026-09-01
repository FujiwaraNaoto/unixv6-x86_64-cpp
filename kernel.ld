/* kernel.ld - x86-64 カーネル リンカスクリプト
 *
 * GRUB は 1MiB 以降の物理アドレスにカーネルをロードする。
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* 高位仮想アドレスのベース (物理 + このオフセット = 仮想) */
KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x100000;              /* 物理ロードアドレス (1MiB) */


SECTIONS {
    /* 仮想アドレスは高位、物理ロードは低位 (AT で指定) */
    . = KERNEL_VMA + KERNEL_LMA;

    /* Multiboot2ヘッダは物理低位に置く必要がある (GRUBが探すため) */
    .multiboot2 : AT(KERNEL_LMA) {
        *(.multiboot2)
    }

    /* コードセクション */
    .text ALIGN(4096) : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text)
        *(.text.*)
    }

    .user ALIGN(4096) : {
        *(.user)
    }

    /* 読み取り専用データ (GDT など) */
    .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata)
        *(.rodata.*)
    }

    /* 初期化済みデータ */
    .data ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA) {
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
    .bss ALIGN(4096) : AT(ADDR(.bss) - KERNEL_VMA) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* カーネル終端シンボル */
    kernel_end = .;

    /* 物理アドレスでのカーネル終端 (pmm用) */
    kernel_phys_end = kernel_end - KERNEL_VMA;
}
