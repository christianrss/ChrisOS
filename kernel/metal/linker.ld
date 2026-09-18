OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kstart)

PHDRS {
    requests PT_LOAD FLAGS(6);
    text     PT_LOAD FLAGS(5);
    data     PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0xffffffff80000000;
    __kernel_start = .;

    .limine_requests : ALIGN(4K) {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests

    .text : ALIGN(4K) {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    } :text

    .data : ALIGN(4K) {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(4K) {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __stack_bottom = .;
        . += 64K;
        __stack_top = .;
        . = ALIGN(4K);
        __kernel_end = .;
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
    }
}
