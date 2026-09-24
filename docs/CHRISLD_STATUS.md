# ChrisLd status

Audit snapshot: `894aed92e48e764e2627ecfc2514684f50809f59`.
Source: `compiler/chrisld/chrisld.c`.

## Implemented

`chrisld_link` writes one ELF64 executable from one `ChrisoImage`:

- ELFCLASS64, little endian, `EM_X86_64`, `ET_EXEC`
- one `PT_LOAD`, flags read and execute
- file size equal to the text size
- memory size rounded up to 4096
- entry at `load_addr + offset` when a symbol name begins with `kstart`,
  else a name that begins with `main`, else `load_addr`
- output cap `CHRISLD_ELF_MAX` (1 MiB)

`tools/test_chrisld.c` checks the ELF magic after linking a three-line
program at `0xffffffff80000000`.

## Not implemented

Multiple input objects. Symbol resolution. Undefined-symbol errors.
Duplicate-symbol errors. Relocation application. Concatenation of text,
rodata, data, and bss. Per-section alignment. A bss section.
Program headers that match `kernel/metal/linker.ld`. A loadable higher-half
layout with Limine request segments, a read-only text segment, a writable
data segment, and the 1 MiB kernel stack. An internal linker script or
manifest. `W^X` as a checked property. A host validator for overlapping
segments, entry inside an executable segment, alignment, and
`filesz <= memsz`.

Setting the load address to `0xffffffff80000000` stores that value in
`p_vaddr`. The host kernel link remains `ld -T kernel/metal/linker.ld`.

`kernel/tools/native_link.c` is a user-space ELF at `0x400000`. It is not
this linker and it is not `BIN/KERNEL.ELF`.
