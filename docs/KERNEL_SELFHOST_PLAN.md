# Kernel self-host plan

SH0 is proven by the host build. SH4 and SH5 are not. This plan is the
order of work. A step is done only when its gate passes. The audit of the
starting tree is `docs/NATIVE_TOOLCHAIN_AUDIT.md`.

## Levels

| Level | Input | Gate | Status |
| --- | --- | --- | --- |
| 0 | one function, literal `outb`, integer `return` | `host-kcc-kernel-l0` | not run |
| 1 | `kernel/metal/serial.c` and a port unit ChrisAsm can assemble | `host-kcc-kernel-l1` | not started |
| 2 | further small `kernel/metal` C files that stay inside the profile | `host-kcc-kernel-l2` | not started |
| 3 | PMM | `host-kcc-kernel-l3` | not started |
| 4 | heap | `host-kcc-kernel-l4` | not started |
| 5 | filesystem helpers | `host-kcc-kernel-l5` | not started |

Level 0 must reject `kernel/metal/serial.c`. Accepting that file by
skipping unrecognized lines is a failed gate, even if the process exits 0.

Level 1 cannot start from `port.c` as it stands. That file is GNU inline
assembly. The `in`/`out` instructions belong in ChrisAsm, and the C
callers belong in a profile translation unit. `serial.c` also needs
includes, macros, `static`, `bool`, `if`, `while`, calls, and volatile
atomics before it is an honest level-1 compile.

## Compiler shape

Keep the current lowering until a replacement produces the same bytes for
the level-0 program. Then split along the pipeline:

```
source
  lexer
  parser
  AST
  semantic check
  simple IR
  ChrisAsm or x86-64 bytes
  ChrisO
```

SSA is not required. Diagnostics are required: file, line, column,
severity, message.

Each new profile feature lands with a differential test against host GCC
when the feature has observable behavior: integer arithmetic, pointer
arithmetic, struct layout (`sizeof`, `offsetof`, packed, aligned), casts,
signedness, loops, calls, and 64-bit math. The binaries do not have to be
identical. The results do.

Volatile MMIO is a hard stop. No driver file is compiled by KCC until a
test shows a volatile read and a volatile write survive.

## Objects and link

ChrisO gains bss, symbol binding (local, global, undefined), symbol kind
(function, object), and the relocation types listed in
`docs/CHRISO_FORMAT.md`. A dumper prints sections, symbols, and
relocations.

ChrisLd then accepts more than one object, reports undefined and duplicate
symbols, applies relocations, concatenates sections, honors alignment, and
emits a higher-half ELF. The load base and the entry symbol come from the
manifest. The first target is:

```
KERNEL_BASE = 0xffffffff80000000
entry kstart
.text .rodata .data .bss
```

A host validator checks ELFCLASS64, little endian, `EM_X86_64`,
non-overlapping segments, an entry inside an executable segment, alignment,
`filesz <= memsz`, and `W^X` where a segment is executable.

## ChrisBuild

`SYS/KERNEL.BUILD` lists C sources, assembly sources, include paths, the
load base, and the entry. ChrisBuild compiles what the manifest names,
writes ChrisO, and calls ChrisLd. It does not keep a hard-coded demo list
as the kernel.

Rebuild decisions start with mtime. The later key is source hash, compiler
version, flags, and a dependency hash. A clean tree and a tree rebuilt
after a no-op edit produce the same semantic result. Byte-identical output
is the goal once the compiler is deterministic, and it is not claimed now.

## Bootstrap stages

| Stage | Who builds the kernel | Gate |
| --- | --- | --- |
| 0 | host GCC and host ld | existing `make kernel` |
| 1 | a host-built ChrisOS runs KCC and ChrisLd and writes `KERNEL1.ELF` | `qemu-selfhost-kernel-stage1` |
| 2 | the stage-1 kernel runs the same toolchain and writes `KERNEL2.ELF` | `qemu-selfhost-kernel-stage2` |

Stage 1 log markers, in order, when that gate exists:

```
SELFHOST KERNEL BUILD START
SELFHOST COMPILE OK
SELFHOST LINK OK
SELFHOST KERNEL GENERATED
SELFHOST KERNEL INSTALLED
SELFHOST REBOOT
SELFHOST NEW KERNEL BOOTED
```

The booted kernel prints a build id, the compiler name (`KCC stage1` or
`KCC stage2`), and a hash of the ELF it claims to be. Those strings are
how the gate recognizes the image. They are not printed today.

Until the level gates exist, an internal build writes
`BIN/KERNEL.TEST.ELF` and a boot menu entry distinct from the stable
kernel. It does not replace the only bootable kernel.

## What this plan does not do

It does not remove Limine. It does not treat `sys_cc` or a kernel-linked
`compiler/chrisc` as SH4. It does not start xHCI, NVMe bring-up, or a
disk install change before level 0 is an honest gate.
