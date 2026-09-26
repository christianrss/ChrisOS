OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
}
SECTIONS {
    . = 0x1000;
    .text : {
        *(.text .text.*)
    } :text
}
