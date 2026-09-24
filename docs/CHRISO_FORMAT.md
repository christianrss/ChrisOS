# ChrisO format

Version 1, as implemented in `compiler/chrisld/chriso.h` and
`compiler/chrisld/chriso.c` at
`894aed92e48e764e2627ecfc2514684f50809f59`. Version 2 is not implemented.

## Header

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `0x4F524843` |
| 4 | 4 | version `1` |
| 8 | 4 | symbol count |
| 12 | 4 | relocation count |
| 16 | 12 | size of text, rodata, data |
| 28 | 36 | reserved, zero |

Payloads follow in section order. Then symbol records of 80 bytes.
The fields are a 64-byte name and three `u32` values (76 bytes). The
writer copies 80 bytes from each `ChrisoSym`. Then relocation records of
16 bytes (section, offset, symbol index, addend).

Caps: 256 symbols, 512 relocations, three sections.

## Symbols

A symbol has a name, a section index, an offset, and a size. There is no
binding and no type. LOCAL, GLOBAL, and UNDEFINED are not represented.
FUNCTION and OBJECT are not represented.

A ChrisAsm `call` writes the callee name as a text symbol whose offset is
the displacement field. That is not an undefined symbol.

## Relocations

The record has no type. These names are the ones a later version needs
for the kernel, and they are not implemented:

| Name | Use |
| --- | --- |
| `R_X86_64_64` | 64-bit absolute |
| `R_X86_64_PC32` | 32-bit PC-relative |
| `R_X86_64_PLT32` | 32-bit PC-relative call or branch |
| `R_X86_64_32` | zero-extended 32-bit absolute |
| `R_X86_64_32S` | sign-extended 32-bit absolute |

`chriso_merge_text` appends text and shifts symbol offsets. It drops
relocations and does not append rodata or data. The text cap of a merge
is 64 KiB.

## Dump

No dumper is linked. A later `chrisobjdump` has to print sections,
symbols, and relocations before multi-object link bugs can be read off
an object file. `host-chriso-test` round-trips the v1 container. It does
not prove a relocation.
