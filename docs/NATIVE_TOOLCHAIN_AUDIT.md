# Native toolchain audit

Snapshot of `894aed92e48e764e2627ecfc2514684f50809f59` on
`cursor/native-toolchain-7c6f`. `origin/feat/os2` is
`07e115fc4852bd0f7243997ad2120d3b9c21ba8a`. That commit squashes the
foundation tree already present at `1e81ca4`. This branch also keeps the
developer-toolkit commits. Nothing was reset.

This is a source audit. It does not mark SH4, SH5, or any real-hardware
milestone. The pipeline that would count is source, then KCC, then ChrisO,
then ChrisLd, then `BIN/KERNEL.ELF`, then an installed boot of that ELF.
Host GCC and host `ld` remain the kernel build.

## Pipeline today

```
kernel/*.c
    host GCC
    host ld -T kernel/metal/linker.ld
    BIN/KERNEL.ELF          (higher half 0xffffffff80000000, entry kstart)
    Limine
```

The in-tree sketch is separate and does not feed that image:

```
a few statements
    KCC (text buffer, 32 KiB)
    ChrisAsm (text only, 64 KiB)
    ChrisO v1
    ChrisLd (one object, one PT_LOAD)
```

`kernel/tools/chrisbuild.c` can call that sketch from `SYS/BUILD.MK`.
No `SYS/BUILD.MK` or `SYS/KERNEL.BUILD` is in the tree. `ASM_OBJECTS=` is
parsed only to leave C mode. Assembly is not assembled. There is no mtime
or hash cache. `BUILD_MAX_OBJ` is 48 and each source read is capped at
64 KiB. `mk_install` rewrites `BIN/KERNEL.ELF` onto itself.

`tools/seed_selfhost.c` writes a CFS image from the host. That seed is a
host dependency.

## KCC

`compiler/kcc/kcc.c` is one file. There is no lexer, parser, AST, type
checker, or IR. `kcc_compile_source` walks lines and prints ChrisAsm text
into `g_asm[32768]`.

Accepted without a diagnostic:

- a line whose first byte is `#`
- `//` and block comments
- `#include` and `#define` after whitespace
- a whole line that begins with `static`
- function headers whose type word is `void`, `bool`, `int`, or
  `uint8_t` / `uint16_t` / `uint32_t` / `uint64_t`
- `outb` of integer literals, including a single `+` of two decimal
  numbers and a decimal or hex value
- `return` of an integer literal, and a bare `return`
- `{` and `}`

Everything else inside a function is ignored. The function still returns
success. A closing `}` on a function whose name is exactly `kstart` also
emits `mov rax, 0` and `ret`.

`outb` is lowered to `mov rax, 0`, `mov rdi, port`, `mov rsi, value`,
`call outb`. That is not an `out` instruction. The call displacement is
left zero. ChrisAsm records the name `outb` as a defined text symbol at
the displacement, not as an undefined symbol and not as a relocation.
ChrisLd never patches it, so the call lands on the next instruction.

There is no column, no severity, and no message. Failure is only a missing
argument, an unclosed block comment, or an assembler reject. Truncation of
the 32 KiB asm buffer is silent.

### What `test_kcc` actually checks

`tools/test_kcc.c` reads `kernel/metal/serial.c`, requires
`kcc_compile_source` to return 0, and requires a non-empty text section.
`serial.c` starts with `#include`, `static`, `if`, `while`, `inb`, and
`return false`. KCC skips those lines. The gate passes because the file
was not compiled. That result is not KERNEL_SELFHOST_LEVEL_1 and it is
not SH4.

`host-kcc-test` is already a dependency of `host-gates`. The recipe links
`kcc.c`, `chrisasm.c`, and `chriso.c`. It does not link `chrisld.c`.

No kernel C file in `kernel/metal/` or `kernel/fs/` is in the accepted
subset. `kernel/metal/port.c` is the usual port layer and is GNU
`__asm__ volatile` inline assembly. KCC does not compile it.

## ChrisAsm

`compiler/chrisasm/chrisasm.c`. Text cap `ASM_TEXT_MAX` is 65536. A full
buffer stops emitting and still returns success.

Registers: `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi` only.

Instructions that emit bytes:

