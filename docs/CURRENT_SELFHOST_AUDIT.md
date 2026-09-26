# Current self-host audit

Definitions match the campaign levels. A level is proven only by a gate
that shows it. This pass did not add that gate.

| Level | Class | Why |
| --- | --- | --- |
| SH0 | HOST-TESTED on earlier trees | Host GCC and `ld` build the image. `make host-gates` is the host suite. It was not re-run as a whole here. |
| SH1 | NOT PROVEN | In-kernel ChrisC is the bootstrap compiler. No QEMU marker shows a compiler that is itself a ChrisOS program compiling and running an application. |
| SH2 | NOT PROVEN | No stage-1 and stage-2 compiler gate. |
| SH3 | NOT PROVEN | Libraries, desktop, and tools are not rebuilt inside the OS by a gate. |
| SH4 | NOT PROVEN | `host-kcc-test` compiles the level-0 fixture and every `kernel/metal` C file, including `start.c` (`kstart`), `bootinfo.c`, `gdt.c`, and `spin.c`. A probe compiled 50 of 112 makefile C units. Float arithmetic, `union`, numeric array initializers, and `idt_stubs.asm` still stop the rest. `chrisld_link_objects` has not linked `BIN/KERNEL.ELF`. |
| SH5 | NOT PROVEN | No install of an internally built kernel and no reboot that prints that kernel's hash. |
| SH6 | NOT PROVEN | A successor install still needs the host toolchain. Limine stays external, which the level allows, but the OS cannot yet build itself. |

Blocker for every level past SH0: the native toolchain does not compile
the whole kernel, and nothing it emits has been booted. `limine.h` parses
for x86_64 API revision 3. The next real stops are `union` in `task.h`,
numeric brace initializers, `<string.h>`, float arithmetic in the GFX
units, NASM `idt_stubs.asm`, and a ChrisLd image with a Limine requests
`PT_LOAD`, a 1 MiB stack, and entry `kstart`. A GCC differential test is
still absent.
