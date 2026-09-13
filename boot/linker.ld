/* LEARN:P01 linker: kernel em 0x1000, todas as secções. */
ENTRY(START)

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    /*
     * LEARN:P01c — 128 sectores ocupam 0x1000..0x10FFF.
     * Mantém o BSS acima da carga e longe do VBE info em 0x8000.
     */
    . = 0x12000;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)

        __bss_end = .;
    }
}