- `ret` (`c3`)
- `syscall` (`0f 05`)
- `push rax` / `pop rax` only
- `mov reg, imm64` (`48 b8+rd` plus 8 bytes) and `mov reg, reg`
- `call name` (`e8` plus a zero displacement)

`.text` is ignored. A label becomes a text symbol at the current offset.
An unknown mnemonic returns success and emits nothing. There is no memory
operand, no `r8`–`r15`, no 32/16/8-bit names, no `lea`, arithmetic, `jmp`,
conditional branches, `cli`/`sti`/`hlt`, `iretq`, `in`/`out`, descriptor
or control-register ops, `cpuid`, `rdmsr`/`wrmsr`, `lock`, or SSE.
No `.rodata`, `.data`, `.bss`, `global`, `extern`, `align`, or data
directives.

## ChrisO v1

`compiler/chrisld/chriso.h`. Magic `0x4F524843`, version 1.

Sections: text, rodata, data. No bss. Symbols: name[64], section, offset,
size. Cap 256. Relocations: section, offset, symbol index, addend. Cap
512. There is no relocation type field, so `R_X86_64_64`, `R_X86_64_PC32`,
`R_X86_64_PLT32`, `R_X86_64_32`, and `R_X86_64_32S` do not exist.
There is no LOCAL / GLOBAL / UNDEFINED and no FUNCTION / OBJECT.

On disk: 64-byte header (magic, version, counts, three sizes), payloads,
80-byte symbol records, 16-byte relocation records.

`chriso_merge_text` concatenates text only, up to 64 KiB, and shifts
symbol offsets. It does not merge rodata, data, or relocations. A call
symbol stays a defined text symbol.

There is no `chrisobjdump`.

## ChrisLd

`chrisld_link` takes one `ChrisoImage`. It writes ELFCLASS64, little
endian, `EM_X86_64`, `ET_EXEC`, one `PT_LOAD` with read and execute.
`filesz` is the text size. `memsz` is that size rounded up to 4 KiB.
`CHRISLD_ELF_MAX` is 1 MiB.

The entry is `load_addr` plus the offset of a symbol whose name begins
with the characters `kstart`, otherwise a symbol that begins with `main`,
otherwise `load_addr`. The scan is character-by-character. A symbol named
`kstart_helper` would match.

Rodata, data, bss, and the relocation array are ignored. There is no
undefined-symbol error, no duplicate-symbol error, no section alignment
beyond that 4 KiB `memsz`, and no linker script. Passing
`0xffffffff80000000` only sets `p_vaddr`. It does not reproduce
`kernel/metal/linker.ld` (Limine requests, separate text and data
segments, 1 MiB stack in bss).

`tools/test_chrisld.c` assembles `main` / `mov rax, 0` / `ret`, links at
the higher-half address, and checks the ELF magic. It does not check
segments, entry range, `W^X`, or relocations.

`kernel/tools/native_link.c` writes a user ELF at `0x400000` from one
image. That is not the kernel link.

## Host tools the kernel still needs

| Step | Tool today |
| --- | --- |
| Kernel C and the bootstrap compiler | host GCC |
| `kernel/metal/*.S` and a few user blobs | NASM |
| Kernel link | host `ld` and `kernel/metal/linker.ld` |
| ISO / UEFI CD boot | Limine in `third_party/limine` |
| CFS seed used by self-host tests | `tools/seed_selfhost.c` on the host |

Limine stays. It is the bootloader. Removing it is not part of this stage.

## Real kernel files

| Level | Intended file | Can KCC compile it at this snapshot? |
| --- | --- | --- |
| 0 | a unit with only a function, literal `outb`, and `return` of an integer | the compiler accepts that shape, and it also accepts files that are not that shape |
| 1 | `kernel/metal/serial.c`, `kernel/metal/port.c` | no |
| 2 | other small `kernel/metal` units | no |
| 3 | PMM | no |
| 4 | heap | no |
| 5 | filesystem helpers | no |

`serial.c` needs includes, macros, `static`, `bool`, `if`, `while`,
`inb`, calls, pointer stores, and `volatile` spinlocks. `port.c` is
inline GNU assembly. Both are the level-1 target. Neither is compiled
by KCC.

## Installer, as it relates to a later boot of an internal kernel

