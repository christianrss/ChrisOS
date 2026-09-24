# ChrisLd status

Audit snapshot: `894aed92e48e764e2627ecfc2514684f50809f59`.
Source: `compiler/chrisld/chrisld.c`.

## Implemented

`chrisld_link` writes one ELF64 executable from one `ChrisoImage`:

- ELFCLASS64, little endian, `EM_X86_64`, `ET_EXEC`
- one `PT_LOAD`, flags read and execute
- file size equal to the text size
- memory size rounded up to 4096
- entry at `load_addr + offset` of the exact symbol `kstart`, else
  exact `main`, else `load_addr`
- output cap `CHRISLD_ELF_MAX` (1 MiB)

`chrisld_link_objects` concatenates `.text` from more than one ChrisO,
resolves global symbols, rejects a duplicate global, and fails when a
relocation names a missing symbol. `R_X86_64_64`, `R_X86_64_PC32`,
`R_X86_64_PLT32`, `R_X86_64_32`, and `R_X86_64_32S` are applied.
`chrisld_validate` checks ELFCLASS64, little endian, `EM_X86_64`,
`filesz <= memsz`, `W^X`, non-overlapping load segments, and an entry
inside an executable segment.

`tools/test_chrisld.c` links one object at `0xffffffff80000000` and two
objects whose call displacement is checked. It also rejects an undefined
symbol and a duplicate global.

## Not implemented

Rodata, data, and bss in the ELF image. A second writable `PT_LOAD`.
The Limine request segment and the 1 MiB kernel stack from
`kernel/metal/linker.ld`. An internal linker script.

Setting the load address to `0xffffffff80000000` stores that value in
`p_vaddr`. The host kernel link remains `ld -T kernel/metal/linker.ld`.

`kernel/tools/native_link.c` is a user-space ELF at `0x400000`. It is not
this linker and it is not `BIN/KERNEL.ELF`.
