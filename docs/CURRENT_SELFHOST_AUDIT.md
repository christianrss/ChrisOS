# Current self-host audit

Definitions match the campaign levels. A level is proven only by a gate
that shows it. This pass did not add that gate.

| Level | Class | Why |
| --- | --- | --- |
| SH0 | HOST-TESTED on earlier trees | Host GCC and `ld` build the image. `make host-gates` is the host suite. It was not re-run as a whole here. |
| SH1 | NOT PROVEN | In-kernel ChrisC is the bootstrap compiler. No QEMU marker shows a compiler that is itself a ChrisOS program compiling and running an application. |
| SH2 | NOT PROVEN | No stage-1 and stage-2 compiler gate. |
| SH3 | NOT PROVEN | Libraries, desktop, and tools are not rebuilt inside the OS by a gate. |
| SH4 | NOT PROVEN | KCC's level 0 accepts `tools/kcc_fixtures/level0.c` and rejects `kernel/metal/serial.c`. It has no preprocessor, AST, or volatile MMIO lowering for real kernel files. ChrisAsm does not implement `cli`, `hlt`, `invlpg`, or memory operands the kernel uses. `chrisld_link_objects` can take more than one object and reject duplicate globals. It has not linked `BIN/KERNEL.ELF` from those objects. |
| SH5 | NOT PROVEN | No install of an internally built kernel and no reboot that prints that kernel's hash. |
| SH6 | NOT PROVEN | A successor install still needs the host toolchain. Limine stays external, which the level allows, but the OS cannot yet build itself. |

Blocker for every level past SH0: the native toolchain does not compile
the kernel sources. Growing KCC by special cases for one file does not
move SH4. The next real step is one kernel translation unit through
preprocessor, parser, and volatile loads, with a GCC differential test.
That step was not taken in this pass.