`docs/FOUNDATION_AUDIT.md` records `test-qemu-install` as PASS. The old
stop at `install backup gpt` was the ATA DMA poll treating an
already-complete command as a timeout, plus a CFS bitmap scan from zero.
The gate now sees `install tree copied`, `install gpt+esp+cfs disk=ahci`,
`install image ok`, then `cfs mounted` and `desktop 60Hz` on the installed
disk. That boot is the host-built kernel. It is not SH5.

Remaining installer facts, not claimed as new bugs in the foundation gate:

- `install_auto` uses the first `bd_installable` disk. There is no model,
  serial, or capacity confirmation and no dry run.
- ESP is 16384 sectors, or 65536 sectors when `sector_count > 200000`.
  A disk smaller than `2048 + esp + STOR_DISK_SECTORS + 64` is refused.
- `copy_tree` copies `SYS`, `APPS`, `LIB`, `GAMES`, `BOOT`, `SRC`, `BIN`.
  It does not copy `EFI/`. The ESP is written separately (`BOOTX64.EFI`,
  `KERNEL.ELF`, `LIMINE.CFG`).
- The selftest RAM disk is sized from `STOR_DISK_SECTORS`, not from a
  variable target.
- There is no `KERNEL.PREV.ELF` and no Limine "previous kernel" entry.
- Host makefiles must not write `/dev/sdX` or `/dev/nvmeXnY`. None of the
  install gates do that. The in-OS picker still has the first-disk path.

## ChrisFS and large disks

`STOR_SECTOR_SIZE` is 512. `STOR_DISK_SECTORS` is 1048576, which is 512 MiB.
The superblock word at offset 8 must equal that constant or mount returns
-4. The bitmap is 256 sectors, exactly 1048576 bits. Inodes are 2048,
128 bytes each. Block pointers and `inode.size` are 32-bit.
`BlockDevice.sector_count` is `uint32_t`. The journal is 64 sectors.

A 64 GiB or larger SSD does not fit this format. Growing it is a format
revision with an explicit migration. Existing disks must keep mounting.
That work is not started. GPT math already reads `disk->sector_count`,
but the ChrisFS image it writes is still the fixed layout.

## QEMU assumptions that are not a hardware profile

- `disk.img` geometry and 512-byte sectors in the gates
- VirtIO, ATA, and AHCI as the test matrix presents them
- PS/2 input in the desktop gates
- framebuffer pitch and mode taken from the Limine response the test VM
  happens to provide
- SMP counts passed on the QEMU command line, with MADT used on some
  paths and not treated as the only topology source
- first installable disk in `install_auto`
- no USB HID requirement
- no disk serial confirmation

`kernel/fs/nvme.c`, `kernel/fs/ahci.c`, `kernel/metal/acpi.c`, and
`kernel/metal/apic.c` exist. Their presence is not a real-machine pass.
There is no xHCI source file. UHCI mass storage is `kernel/fs/usb_msc.c`
and is not a HID keyboard or mouse.

## ABI

The kernel is System V AMD64 as produced by host GCC: integer arguments
in RDI, RSI, RDX, RCX, R8, R9, return in RAX, callee-saved RBX, RBP,
R12–R15, 16-byte stack alignment. KCC does not implement that ABI. It
hard-codes the `outb` lowering into RDI and RSI. There is no host or
QEMU ABI test comparing KCC output with GCC. The kernel should not rely
on the red zone. That choice is not enforced by KCC because KCC does not
allocate stack slots.

## Gates that do not exist yet

`host-kcc-kernel-l0` through later kernel levels, `qemu-selfhost-kernel-stage1`,
`qemu-selfhost-kernel-stage2`, `test-qemu-installed-uefi` as a no-ISO
second boot with the markers in `docs/REAL_HARDWARE_PLAN.md`, and
`hardware-profile1`. Adding a name without a run does not pass it.

## Defects this audit treats as blocking

1. `host-kcc-test` succeeds on `serial.c` by skipping the file.
2. `call` records a defined symbol and ChrisLd does not apply a relocation.
3. ChrisLd links one text blob. It cannot link the kernel.
4. ChrisFS v4 cannot describe a disk other than 512 MiB.
5. `install_auto` selects the first installable disk.
